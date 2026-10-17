#include "printer_popups.h"

#include <Arduino.h>
#include <string.h>

// The services first: they bring ArduinoJson, whose templates do not survive
// the T() macro from lang.h in front of them.
#include "services/backend.h"
#include "services/bambuddy_api.h"
#include "services/printer_watch.h"
#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "ui/confirm_popup.h"
#include "ui/ams_view.h"
#include "ui/main_screen_helpers.h"
#include "ui/question_box.h"
#include "ui/theme.h"
#include "ui_common.h"

// What the box is asking. One box, rebuilt per step, so the steps of the
// question after a print read as one conversation rather than three popups.
enum PpStep : uint8_t {
  PP_NONE = 0,
  PP_VERDICT,        // did the print come out well
  PP_SPOOL,          // is a spool empty, or does one need weighing
  PP_POWER_AFTER,    // switch the printer off after the print
  PP_POWER_ON,       // the chip, plug off: switch on?
  PP_POWER_OFF       // the chip, plug on: off now, or once cool?
};

// How long "put the empty spool down" stays armed after the question.
#define PP_ARCHIVE_ARM_MS  (5UL * 60UL * 1000UL)

static lv_obj_t* s_scr      = nullptr;
static PpStep    s_step     = PP_NONE;
static volatile uint8_t s_answer = QB_NONE;   // QbAnswer, written by the box
static BbJob     s_job;

static lv_obj_t* s_chip     = nullptr;
static lv_obj_t* s_chip_lbl = nullptr;
static bool      s_chip_tap = false;
static bool      s_chip_read_pending = false;
static int       s_chip_printer = 0;      // the printer the chip was painted for

static unsigned long s_archive_armed_ms = 0;
// The pad has been empty since "empty" was chosen. Only a spool put down
// after that is offered: whatever lay there while the question was answered
// is not necessarily the one that ran out.
static bool          s_archive_pad_cleared = false;
static bool          s_was_cooling = false;
static int           s_cool_shown  = -2;

bool isPrinterPopupOpen() { return s_scr != nullptr; }

void closePrinterPopup() {
  releaseScreen(&s_scr);
  s_step = PP_NONE;
  s_answer = QB_NONE;
}

// ---- the box ---------------------------------------------------------------

static void showBox(PpStep step, const char* title, const char* question, const char* hint,
                    int left, int mid, int right) {
  closePrinterPopup();
  s_step = step;
  s_scr = questionBoxCreate(title, question, hint, left, mid, right, &s_answer);
}

static void titleForJob(char* out, size_t n) {
  char head[40];
  copyT(head, sizeof(head), STR_PD_TITLE);
  if (s_job.name[0]) snprintf(out, n, "%s: %s", head, s_job.name);
  else               snprintf(out, n, "%s", head);
}

// ---- the steps -------------------------------------------------------------

static void askVerdict() {
  char title[96];
  titleForJob(title, sizeof(title));
  char q[64];
  copyT(q, sizeof(q), STR_PD_Q_OK);
  showBox(PP_VERDICT, title, q, nullptr, STR_PD_GOOD, STR_PD_LATER, STR_PD_BAD);
}

static void askSpool() {
  char title[96];
  titleForJob(title, sizeof(title));
  char q[64];
  copyT(q, sizeof(q), STR_PD_Q_SPOOL);
  showBox(PP_SPOOL, title, q, nullptr, STR_PD_REWEIGH, STR_PD_NEXT, STR_PD_EMPTY);
}

// The power step only for a plug that can be switched; without one the
// question ends here.
static void askPowerAfter() {
  printerWatchRefreshPlug();
  const BbPlug& p = printerWatchPlug();
  if (!p.found || !p.controllable) {
    closePrinterPopup();
    return;
  }
  char title[96];
  titleForJob(title, sizeof(title));
  char q[64], hint[96];
  copyT(q, sizeof(q), STR_PD_Q_POWER);
  if (p.auto_off) {
    copyT(hint, sizeof(hint), STR_PD_POWER_BB);
  } else {
    const int c = (p.off_by_temp && p.off_temp_c > 0) ? p.off_temp_c : PW_COOL_DEFAULT_C;
    snprintf(hint, sizeof(hint), T(STR_PD_POWER_HINT), c);
  }
  showBox(PP_POWER_AFTER, title, q, hint, STR_PD_OFF_COOL, 0, STR_PD_LEAVE_ON);
}

static void askPowerFromChip() {
  const BbPlug& p = printerWatchPlug();
  char title[48];
  snprintf(title, sizeof(title), "%s", p.name[0] ? p.name : T(STR_PWR_TITLE));
  char q[64];
  if (p.state_known && p.on) {
    copyT(q, sizeof(q), STR_PWR_Q_OFF);
    char hint[96];
    const int c = (p.off_by_temp && p.off_temp_c > 0) ? p.off_temp_c : PW_COOL_DEFAULT_C;
    snprintf(hint, sizeof(hint), T(STR_PD_POWER_HINT), c);
    showBox(PP_POWER_OFF, title, q, hint, STR_PD_OFF_COOL, STR_CANCEL, STR_PWR_OFF_NOW);
  } else {
    copyT(q, sizeof(q), STR_PWR_Q_ON);
    showBox(PP_POWER_ON, title, q, nullptr, STR_PWR_ON_NOW, STR_CANCEL, 0);
  }
}

// ---- the chip --------------------------------------------------------------

static void paintChip() {
  if (!s_chip_lbl) return;
  const BbPlug& p = printerWatchPlug();
  char buf[24];
  uint32_t col = UI_COL_CAPTION;
  if (!p.found) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_POWER " -");
  } else if (printerWatchCooling()) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_POWER " %s", T(STR_PWR_COOLING));
    col = UI_COL_WARN;
  } else if (!p.state_known) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_POWER " ?");
  } else {
    snprintf(buf, sizeof(buf), LV_SYMBOL_POWER " %s", T(p.on ? STR_PWR_ON : STR_PWR_OFF));
    col = p.on ? UI_COL_GOOD : UI_COL_CAPTION;
  }
  lv_label_set_text(s_chip_lbl, buf);
  lv_obj_set_style_text_color(s_chip_lbl, lv_color_hex(col), 0);
}

static void chipCb(lv_event_t* e) {
  s_chip_tap = true;
}

lv_obj_t* printerPowerChipCreate(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                                 lv_coord_t w, lv_coord_t h) {
  s_chip = lv_btn_create(parent);
  if (!s_chip) return nullptr;
  lv_obj_set_size(s_chip, w, h);
  lv_obj_set_pos(s_chip, x, y);
  lv_obj_set_style_bg_color(s_chip, lv_color_hex(UI_COL_ACCENT_CHIP), 0);
  lv_obj_set_style_radius(s_chip, 6, 0);
  lv_obj_set_style_shadow_width(s_chip, 0, 0);
  lv_obj_set_style_pad_all(s_chip, 0, 0);
  lv_obj_add_event_cb(s_chip, chipCb, LV_EVENT_CLICKED, nullptr);
  s_chip_lbl = lv_label_create(s_chip);
  lv_obj_set_style_text_font(s_chip_lbl, UI_FONT_SMALL, 0);
  lv_obj_align(s_chip_lbl, LV_ALIGN_CENTER, 0, 0);
  paintChip();
  s_chip_read_pending = true;
  return s_chip;
}

void printerPowerChipForget() {
  s_chip = nullptr;
  s_chip_lbl = nullptr;
  s_chip_tap = false;
  s_chip_read_pending = false;
  s_chip_printer = 0;
}

// ---- answers ---------------------------------------------------------------

static void reportSwitch(int code, bool on) {
  if (code == 200) {
    statusMessageShow(T(on ? STR_PWR_SWITCHED_ON : STR_PD_OFF_DONE), UI_COL_GOOD);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), T(STR_PD_OFF_FAIL), code);
    statusMessageShow(buf, UI_COL_BAD_TEXT);
  }
}

static void offWhenCool() {
  switch (printerWatchPowerOffWhenCool()) {
    case PW_OFF_BAMBUDDY: statusMessageShow(T(STR_PD_POWER_BB), UI_COL_GOOD); break;
    case PW_OFF_NOW:      statusMessageShow(T(STR_PD_OFF_DONE), UI_COL_GOOD); break;
    case PW_OFF_WAITING:  s_cool_shown = -2; break;   // the tick paints it
    default: break;
  }
}

static void handleAnswer() {
  const PpStep step = s_step;
  const uint8_t a = s_answer;
  s_answer = QB_NONE;
  closePrinterPopup();

  switch (step) {
    case PP_VERDICT:
      if (a == QB_MID) {
        // Later: asked about once, so the job is not asked about again.
        printerWatchTakeJob();
        return;
      }
      {
        const int code = bbSetJobVerdict(backendBaseUrl(), bambuddyApiKey(), s_job.id,
                                         a == QB_LEFT);
        logSDf("PrintDone: job %d verdict %s, HTTP %d", s_job.id,
               a == QB_LEFT ? "good" : "reject", code);
        printerWatchTakeJob();
        if (code != 200) {
          char buf[64];
          snprintf(buf, sizeof(buf), T(STR_PD_VERDICT_FAIL), code);
          statusMessageShow(buf, UI_COL_BAD_TEXT);
        }
      }
      askSpool();
      return;
    case PP_SPOOL:
      if (a == QB_RIGHT) {
        s_archive_armed_ms = millis();
        if (!s_archive_armed_ms) s_archive_armed_ms = 1;
        s_archive_pad_cleared = false;
        statusMessageShow(T(STR_PD_PUT_EMPTY), UI_COL_WARN);
      } else if (a == QB_LEFT) {
        statusMessageShow(T(STR_PD_PUT_WEIGH), UI_COL_GOOD);
      }
      askPowerAfter();
      return;
    case PP_POWER_AFTER:
      if (a == QB_LEFT) offWhenCool();
      return;
    case PP_POWER_ON:
      if (a == QB_LEFT) reportSwitch(printerWatchSwitch(true), true);
      paintChip();
      return;
    case PP_POWER_OFF:
      if (a == QB_LEFT) offWhenCool();
      else if (a == QB_RIGHT) reportSwitch(printerWatchSwitch(false), false);
      paintChip();
      return;
    default:
      return;
  }
}

// "Empty: archive" armed: the next known spool that turns up on the pad is
// offered for archiving through the screen's own confirmation, which also
// does the write.
static void archiveTick() {
  if (!s_archive_armed_ms) return;
  if (millis() - s_archive_armed_ms > PP_ARCHIVE_ARM_MS) {
    s_archive_armed_ms = 0;
    return;
  }
  const bool pad_empty = !tag_present && scale_weight_g < 50.0f;
  if (pad_empty) {
    s_archive_pad_cleared = true;
    return;
  }
  if (!s_archive_pad_cleared) return;
  if (!sm_found || sm_id <= 0 || sm_archived || uiModalWaiting() || isPrinterPopupOpen()) return;
  s_archive_armed_ms = 0;
  logSDf("PrintDone: offering spool %d for archiving", sm_id);
  showConfirmPopup(T(STR_ARCHIVE_CONFIRM), 3);
}

static void coolingTick() {
  const bool cooling = printerWatchCooling();
  if (cooling) {
    const int t = printerWatchCoolingTemp();
    if (t >= 0 && t != s_cool_shown) {
      s_cool_shown = t;
      char buf[64];
      snprintf(buf, sizeof(buf), T(STR_PD_COOLING), printerWatchCoolingTarget(), t);
      statusMessageShow(buf, UI_COL_WARN);
      paintChip();
    }
  } else if (s_was_cooling) {
    // The wait ended: switched, a new print took over, or it timed out. The
    // plug state says which.
    printerWatchRefreshPlug();
    paintChip();
    if (printerWatchPlug().state_known && !printerWatchPlug().on) {
      statusMessageShow(T(STR_PD_OFF_DONE), UI_COL_GOOD);
    }
  }
  s_was_cooling = cooling;
}

void printerPopupsTick() {
  if (s_answer != QB_NONE && s_scr) {
    handleAnswer();
    return;
  }
  // Stepping to another printer in the AMS view repaints the chip for it.
  if (s_chip && amsViewPrinterId() > 0 && amsViewPrinterId() != s_chip_printer) {
    s_chip_read_pending = true;
  }
  if (s_chip && s_chip_read_pending) {
    s_chip_read_pending = false;
    s_chip_printer = amsViewPrinterId();
    printerWatchRefreshPlug();
    paintChip();
  }
  if (s_chip && s_chip_tap) {
    s_chip_tap = false;
    if (!isPrinterPopupOpen()) {
      printerWatchRefreshPlug();
      paintChip();
      if (printerWatchPlug().found && printerWatchPlug().controllable) askPowerFromChip();
      else statusMessageShow(T(STR_PWR_NO_PLUG), UI_COL_WARN);
    }
  }
  coolingTick();
  archiveTick();

  if (!isPrinterPopupOpen() && !uiModalWaiting()) {
    const BbJob* j = printerWatchPendingJob();
    if (j && (!s_job.id || s_job.id != j->id || s_step == PP_NONE)) {
      s_job = *j;
      askVerdict();
    }
  }
}
