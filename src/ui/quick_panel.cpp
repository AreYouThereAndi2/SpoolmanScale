#include "quick_panel.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "app/app_state.h"
#include "app/deferred_actions.h"
#include "app_config.h"
#include "hardware/display.h"
#include "hardware/scale.h"
#include "hardware/scale_state.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/prefs_store.h"
#include "ui/navigation.h"
#include "ui/settings_screen.h"
#include "ui/theme.h"
#include "ui/ui_common.h"

#define QP_H          206    // the panel's height; what slides
#define QP_SLIDE_MS   180
#define QP_TILE_W     108
#define QP_TILE_H     62

// What a button asked for, run from quickPanelTick() after the panel is gone.
enum QpAction : uint8_t { QP_NONE, QP_TARE, QP_AMS, QP_INFO, QP_SETTINGS };

static lv_obj_t* s_scrim = nullptr;   // the dimmed scale behind; holds the panel
static lv_obj_t* s_panel = nullptr;
static QpAction s_action = QP_NONE;

bool quickPanelIsOpen() { return s_scrim != nullptr; }

static void setY(void* obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, (lv_coord_t)v); }

static void deleteNow() {
  if (!s_scrim) return;
  lv_anim_del(s_panel, setY);
  lv_obj_t* gone = s_scrim;
  s_scrim = s_panel = nullptr;
  lv_obj_del_async(gone);
}

void quickPanelClose(bool animated) {
  if (!s_scrim) return;
  if (!animated) {
    deleteNow();
    return;
  }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_panel);
  lv_anim_set_exec_cb(&a, setY);
  lv_anim_set_values(&a, lv_obj_get_y(s_panel), -QP_H);
  lv_anim_set_time(&a, QP_SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_ready_cb(&a, [](lv_anim_t*) { deleteNow(); });
  lv_anim_start(&a);
}

static void onTile(lv_event_t* e) {
  s_action = (QpAction)(uintptr_t)lv_event_get_user_data(e);
  logSDf("Quick panel: action %d", (int)s_action);
}

static lv_obj_t* tile(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, const char* icon, int label,
                      QpAction action) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, QP_TILE_W, QP_TILE_H);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_radius(b, UI_RADIUS_BOX, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_add_event_cb(b, onTile, LV_EVENT_CLICKED, (void*)(uintptr_t)action);
  lv_obj_t* ic = lv_label_create(b);
  lv_label_set_text(ic, icon);
  lv_obj_set_style_text_font(ic, UI_FONT_TITLE, 0);
  lv_obj_set_style_text_color(ic, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_t* lb = lv_label_create(b);
  lv_label_set_text(lb, T(label));
  lv_obj_set_style_text_font(lb, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(lb, lv_color_hex(UI_COL_INK), 0);
  lv_obj_align(lb, LV_ALIGN_BOTTOM_MID, 0, -6);
  return b;
}

static lv_obj_t* line(lv_obj_t* parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                      const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

void quickPanelOpen() {
  if (s_scrim) return;
  logSD("Quick panel: open");
  // The scale dimmed behind it; a tap there closes the panel.
  s_scrim = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_scrim, LV_HOR_RES, LV_VER_RES);
  lv_obj_set_pos(s_scrim, 0, 0);
  lv_obj_set_style_bg_color(s_scrim, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(s_scrim, LV_OPA_50, 0);
  lv_obj_set_style_border_width(s_scrim, 0, 0);
  lv_obj_set_style_radius(s_scrim, 0, 0);
  lv_obj_set_style_pad_all(s_scrim, 0, 0);
  lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_scrim, [](lv_event_t* e) {
    if (lv_event_get_target(e) == s_scrim) quickPanelClose();
  }, LV_EVENT_CLICKED, nullptr);

  s_panel = lv_obj_create(s_scrim);
  lv_obj_set_size(s_panel, LV_HOR_RES, QP_H);
  lv_obj_set_pos(s_panel, 0, -QP_H);
  lv_obj_set_style_bg_color(s_panel, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_panel, 0, 0);
  lv_obj_set_style_radius(s_panel, UI_RADIUS_BOX, 0);
  lv_obj_set_style_pad_all(s_panel, 0, 0);
  lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

  // Wi-Fi and the backend at a glance.
  char buf[96];
  lv_obj_t* wifi = line(s_panel, 14, 12, 220, UI_FONT_SMALL, UI_COL_INK);
  if (WiFi.isConnected())
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s  %s", WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str());
  else
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", T(STR_NO_WIFI));
  lv_label_set_text(wifi, buf);
  lv_obj_t* be = line(s_panel, 246, 12, 220, UI_FONT_SMALL, UI_COL_INK_2);
  snprintf(buf, sizeof(buf), LV_SYMBOL_DRIVE "  %s  %s", backendName(), backendHost());
  lv_label_set_text(be, buf);
  lv_obj_set_style_text_align(be, LV_TEXT_ALIGN_RIGHT, 0);

  // The brightness, as the display settings set it.
  lv_obj_t* dim = lv_label_create(s_panel);
  lv_label_set_text(dim, LV_SYMBOL_EYE_CLOSE);
  lv_obj_set_style_text_color(dim, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_pos(dim, 18, 48);
  lv_obj_t* slider = lv_slider_create(s_panel);
  lv_obj_set_size(slider, 372, 16);
  lv_obj_set_pos(slider, 54, 52);
  lv_slider_set_range(slider, BRIGHT_MIN, BRIGHT_MAX);
  lv_slider_set_value(slider, bright_normal, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COL_LINE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COL_ACCENT), LV_PART_KNOB);
  lv_obj_set_ext_click_area(slider, 14);
  lv_obj_add_event_cb(slider, [](lv_event_t* e) {
    bright_normal = lv_slider_get_value(lv_event_get_target(e));
    displaySetBrightness((uint8_t)bright_normal);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(slider, [](lv_event_t* e) {
    prefsPutUChar("bright", (uint8_t)lv_slider_get_value(lv_event_get_target(e)));
  }, LV_EVENT_RELEASED, nullptr);
  lv_obj_t* bright = lv_label_create(s_panel);
  lv_label_set_text(bright, LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(bright, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_pos(bright, 440, 48);

  // What is done most often, one tap each.
  const lv_coord_t ty = 88, gap = (LV_HOR_RES - 4 * QP_TILE_W) / 5;
  tile(s_panel, gap, ty, LV_SYMBOL_DOWNLOAD, STR_QP_TARE, QP_TARE);
  tile(s_panel, 2 * gap + QP_TILE_W, ty, LV_SYMBOL_LIST, STR_QP_AMS, QP_AMS);
  tile(s_panel, 3 * gap + 2 * QP_TILE_W, ty, LV_SYMBOL_EYE_OPEN, STR_QP_INFO, QP_INFO);
  tile(s_panel, 4 * gap + 3 * QP_TILE_W, ty, LV_SYMBOL_SETTINGS, STR_QP_SETTINGS, QP_SETTINGS);

  // The handle, and what the edges do: found once, used from then on.
  lv_obj_t* hint = line(s_panel, 14, QP_H - 40, LV_HOR_RES - 28, UI_FONT_CAPTION, UI_COL_CAPTION);
  lv_label_set_text(hint, T(STR_QP_HINT));
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* grip = lv_obj_create(s_panel);
  lv_obj_set_size(grip, 44, 5);
  lv_obj_align(grip, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_radius(grip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(grip, 0, 0);
  lv_obj_set_style_bg_color(grip, lv_color_hex(UI_COL_INK_FAINT), 0);
  lv_obj_clear_flag(grip, LV_OBJ_FLAG_CLICKABLE);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_panel);
  lv_anim_set_exec_cb(&a, setY);
  lv_anim_set_values(&a, -QP_H, -UI_RADIUS_BOX);   // the top corners stay above the glass
  lv_anim_set_time(&a, QP_SLIDE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

void quickPanelAddHandle(lv_obj_t* header) {
  lv_obj_t* hit = lv_obj_create(header);
  lv_obj_set_size(hit, 48, 26);
  lv_obj_align(hit, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hit, 0, 0);
  lv_obj_set_style_pad_all(hit, 0, 0);
  lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(hit, [](lv_event_t*) {
    if (!uiModalWaiting()) quickPanelOpen();
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* pill = lv_obj_create(hit);
  lv_obj_set_size(pill, 32, 4);
  lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(pill, 0, 0);
  lv_obj_set_style_bg_color(pill, lv_color_hex(UI_COL_INK_FAINT), 0);
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);
}

static void tare() {
  // As the main screen's button: presence as well as scale_ready, an ADC that
  // left the bus reads all ones and would store -1 as the zero point.
  if (!(scale_ready && scaleHardwarePresent())) {
    logSD("TARE (quick panel): scale not ready");
    return;
  }
  const int32_t raw = scaleHardwareReadRaw();
  saveTareOffset(raw);
  scale_weight_g = 0.0f;
  resetScaleFilter();
  if (lbl_scale_weight) lv_label_set_text(lbl_scale_weight, "0 g");
  logSDf("TARE (quick panel, raw=%d)", raw);
}

void quickPanelTick() {
  if (s_action == QP_NONE) return;
  const QpAction a = s_action;
  s_action = QP_NONE;
  quickPanelClose(false);
  switch (a) {
    case QP_TARE:
      tare();
      break;
    case QP_AMS:
      showMainScreen();
      show_ams_view_pending = true;
      break;
    case QP_INFO:
      showMainScreen();
      show_info_pending = true;
      break;
    case QP_SETTINGS:
      showMainScreen();
      showSettingsScreen();
      break;
    default:
      break;
  }
}

static void setTy(void* obj, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)obj, (lv_coord_t)v, 0); }

void uiSheetIn(lv_obj_t* box) {
  if (!box) return;
  // Only a short way: the box is drawn anew each step, and a short slide is
  // what the display bus shows smoothly.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, box);
  lv_anim_set_exec_cb(&a, setTy);
  lv_anim_set_values(&a, 36, 0);
  lv_anim_set_time(&a, 150);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}
