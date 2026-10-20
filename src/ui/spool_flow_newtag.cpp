#include "spool_flow.h"
#include "ui_common.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <cstring>

#include "app_config.h"
#include "bambu/material_match.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/server_reach.h"
#include "services/spool_cache.h"
#include "services/backend_api.h"
#include "ui/spool_flow_internal.h"
#include "ui/main_screen_helpers.h"
#include "ui/theme.h"
#include "services/spool_extras.h"

// The popup that makes a new spool out of the tag on the reader.
// Moved out of spool_flow.cpp word for word; what it shares with the rest
// of the link flow is declared in spool_flow_internal.h.

// Creating a spool from the tag itself. Kept separate from the copy state:
// there is no template here, the tag is the only source.
lv_obj_t *scr_newtag        = nullptr;
static lv_obj_t *lbl_newtag_info   = nullptr;
static lv_obj_t *btn_newtag_w[NEWTAG_LABEL_COUNT] = { nullptr };
static int  newtag_label_weight    = 0;
static char newtag_material[16]    = "";   // base material, "PETG"
static char newtag_subtype[24]     = "";   // what follows it, "HF"
static char newtag_rgba[SPOOL_COLOR_HEX_MAX] = "";   // RRGGBBAA
// Snapshot too, and for a sharper reason than the others: the no-tag timer in
// app_loop.cpp wipes g_tag 60 s after the tag was last seen. Reading the brand
// live at confirm time meant a slow decision produced a spool with no vendor
// at all - and on a Spoolman server that is worse than it sounds, because
// find_or_create_filament() then builds a filament with no vendor and a name
// cut down to the bare material.
static char newtag_brand[32]       = "";
static char newtag_tray[36]        = "";   // same reason as newtag_brand
static char newtag_color_name[32]  = "";   // resolved from the colour value

// ============================================================
//  CREATE A SPOOL FROM THE TAG
//
//  The way out when no template fits: a brand new Bambu spool whose type is
//  not in the inventory yet. Everything the server needs is already on the
//  tag except the weights - the tag carries no gram value at all - so the
//  core is assumed to be a Bambu one and the nominal weight is picked from
//  the scale and left editable.
//
//  BamBuddy only. Spoolman and FilaMan want a filament_id for a new spool,
//  and a tag cannot supply one.
// ============================================================

void closeNewTagPopup() {
  releaseScreen(&scr_newtag);
  lbl_newtag_info = nullptr;
  for (int i = 0; i < NEWTAG_LABEL_COUNT; i++) btn_newtag_w[i] = nullptr;
}

// Net filament on the pad, measured against the assumed Bambu core.
static float newTagNetto() {
  float netto = spoolExtrasBareReading() - (float)BAMBU_CORE_WEIGHT_G;
  return netto < 0 ? 0 : netto;
}

// Repaints the four choices so the active one is obvious, and refreshes the
// text underneath it.
static void newTagRefresh() {
  static const int choices[NEWTAG_LABEL_COUNT] = NEWTAG_LABEL_CHOICES;
  for (int i = 0; i < NEWTAG_LABEL_COUNT; i++) {
    if (!btn_newtag_w[i]) continue;
    bool on = (choices[i] == newtag_label_weight);
    lv_obj_set_style_bg_color(btn_newtag_w[i], lv_color_hex(on ? UI_COL_OK_BG : UI_COL_ROW), 0);
    lv_obj_set_style_border_width(btn_newtag_w[i], 1, 0);
    lv_obj_set_style_border_color(btn_newtag_w[i], lv_color_hex(on ? UI_COL_ACCENT : UI_COL_LINE), 0);
  }
  if (lbl_newtag_info) {
    // The snapshot, not g_tag: a clear filament has no color_hex to show, and
    // the snapshot is what the spool will be created with.
    char rgba[SPOOL_COLOR_HEX_MAX + 1];
    snprintf(rgba, sizeof(rgba), "#%s", newtag_rgba);
    char msg[192];
    snprintf(msg, sizeof(msg), T(STR_NEWTAG_MSG),
             newtag_brand, g_tag.material,
             newtag_color_name[0] ? newtag_color_name : (newtag_rgba[0] ? rgba : "-"),
             newTagNetto());
    lv_label_set_text(lbl_newtag_info, msg);
  }
}

// Creates the spool and hands it to the main screen, exactly as a copy would.
void doCreateSpoolFromTag() {
  if (!wifi_ok) return;

  int new_id = 0;
  int code = serverReachNote(backendCreateSpoolFromTag(newtag_material, newtag_subtype, newtag_brand,
                                       newtag_rgba, newtag_color_name, newtag_label_weight,
                                       BAMBU_CORE_WEIGHT_G, newTagNetto(),
                                       g_tag.temp_min, g_tag.temp_max, &new_id, 8000), true);
  if ((code == 200 || code == 201) && new_id > 0) {
    Serial.printf("New spool from tag: new ID=%d\n", new_id);
    logSDf("New spool from tag: mat=%s sub=%s brand=%s col=%s rgba=%s label=%d new_spool_id=%d",
           newtag_material, newtag_subtype, newtag_brand, newtag_color_name,
           newtag_rgba, newtag_label_weight, new_id);
    spoolCacheForget("spool created from a tag");
    // The spool exists either way; a tag that could not be bound has said so
    // on the status line, and that must stay readable.
    if (finishCopyFlow(new_id, newtag_tray)) statusMessageShow(T(STR_NEWTAG_OK), UI_COL_GOOD);
    return;
  }
  logSDf("New spool from tag failed: HTTP %d", code);
  char fail_buf[40]; copyT(fail_buf, sizeof(fail_buf), STR_NEWTAG_FAIL);
  lv_label_set_text(lbl_status, fail_buf);
  lv_obj_set_style_text_color(lbl_status, lv_color_hex(UI_COL_BAD_TEXT), 0);
}

void showNewFromTagPopup() {
  logSD("SHOW: NewFromTagPopup");
  closeNewTagPopup();

  // Split "PETG HF" into the material BamBuddy stores and the subtype beside
  // it. extractBambuSubtype() hands back the tail; its return value answers a
  // different question (the PLA blacklist) and is not used here.
  newtag_subtype[0] = '\0';
  extractBambuSubtype(g_tag.material, newtag_subtype, sizeof(newtag_subtype));
  size_t head = 0;
  while (g_tag.material[head] && g_tag.material[head] != ' ' && g_tag.material[head] != '-') head++;
  if (head >= sizeof(newtag_material)) head = sizeof(newtag_material) - 1;
  memcpy(newtag_material, g_tag.material, head);
  newtag_material[head] = '\0';

  strncpy(newtag_brand, g_tag.vendor[0] ? g_tag.vendor : BAMBU_VENDOR_NAME,
          sizeof(newtag_brand)-1);
  newtag_brand[sizeof(newtag_brand)-1] = '\0';
  strncpy(newtag_tray, g_tag.tray_uuid, sizeof(newtag_tray)-1);
  newtag_tray[sizeof(newtag_tray)-1] = '\0';

  // RRGGBBAA as the tag holds it, alpha included. It used to be forced to FF,
  // which created a clear spool as opaque black and a translucent one as solid.
  // BamBuddy stores the same four bytes the printer reports for the tray, so
  // 00000000 reaches it as the clear spool it is. Left empty when the colour
  // block did not read - an absent field is the honest answer.
  if (g_tag.color.valid) {
    snprintf(newtag_rgba, sizeof(newtag_rgba), "%06X%02X",
             (unsigned)g_tag.color.rgb, (unsigned)g_tag.color.alpha);
  } else {
    newtag_rgba[0] = '\0';
  }

  // The tag has the colour as a value only. Ask the backend for its name, so
  // the new spool reads "PETG HF Orange" rather than a bare hex nobody can
  // shop for. An unknown colour simply leaves the field empty, and so does a
  // clear one: the catalogue is keyed on RGB alone, and 000000 would come back
  // as "Black".
  newtag_color_name[0] = '\0';
  if (newtag_rgba[0] && spoolColorNamesHue(g_tag.color)) {
    backendLookupColorName(newtag_rgba, g_tag.material,
                           newtag_color_name, sizeof(newtag_color_name));
  }

  // Nearest nominal weight to what is actually on the pad.
  static const int choices[NEWTAG_LABEL_COUNT] = NEWTAG_LABEL_CHOICES;
  float netto = newTagNetto();
  newtag_label_weight = choices[NEWTAG_LABEL_COUNT - 1];
  int best = -1;
  for (int i = 0; i < NEWTAG_LABEL_COUNT; i++) {
    int diff = (int)(netto > choices[i] ? netto - choices[i] : choices[i] - netto);
    if (best < 0 || diff < best) { best = diff; newtag_label_weight = choices[i]; }
  }

  scr_newtag = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_newtag, 480, 320);
  lv_obj_set_pos(scr_newtag, 0, 0);
  lv_obj_set_style_bg_color(scr_newtag, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scr_newtag, LV_OPA_70, 0);
  lv_obj_set_style_border_width(scr_newtag, 0, 0);
  lv_obj_set_style_pad_all(scr_newtag, 0, 0);
  lv_obj_clear_flag(scr_newtag, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(scr_newtag);
  lv_obj_set_size(box, 440, 284);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_radius(box, 10, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(box);
  char title_buf[40]; copyT(title_buf, sizeof(title_buf), STR_NEWTAG_TITLE);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 10);

  lbl_newtag_info = lv_label_create(box);
  lv_obj_set_style_text_color(lbl_newtag_info, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl_newtag_info, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_newtag_info, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_newtag_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_newtag_info, 410);
  lv_obj_align(lbl_newtag_info, LV_ALIGN_TOP_MID, 0, 38);

  lv_obj_t *lbl_lw = lv_label_create(box);
  char lw_buf[24]; copyT(lw_buf, sizeof(lw_buf), STR_NEWTAG_LABEL_W);
  lv_label_set_text(lbl_lw, lw_buf);
  lv_obj_set_style_text_color(lbl_lw, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(lbl_lw, &lv_font_montserrat_ext_12, 0);
  lv_obj_align(lbl_lw, LV_ALIGN_TOP_MID, 0, 108);

  // Four nominal weights side by side. The index is stored on the button so
  // one shared callback serves all of them.
  const int W_BTN = 100, W_GAP = 6, W_Y = 128;
  const int w_x0 = (440 - (NEWTAG_LABEL_COUNT * W_BTN + (NEWTAG_LABEL_COUNT - 1) * W_GAP)) / 2;
  for (int i = 0; i < NEWTAG_LABEL_COUNT; i++) {
    lv_obj_t *b = lv_btn_create(box);
    lv_obj_set_size(b, W_BTN, 46);
    lv_obj_set_pos(b, w_x0 + i * (W_BTN + W_GAP), W_Y);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_user_data(b, (void*)(intptr_t)i);
    lv_obj_add_event_cb(b, [](lv_event_t *e) {
      static const int ch[NEWTAG_LABEL_COUNT] = NEWTAG_LABEL_CHOICES;
      int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
      if (idx < 0 || idx >= NEWTAG_LABEL_COUNT) return;
      newtag_label_weight = ch[idx];
      newTagRefresh();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    char wb[12]; snprintf(wb, sizeof(wb), "%d g", choices[i]);
    lv_label_set_text(l, wb);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_14, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    btn_newtag_w[i] = b;
  }

  lv_obj_t *btn_ok = lv_btn_create(box);
  lv_obj_set_size(btn_ok, 200, 52);
  lv_obj_set_pos(btn_ok, 12, 194);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(UI_COL_OK_BG), 0);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(UI_COL_OK_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_ok, 8, 0);
  lv_obj_set_style_shadow_width(btn_ok, 0, 0);
  lv_obj_add_event_cb(btn_ok, [](lv_event_t *e) {
    logSD("BTN: NewFromTag -> Confirm");
    newtag_create_pending = true;
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_ok);
    char b[32]; copyT(b, sizeof(b), STR_BTN_CONFIRMED);
    lv_label_set_text(l, b);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_OK_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0); }

  lv_obj_t *btn_no = lv_btn_create(box);
  lv_obj_set_size(btn_no, 200, 52);
  lv_obj_set_pos(btn_no, 228, 194);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_no, 8, 0);
  lv_obj_set_style_shadow_width(btn_no, 0, 0);
  lv_obj_add_event_cb(btn_no, [](lv_event_t *e) {
    logSD("BTN: NewFromTag -> Cancel");
    closeNewTagPopup();
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_no);
    char b[32]; copyT(b, sizeof(b), STR_CANCEL);
    lv_label_set_text(l, b);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0); }

  newTagRefresh();
}
