#include "spool_flow.h"
#include "ui_common.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <cstring>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/server_reach.h"
#include "services/tag_field.h"
#include "services/tag_uid.h"
#include "services/user_options.h"
#include "services/backend_api.h"
#include "ui/spool_flow_internal.h"
#include "services/spool_tare.h"
#include "ui/tag_spool_compare.h"
#include "ui/theme.h"
#include "services/backend.h"
#include "services/tag_spool_match.h"

// Linking by spool id: the keypad, the lookup behind it, and the two
// warnings a link can run into.
// Moved out of spool_flow.cpp word for word; what it shares with the rest
// of the link flow is declared in spool_flow_internal.h.

// ============================================================
//  LINK FLOW: HELPER - create overlay base
// ============================================================
lv_obj_t* buildLinkOverlay() {
  lv_obj_t *scr = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr, 480, 320);
  lv_obj_set_pos(scr, 0, 0);
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

// ============================================================
//  LINK FLOW: WARNING POPUP A (spool already linked)
// ============================================================
void showWarnPopupA(int spool_id, const char* existing_tag, bool is_bambu,
                    const char* link_uuid, bool add_mode) {
  logSDf("SHOW: WarnPopupA spool=%d", spool_id);
  releaseScreen(&scr_link_warn_a);

  scr_link_warn_a = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_link_warn_a, 480, 320);
  lv_obj_set_pos(scr_link_warn_a, 0, 0);
  lv_obj_set_style_bg_color(scr_link_warn_a, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scr_link_warn_a, LV_OPA_80, 0);
  lv_obj_set_style_border_width(scr_link_warn_a, 0, 0);
  lv_obj_set_style_radius(scr_link_warn_a, 0, 0);
  lv_obj_set_style_pad_all(scr_link_warn_a, 0, 0);
  lv_obj_clear_flag(scr_link_warn_a, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(scr_link_warn_a);
  lv_obj_set_size(box, 440, 262);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  // Warning icon + title
  lv_obj_t *lbl_title = lv_label_create(box);
  lv_label_set_text(lbl_title, T(add_mode ? STR_WARN_A_ADD_TITLE : STR_WARN_A_TITLE));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 16);

  // Separator line
  lv_obj_t *line = lv_obj_create(box);
  lv_obj_set_size(line, 420, 1);
  lv_obj_set_pos(line, 10, 42);
  lv_obj_set_style_bg_color(line, lv_color_hex(UI_COL_CAUTION_BG), 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_radius(line, 0, 0);
  lv_obj_set_style_pad_all(line, 0, 0);

  // ID + shortened tag
  char tag_short[14];
  snprintf(tag_short, sizeof(tag_short), "%.10s...", existing_tag);

  // Get material and name from link_spools
  const char* sm_mat  = "";
  const char* sm_name = "";
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].id == spool_id) {
      sm_mat  = link_spools[i].material;
      sm_name = link_spools[i].name;
      break;
    }
  }
  char info_buf[96];
  if (add_mode) {
    // The UID list is too long to show and too dull to read. The count is what
    // the user needs in order to recognise the spool as one that is already
    // bound, and nothing is being replaced here anyway.
    const int n = cardUidsCount(existing_tag);
    if (sm_mat[0] || sm_name[0]) {
      snprintf(info_buf, sizeof(info_buf), T(STR_WARN_A_ADD_INFO),
        spool_id, sm_mat, sm_name, n);
    } else {
      snprintf(info_buf, sizeof(info_buf), T(STR_WARN_A_ADD_SHORT), spool_id, n);
    }
  } else if (sm_mat[0] || sm_name[0]) {
    snprintf(info_buf, sizeof(info_buf), T(STR_WARN_A_SPOOL_INFO),
      spool_id, sm_mat, sm_name, tag_short);
  } else {
    snprintf(info_buf, sizeof(info_buf), T(STR_WARN_A_SPOOL_SHORT), spool_id, tag_short);
  }
  lv_obj_t *lbl_info = lv_label_create(box);
  lv_label_set_text(lbl_info, info_buf);
  lv_obj_set_style_text_color(lbl_info, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_info, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_info, 400);
  lv_obj_align(lbl_info, LV_ALIGN_TOP_MID, 0, 54);

  // Three buttons: link anyway / enter new ID / cancel
  // Button: link anyway
  // We pass spool_id and is_bambu via static captures (lambda workaround: user_data)
  static int  warn_a_spool_id = 0;
  static bool warn_a_is_bambu = false;
  warn_a_spool_id = spool_id;
  warn_a_is_bambu = is_bambu;

  lv_obj_t *btn_force = lv_btn_create(box);
  lv_obj_set_size(btn_force, 420, 44);
  lv_obj_set_pos(btn_force, 10, 114);
  // Adding is not the destructive act overwriting is, so it gets the calm
  // green of a normal confirmation rather than the warning amber.
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(add_mode ? UI_COL_GO_BG : UI_COL_CAUTION_BG), 0);
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(add_mode ? UI_COL_GO_BG_PRESSED : UI_COL_CAUTION_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_force, 8, 0);
  lv_obj_set_style_shadow_width(btn_force, 0, 0);
  lv_obj_set_style_border_width(btn_force, 0, 0);
  lv_obj_add_event_cb(btn_force, [](lv_event_t *e) {
    // Parked: the popup this button sits on and the numpad behind it are
    // taken down by the handler, then the PATCH runs from the loop.
    link_patch_id    = warn_a_spool_id;
    link_patch_bambu = warn_a_is_bambu;
    link_patch_pending = true;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_force = lv_label_create(btn_force);
  lv_label_set_text(lbl_force, T(add_mode ? STR_BTN_ADD_UID : STR_BTN_OVERWRITE));
  lv_obj_set_style_text_color(lbl_force, lv_color_hex(add_mode ? UI_COL_OK_TEXT_2 : UI_COL_WARN), 0);
  lv_obj_set_style_text_font(lbl_force, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_force);

  lv_obj_t *btn_retry = lv_btn_create(box);
  lv_obj_set_size(btn_retry, 420, 44);
  lv_obj_set_pos(btn_retry, 10, 166);  // 114+44+8
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_retry, 8, 0);
  lv_obj_set_style_shadow_width(btn_retry, 0, 0);
  lv_obj_set_style_border_width(btn_retry, 1, 0);
  lv_obj_set_style_border_color(btn_retry, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_add_event_cb(btn_retry, [](lv_event_t *e) {
    logSD("BTN: WarnA -> retry IdInput (flag)");
    releaseScreen(&scr_link_warn_a);
    releaseScreen(&scr_link_id);
    link_id_input[0] = '\0';
    link_id_lookup_pending = 0;
    show_id_input_rebuild = true;  // loop rebuilds IdInputPopup safely
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_retry = lv_label_create(btn_retry);
  lv_label_set_text(lbl_retry, T(STR_ENTER_NEW_ID));
  lv_obj_set_style_text_color(lbl_retry, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl_retry, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_retry);

  lv_obj_t *btn_cancel = lv_btn_create(box);
  lv_obj_set_size(btn_cancel, 420, 36);
  lv_obj_set_pos(btn_cancel, 10, 218);  // 166+44+8
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_cancel, 0, 0);
  lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e) {
    releaseScreen(&scr_link_warn_a);
    releaseScreen(&scr_link_id);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_cancel, T(STR_CANCEL));
  lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_ext_14, 0);
  lv_obj_center(lbl_cancel);
}

// ============================================================
//  LINK FLOW: WARNING POPUP B (the tag does not match the spool)
//  Nur Flow A (Bambu), Pfad 1
// ============================================================
// What B is about: the verdict its rows show, and the question about a bound
// spool that still has to follow once the difference is accepted. That one
// used to come first and return, so a bound spool never reached this popup.
static TagSpoolVerdict s_warn_b_verdict;
static bool   s_warn_b_then_a = false;
static String s_warn_b_a_tag;
static bool   s_warn_b_a_add  = false;

// Where B's tag-against-spool rows start, and the room between the blocks.
#define WARN_B_ROWS_Y       52
#define WARN_B_ROWS_GAP     16
#define WARN_B_BTN_STEP     56
#define WARN_B_CANCEL_STEP  52
#define WARN_B_CANCEL_H     36
#define WARN_B_BOTTOM_PAD   10

void showWarnPopupB(int spool_id, bool is_bambu) {
  logSDf("SHOW: WarnPopupB spool=%d", spool_id);
  releaseScreen(&scr_link_warn_b);

  static int  warn_b_spool_id = 0;
  static bool warn_b_is_bambu = false;
  warn_b_spool_id = spool_id;
  warn_b_is_bambu = is_bambu;

  scr_link_warn_b = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_link_warn_b, 480, 320);
  lv_obj_set_pos(scr_link_warn_b, 0, 0);
  lv_obj_set_style_bg_color(scr_link_warn_b, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scr_link_warn_b, LV_OPA_80, 0);
  lv_obj_set_style_border_width(scr_link_warn_b, 0, 0);
  lv_obj_set_style_radius(scr_link_warn_b, 0, 0);
  lv_obj_set_style_pad_all(scr_link_warn_b, 0, 0);
  lv_obj_clear_flag(scr_link_warn_b, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(scr_link_warn_b);
  lv_obj_set_size(box, 440, 260);   // grown to fit below, once the rows are in
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(box);
  lv_label_set_text(lbl_title, T(STR_LINK_MISMATCH_TITLE));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 16);

  lv_obj_t *line = lv_obj_create(box);
  lv_obj_set_size(line, 420, 1);
  lv_obj_set_pos(line, 10, 42);
  lv_obj_set_style_bg_color(line, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_radius(line, 0, 0);
  lv_obj_set_style_pad_all(line, 0, 0);

  // Tag and spool side by side. The spool's side comes from its row, which
  // linkIdLookupAndPatch() made from the document it just read.
  const UnlinkedSpool* row = nullptr;
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].id == spool_id) { row = &link_spools[i]; break; }
  }
  const int rows_h = tagSpoolCompareRows(box, WARN_B_ROWS_Y, s_warn_b_verdict,
                                         row ? row->material : "", row ? row->color_hex : "",
                                         row ? row->vendor : "");
  const int force_y  = WARN_B_ROWS_Y + rows_h + WARN_B_ROWS_GAP;
  const int retry_y  = force_y + WARN_B_BTN_STEP;
  const int cancel_y = retry_y + WARN_B_CANCEL_STEP;
  lv_obj_set_height(box, cancel_y + WARN_B_CANCEL_H + WARN_B_BOTTOM_PAD);

  lv_obj_t *btn_force = lv_btn_create(box);
  lv_obj_set_size(btn_force, 420, 48);
  lv_obj_align(btn_force, LV_ALIGN_TOP_MID, 0, force_y);
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_force, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_force, 8, 0);
  lv_obj_set_style_shadow_width(btn_force, 0, 0);
  lv_obj_set_style_border_width(btn_force, 0, 0);
  lv_obj_add_event_cb(btn_force, [](lv_event_t *e) {
    // A bound spool still gets its own question: this one accepted the
    // difference, that one decides between replacing and adding the tag.
    if (s_warn_b_then_a) {
      s_warn_b_then_a = false;
      releaseScreen(&scr_link_warn_b);
      showWarnPopupA(warn_b_spool_id, s_warn_b_a_tag.c_str(), warn_b_is_bambu, "",
                     s_warn_b_a_add);
      return;
    }
    link_patch_id    = warn_b_spool_id;
    link_patch_bambu = warn_b_is_bambu;
    link_patch_pending = true;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_force = lv_label_create(btn_force);
  lv_label_set_text(lbl_force, T(STR_BTN_LINK_ANYWAY));
  lv_obj_set_style_text_color(lbl_force, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_force, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_force);

  lv_obj_t *btn_retry = lv_btn_create(box);
  lv_obj_set_size(btn_retry, 420, 44);
  lv_obj_align(btn_retry, LV_ALIGN_TOP_MID, 0, retry_y);
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(btn_retry, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_retry, 8, 0);
  lv_obj_set_style_shadow_width(btn_retry, 0, 0);
  lv_obj_set_style_border_width(btn_retry, 1, 0);
  lv_obj_set_style_border_color(btn_retry, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_add_event_cb(btn_retry, [](lv_event_t *e) {
    // Same route as the retry on popup A: the loop rebuilds the numpad once
    // this popup is gone.
    releaseScreen(&scr_link_warn_b);
    releaseScreen(&scr_link_id);
    link_id_input[0] = '\0';
    link_id_lookup_pending = 0;
    show_id_input_rebuild = true;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_retry = lv_label_create(btn_retry);
  lv_label_set_text(lbl_retry, T(STR_ENTER_NEW_ID));
  lv_obj_set_style_text_color(lbl_retry, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl_retry, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(lbl_retry);

  lv_obj_t *btn_cancel = lv_btn_create(box);
  lv_obj_set_size(btn_cancel, 420, WARN_B_CANCEL_H);
  lv_obj_align(btn_cancel, LV_ALIGN_TOP_MID, 0, cancel_y);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(UI_COL_QUIET_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_cancel, 0, 0);
  lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e) {
    releaseScreen(&scr_link_warn_b);
    releaseScreen(&scr_link_id);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
  lv_label_set_text(lbl_cancel, T(STR_CANCEL));
  lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_ext_14, 0);
  lv_obj_center(lbl_cancel);
}

// Grow link_spools to hold at least `needed` entries. PSRAM first, internal
// RAM as fallback, same order the fetch functions use. Returns false if the
// list could not be grown, in which case the caller must not append.
static bool linkSpoolsEnsureCapacity(int needed) {
  if (link_spools && link_spools_capacity >= needed) return true;

  size_t bytes = (size_t)needed * sizeof(UnlinkedSpool);
  UnlinkedSpool* grown = (UnlinkedSpool*)heap_caps_realloc(link_spools, bytes, MALLOC_CAP_SPIRAM);
  if (!grown) grown = (UnlinkedSpool*)realloc(link_spools, bytes);
  if (!grown) {
    logSDf("link spools: grow to %d entries failed", needed);
    return false;
  }
  link_spools          = grown;
  link_spools_capacity = needed;
  return true;
}

// ============================================================
//  LINK-FLOW: HTTP-LOOKUP + VERKNUEPFUNG (ausgelagert vom Lambda)
// ============================================================
void linkIdLookupAndPatch(int entered_id, bool is_bambu) {
  if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_CHECKING));
  lv_timer_handler();
  if (!wifi_ok) { if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_NO_WIFI)); return; }

  // The ID path skips the list fetch, so it has to settle this for itself.
  link_cu_ok = tagFieldIsList() && g_card_uids_write &&
               backendHasExtraField(tagFieldKey());

  JsonDocument doc;
  DeserializationError err = DeserializationError::Ok;
  int code = serverReachNote(backendGetSpoolJson(cfg_spoolman_base, entered_id, doc, 5000, &err), true);
  // Before the "not found" below, which takes every negative code: a server
  // that never answered said nothing about the id.
  if (serverReachIsNetworkFailure(code)) {
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_NO_CONNECTION));
    return;
  }
  if (code == 404 || code < 0) {
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_ID_NOT_FOUND));
    return;
  }
  if (code == -2) {
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_JSON_ERR));
    return;
  }
  if (code != 200) {
    char err[32]; snprintf(err, sizeof(err), T(STR_LINK_HTTP_ERR), code);
    if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, err);
    return;
  }

  String existing = "";
  if (!doc["extra"]["tag"].isNull()) {
    existing = doc["extra"]["tag"].as<String>();
    existing.replace("\"",""); existing.trim();
  }
  // A spool managed by SpoolLink has an empty tag field but is bound all the
  // same. Kept apart from the tag field, because which of the two is set
  // decides whether the warning offers to overwrite or to add.
  String existing_cu = "";
  if (!doc["extra"][CARD_UIDS_FIELD].isNull()) {
    existing_cu = doc["extra"][CARD_UIDS_FIELD].as<String>();
    existing_cu.replace("\"",""); existing_cu.trim();
  }

  bool found_in_list = false;
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].id != entered_id) continue;
    found_in_list = true;
    // The row is already there, so nothing below appends one - and until the
    // list cache that was the end of it, because the row was seconds old and
    // held the spool's real tag values. A row out of the cache holds none.
    // Left like that, the warning below would count the UIDs in the fresh
    // document while doLinkPatch() reads this row through linkTargetValues(),
    // finds it empty, takes the spool for unbound and writes a new list over
    // the one it has. The document is in hand, so the row is made true here.
    if (link_spools[i].from_cache) {
      UnlinkedSpool &row = link_spools[i];
      linkFillTagValues(row, doc.as<JsonObjectConst>());
      row.remaining    = doc["remaining_weight"] | 0.0f;
      row.total        = doc["filament"]["weight"] | 0.0f;
      row.filament_id  = doc["filament"]["id"] | 0;
      row.spool_weight = spoolTare(doc);
      row.from_cache   = false;
      logSDf("link: row of spool %d read fresh behind the numpad", entered_id);
    }
    break;
  }
  // A hand entered ID is appended to the list so the rest of the flow can look
  // it up like any other spool. The list is either absent (no list loaded) or
  // allocated to an exact fit by the fetch functions, so it has to be grown
  // first - writing at link_spool_count without that was a heap overflow of one
  // UnlinkedSpool past the end of the block.
  if (!found_in_list && linkSpoolsEnsureCapacity(link_spool_count + 1)) {
    UnlinkedSpool &s = link_spools[link_spool_count];
    s.id = entered_id;
    s.from_cache = false;   // read from the server a moment ago
    s.article_hit = false;  // typed in, not offered by the list
    // Same rule as the list fetch: too long is stored as empty, never cut.
    linkFillTagValues(s, doc.as<JsonObjectConst>());
    String mat = doc["filament"]["material"] | String("");
    mat.trim(); strncpy(s.material, mat.c_str(), sizeof(s.material)-1);
    s.material[sizeof(s.material)-1] = '\0';
    String fname = doc["filament"]["name"] | String("?");
    fname.trim(); strncpy(s.name, fname.c_str(), sizeof(s.name)-1);
    s.name[sizeof(s.name)-1] = '\0';
    String vnd = doc["filament"]["vendor"]["name"] | String("");
    vnd.trim(); strncpy(s.vendor, vnd.c_str(), sizeof(s.vendor)-1);
    s.vendor[sizeof(s.vendor)-1] = '\0';
    String col = doc["filament"]["color_hex"] | String("");
    col.trim();
    if (col.length() > 0 && col[0] != '#') col = "#" + col;
    strncpy(s.color_hex, col.c_str(), sizeof(s.color_hex)-1);
    s.color_hex[sizeof(s.color_hex)-1] = '\0';
    // The four numbers the list fetch fills and this path used to leave alone.
    // link_spools[] comes from heap_caps_malloc, which does not zero, so a row
    // rendered afterwards showed whatever the block held before, and the copy
    // flow would have carried that straight into a new spool as its filament
    // id. Read from the same document as everything above.
    s.remaining    = doc["remaining_weight"] | 0.0f;
    s.total        = doc["filament"]["weight"] | 0.0f;
    s.filament_id  = doc["filament"]["id"] | 0;
    s.spool_weight = spoolTare(doc);
    link_spool_count++;
  }

  // While appending is possible neither store is being replaced, so both get
  // the friendlier "add one" variant - the tag field's UID moves into the list
  // and the new one joins it there. The list wins when both are set, because
  // that is the one being written.
  //
  // Without it the tag field wins instead: overwriting it is the destructive
  // case and has to be asked about first.
  bool   ask_a = true;
  String a_tag;
  bool   a_add = false;
  if (link_cu_ok && (existing_cu.length() > 0 || existing.length() > 0)) {
    a_tag = existing_cu.length() > 0 ? existing_cu : existing;
    a_add = true;
  } else if (existing.length() > 0) {
    a_tag = existing;
  } else if (existing_cu.length() > 0) {
    a_tag = existing_cu;
  } else {
    ask_a = false;
  }

  // The Bambu tag against the spool typed in, by the verdict every other link
  // uses. First, because a wrong spool is wrong whether it is bound or not;
  // the question about its tag follows from B once the difference is accepted.
  if (is_bambu && strlen(g_tag.tray_uuid) == 32) {
    s_warn_b_verdict = tagSpoolCompareTag(doc.as<JsonObjectConst>());
    if (s_warn_b_verdict.any()) {
      char why[32];
      tagSpoolVerdictText(s_warn_b_verdict, why, sizeof(why));
      logSDf("Link by id: spool %d does not match the tag (%s)", entered_id, why);
      s_warn_b_then_a = ask_a;
      s_warn_b_a_tag  = a_tag;
      s_warn_b_a_add  = a_add;
      showWarnPopupB(entered_id, is_bambu);
      return;
    }
  }
  if (ask_a) {
    showWarnPopupA(entered_id, a_tag.c_str(), is_bambu, "", a_add);
    return;
  }
  doLinkPatch(entered_id, is_bambu);
}

// ============================================================
//  LINK-FLOW: ZIFFERNBLOCK (Pfad 1 - ID eingeben)
// ============================================================
void showIdInputPopup(bool is_bambu, bool is_copy) {
  logSDf("SHOW: IdInputPopup bambu=%d copy=%d", (int)is_bambu, (int)is_copy);
  id_input_open = true;  // suppress NFC Spoolman query while numpad open
  releaseScreen(&scr_link_id);
  lbl_link_id_display = nullptr;
  lbl_link_id_status  = nullptr;

  scr_link_id = buildLinkOverlay();

  // ── Header like settings menu ───────────────────────────
  // Title zentriert
  lv_obj_t *lbl_title = lv_label_create(scr_link_id);
  { char tb[40]; backendText(T(STR_LINK_ID_TITLE), tb, sizeof(tb)); lv_label_set_text(lbl_title, tb); }
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_18, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 12);

  // Zurueck-Button (←) oben links
  lv_obj_t *btn_back = lv_btn_create(scr_link_id);
  lv_obj_set_size(btn_back, 44, 44);
  lv_obj_set_pos(btn_back, 4, 2);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_set_style_border_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, [](lv_event_t *e) {
    logSD("BTN: IdInput -> Back (flag)");
    show_id_input_pending = false;  // cancel any pending re-open
    // Use flag pattern - cannot delete own parent screen in callback
    if (id_popup_is_copy) {
      // Close and show copy entry - deferred via loop
      if (scr_link_id) { lv_obj_add_flag(scr_link_id, LV_OBJ_FLAG_HIDDEN); }
      if (scr_copy_entry) lv_obj_clear_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN);
      // Delete scr_link_id safely after callback via pending flag
      show_id_input_pending = true;  // reuse flag to signal cleanup
    } else {
      if (scr_link_id) { lv_obj_add_flag(scr_link_id, LV_OBJ_FLAG_HIDDEN); }
      if (scr_link_entry) lv_obj_clear_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
      show_id_input_pending = true;
    }
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_bk = lv_label_create(btn_back);
  lv_label_set_text(lbl_bk, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lbl_bk, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_bk, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_bk);

  // X-Button oben rechts → komplett schliessen
  lv_obj_t *btn_x = lv_btn_create(scr_link_id);
  lv_obj_set_size(btn_x, 44, 44);
  lv_obj_align(btn_x, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_x, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_x, 8, 0);
  lv_obj_set_style_shadow_width(btn_x, 0, 0);
  lv_obj_set_style_border_width(btn_x, 0, 0);
  lv_obj_add_event_cb(btn_x, [](lv_event_t *e) {
    logSD("BTN: IdInput -> X Close");
    // Flag pattern: cannot delete own parent screen from callback
    id_input_open = false;
    link_id_lookup_pending = 0;
    copy_id_lookup_pending = 0;
    show_id_input_pending = true;  // loop will delete scr_link_id safely
    // Also mark entry popup for deletion
    if (id_popup_is_copy) {
      if (scr_copy_entry) lv_obj_add_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN);
    } else {
      if (scr_link_entry) lv_obj_add_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
    }
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_x = lv_label_create(btn_x);
  lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(lbl_x, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_x);

  // Separator line
  lv_obj_t *div = lv_obj_create(scr_link_id);
  lv_obj_set_size(div, 472, 1); lv_obj_set_pos(div, 4, 48);
  lv_obj_set_style_bg_color(div, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  // Kontext-Info
  lv_obj_t *lbl_ctx = lv_label_create(scr_link_id);
  char ctx_buf[48];
  if (is_bambu) {
    snprintf(ctx_buf, sizeof(ctx_buf), "Bambu  %s", g_tag.material[0] ? g_tag.material : "Tag");
  } else {
    snprintf(ctx_buf, sizeof(ctx_buf), "UID: %.14s", link_tag_uid);
  }
  lv_label_set_text(lbl_ctx, ctx_buf);
  lv_obj_set_style_text_color(lbl_ctx, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(lbl_ctx, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_ctx, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_ctx, LV_ALIGN_TOP_MID, 0, 56);

  // Input field - kompakter, y=76
  lv_obj_t *input_box = lv_obj_create(scr_link_id);
  lv_obj_set_size(input_box, 260, 44);
  lv_obj_align(input_box, LV_ALIGN_TOP_MID, 0, 76);
  lv_obj_set_style_bg_color(input_box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(input_box, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_border_width(input_box, 1, 0);
  lv_obj_set_style_radius(input_box, 6, 0);
  lv_obj_set_style_pad_all(input_box, 0, 0);
  lv_obj_clear_flag(input_box, LV_OBJ_FLAG_SCROLLABLE);

  lbl_link_id_display = lv_label_create(input_box);
  lv_label_set_text(lbl_link_id_display, link_id_input[0] ? link_id_input : "_");
  lv_obj_set_style_text_color(lbl_link_id_display, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_link_id_display, &lv_font_montserrat_ext_24, 0);
  lv_obj_set_style_text_align(lbl_link_id_display, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl_link_id_display);

  // Status label inside input box (replaces digit display when error occurs)
  lbl_link_id_status = lv_label_create(input_box);
  lv_label_set_text(lbl_link_id_status, "");
  lv_obj_set_style_text_color(lbl_link_id_status, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_link_id_status, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_align(lbl_link_id_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_link_id_status, LV_ALIGN_BOTTOM_MID, 0, -2);

  // ── Numeric keypad: 4x3, START_Y=132 ─────────────────────
  // BTN 80x38, GAP 5 → 4 rows: 4*38+3*5=167px → ends at 132+167=299 ✓
  // No separate cancel button needed (X top right)
  const int BTN_W = 80, BTN_H = 38, GAP = 5;
  const int PAD_X = (480 - 3*BTN_W - 2*GAP) / 2;
  const int START_Y = 132;

  // 12 buttons: 1-9, then 0 / ⌫ / ✓
  const char* digits12[] = {"1","2","3","4","5","6","7","8","9","0",LV_SYMBOL_BACKSPACE,LV_SYMBOL_OK};
  int pos_x12[] = {0,1,2, 0,1,2, 0,1,2, 0,1,2};
  int pos_y12[] = {0,0,0, 1,1,1, 2,2,2, 3,3,3};

  id_popup_is_bambu = is_bambu;
  id_popup_is_copy  = is_copy;

  for (int d = 0; d < 12; d++) {
    lv_obj_t *btn = lv_btn_create(scr_link_id);
    int bx = PAD_X + pos_x12[d] * (BTN_W + GAP);
    int by = START_Y + pos_y12[d] * (BTN_H + GAP);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_pos(btn, bx, by);

    bool is_ok        = (d == 11);
    bool is_backspace = (d == 10);
    uint32_t bg_col = is_ok ? UI_COL_GO_BG : UI_COL_ROW;
    uint32_t bg_pr  = is_ok ? UI_COL_GO_BG_PRESSED : UI_COL_PRESS_FILL;
    uint32_t bd_col = is_ok ? UI_COL_GO_BG_PRESSED : UI_COL_LINE;
    uint32_t tx_col = is_ok ? UI_COL_OK_TEXT_2 : (is_backspace ? UI_COL_WARN : UI_COL_INK);

    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_col), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_pr), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(bd_col), 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, digits12[d]);
    lv_obj_set_style_text_color(lbl, lv_color_hex(tx_col), 0);
    lv_obj_set_style_text_font(lbl, is_ok ? &lv_font_montserrat_ext_20 : &lv_font_montserrat_ext_18, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
      const char* digit_str = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
      bool is_bs     = (strcmp(digit_str, LV_SYMBOL_BACKSPACE) == 0);
      bool is_ok_btn = (strcmp(digit_str, LV_SYMBOL_OK) == 0);

      if (is_ok_btn) {
        if (strlen(link_id_input) == 0) {
          if (lbl_link_id_status) { char tb[40]; backendText(T(STR_LINK_ID_TITLE), tb, sizeof(tb)); lv_label_set_text(lbl_link_id_status, tb); }
          return;
        }
        int entered_id = atoi(link_id_input);
        if (entered_id <= 0) {
          if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_ID_NOT_FOUND));
          return;
        }
        if (id_popup_is_copy) {
          // Defer to loop - HTTP + JSON in lambda causes stack overflow
          if (!wifi_ok) { if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_NO_WIFI)); return; }
          copy_id_lookup_pending = entered_id;
          if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_CHECKING));
        } else {
          // Defer to loop - direct call causes stack overflow in LVGL lambda
          link_id_lookup_pending = entered_id;
          link_id_lookup_is_bambu = id_popup_is_bambu;
          if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_CHECKING));
        }
      } else if (is_bs) {
        int len = strlen(link_id_input);
        if (len > 0) link_id_input[len-1] = '\0';
        if (lbl_link_id_display)
          lv_label_set_text(lbl_link_id_display, link_id_input[0] ? link_id_input : "_");
        if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, "");
      } else {
        int len = strlen(link_id_input);
        if (len < 6) { link_id_input[len] = digit_str[0]; link_id_input[len+1] = '\0'; }
        if (lbl_link_id_display)
          lv_label_set_text(lbl_link_id_display, link_id_input[0] ? link_id_input : "_");
        if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, "");
      }
    }, LV_EVENT_CLICKED, NULL);
  }
}

void closeIdInputPopup() {
  id_input_open = false;
  link_id_lookup_pending = 0;  // cancel any pending lookup when popup closes
  copy_id_lookup_pending = 0;
  releaseScreen(&scr_link_id);
  lbl_link_id_display = nullptr;
  lbl_link_id_status  = nullptr;
}
