#include "spool_flow.h"
#include "ui_common.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <cstring>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "ui/spool_flow_internal.h"
#include "ui/theme.h"
#include "services/backend.h"
#include "services/breadcrumb.h"

// The way into the link flow: the entry popup, and the vendor and
// material lists in front of the spool list.
// Moved out of spool_flow.cpp word for word; what it shares with the rest
// of the link flow is declared in spool_flow_internal.h.

// Upper bound of the deduplicated group lists below, and the only bound they
// have. They are fixed size arrays, so a loop guarded by spool_list_limit
// alone wrote past the end as soon as a library had more than 20 vendors or
// materials - which is what this constant was introduced for.
//
// spool_list_limit is deliberately NOT checked here as well. It is the page
// size of the spool list, and a vendor picker cut down to it made a library
// with 13 vendors show 5 of them because someone had set the spool list to 5.
// These rows are cheap anyway: 716 bytes each measured on hardware, so all 20
// cost 14 kB against the 56 kB free when the picker opens.
#define LINK_GROUP_MAX 20

// ============================================================
//  LINK-FLOW: FLOW B PFAD 2 - MATERIAL-AUSWAHL (Stufe 2)
// ============================================================
void showMaterialList(const char* vendor_name) {
  logSDf("SHOW: MaterialList vendor=%s", vendor_name);
  releaseScreen(&scr_link_mat);
  strncpy(link_selected_vendor, vendor_name, sizeof(link_selected_vendor)-1);
  link_selected_vendor[sizeof(link_selected_vendor)-1] = '\0';
  link_selected_material_full[0] = 0;  // reset on entry - set fresh in stage 3
  link_stage3_shown = false;

  scr_link_mat = buildLinkOverlay();

  char title_buf[48], v16[17];
  utf8Cut(vendor_name, 16, v16, sizeof(v16));
  snprintf(title_buf, sizeof(title_buf), "%s | %s", T(STR_MAT_TITLE), v16);

  // Header with Back + Cancel
  lv_obj_t *hdr_mat = lv_obj_create(scr_link_mat);
  lv_obj_set_size(hdr_mat, 480, 52); lv_obj_set_pos(hdr_mat, 0, 0);
  lv_obj_set_style_bg_color(hdr_mat, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(hdr_mat, 0, 0);
  lv_obj_set_style_pad_all(hdr_mat, 0, 0);
  lv_obj_set_style_radius(hdr_mat, 0, 0);
  lv_obj_clear_flag(hdr_mat, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_title = lv_label_create(hdr_mat);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);
  lv_obj_t *btn_mat_back = lv_btn_create(hdr_mat);
  lv_obj_set_size(btn_mat_back, 44, 44); lv_obj_set_pos(btn_mat_back, 4, 4);
  lv_obj_set_style_bg_color(btn_mat_back, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(btn_mat_back, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_mat_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_mat_back, 0, 0);
  lv_obj_set_style_border_width(btn_mat_back, 0, 0);
  lv_obj_add_event_cb(btn_mat_back, [](lv_event_t *e) {
    logSD("BTN: MatList -> Back");
    link_nav_pending = LNAV_MAT_BACK;
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_mat_back); lv_label_set_text(l, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0); lv_obj_center(l); }
  lv_obj_t *btn_mat_cancel = lv_btn_create(hdr_mat);
  lv_obj_set_size(btn_mat_cancel, 44, 44);
  lv_obj_align(btn_mat_cancel, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_mat_cancel, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_mat_cancel, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_mat_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_mat_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_mat_cancel, 0, 0);
  lv_obj_add_event_cb(btn_mat_cancel, [](lv_event_t *e) {
    logSD("BTN: MatList -> Cancel");
    copy_flow_via_list = false;
    releaseScreen(&scr_link_mat);
    releaseScreen(&scr_link_mat_sub);
    releaseScreen(&scr_link_vendor);
    releaseScreen(&scr_link_entry);
    releaseScreen(&scr_copy_entry);
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_mat_cancel); lv_label_set_text(l, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0); lv_obj_center(l); }
  lv_obj_t *div = lv_obj_create(scr_link_mat);
  lv_obj_set_size(div, 480, 1); lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t *list = lv_obj_create(scr_link_mat);
  lv_obj_set_size(list, 460, 264);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  logLvMem("matlist/pre", 0);

  // Deduplicate material prefixes (3 chars) for the selected vendor
  static char seen_mats[LINK_GROUP_MAX][4] = {};
  static int  mat_counts[LINK_GROUP_MAX]   = {};
  static int  seen_count       = 0;
  seen_count = 0;
  memset(seen_mats, 0, sizeof(seen_mats));
  memset(mat_counts, 0, sizeof(mat_counts));

  bool mat_limit_hit = false;
  for (int i = 0; i < link_spool_count; i++) {
    UnlinkedSpool &s = link_spools[i];
    if (linkSpoolSkip(s)) continue;
    if (strncasecmp(s.vendor, vendor_name, strlen(vendor_name)) != 0) continue;
    if (!s.material[0]) continue;
    char prefix[4]; strncpy(prefix, s.material, 3); prefix[3] = '\0';
    bool found = false;
    for (int j = 0; j < seen_count; j++) {
      if (strncasecmp(seen_mats[j], prefix, 3) == 0) { mat_counts[j]++; found = true; break; }
    }
    if (!found) {
      if (seen_count >= LINK_GROUP_MAX) { mat_limit_hit = true; continue; }
      strncpy(seen_mats[seen_count], prefix, 3);
      mat_counts[seen_count] = 1;
      seen_count++;
    }
  }

  for (int m = 0; m < seen_count; m++) {
    // A row is five objects. LVGL 8.3 answers an exhausted pool with NULL and
    // asserts nothing, and every widget constructor writes through that pointer
    // one line later - so running out here is a panic reboot, not the freeze the
    // assert handler suggests. Asked before the row rather than after each of its
    // objects, because the pool ran out between the second and the third.
    if (!lvPoolHasRoomForRow()) {
      logSDf("MaterialList: LVGL pool low, list cut at %d rows", m);
      break;
    }
    lv_obj_t *row = lv_btn_create(list);
    if (!row) { logSDf("MaterialList: no room for a row, list cut at %d", m); break; }
    lv_obj_set_size(row, 452, 50);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COL_LINE_SOFT), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *lbl_mat = lv_label_create(row);
    lv_label_set_text(lbl_mat, seen_mats[m]);
    lv_obj_set_style_text_color(lbl_mat, lv_color_hex(UI_COL_WARN), 0);
    lv_obj_set_style_text_font(lbl_mat, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl_mat, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *lbl_cnt = lv_label_create(row);
    char cnt_buf[12]; snprintf(cnt_buf, sizeof(cnt_buf), "%d x", mat_counts[m]);
    lv_label_set_text(lbl_cnt, cnt_buf);
    lv_obj_set_style_text_color(lbl_cnt, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_add_event_cb(row, [](lv_event_t *e) {
      int idx = (intptr_t)lv_event_get_user_data(e);
      strncpy(link_selected_material, seen_mats[idx], sizeof(link_selected_material)-1);
      link_selected_material[sizeof(link_selected_material)-1] = '\0';
      link_selected_material_full[0] = 0;  // reset for new branch
      link_nav_pending = LNAV_MAT_ROW;
    }, LV_EVENT_CLICKED, (void*)(intptr_t)m);
  }

  logLvMem("matlist/post", seen_count);
  if (seen_count == 0) {
    lv_obj_t *lbl_empty = lv_label_create(scr_link_mat);
    lv_label_set_text(lbl_empty, T(STR_NO_MATERIALS));
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(UI_COL_WARN), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (mat_limit_hit) {
    addListMoreInfo(list, STR_LIST_MORE_MATS);
  }

}

// ============================================================
//  LINK-FLOW: FLOW B PFAD 2 - MATERIAL-VOLLNAME-AUSWAHL (Stufe 3)
//  Dedupliziert s.material exakt fuer Vendor + Material-Prefix.
//  Bei nur einem Eintrag: direkt zu Stufe 4 (auto-skip).
// ============================================================
void showMaterialSubList(const char* vendor_name, const char* material_prefix) {
  logSDf("SHOW: MaterialSubList vendor=%s mat=%s", vendor_name, material_prefix);

  // First pass: collect unique full material names + counts
  static char seen_full[LINK_GROUP_MAX][32] = {};
  static int  full_counts[LINK_GROUP_MAX]   = {};
  static int  full_seen_count   = 0;
  full_seen_count = 0;
  memset(seen_full, 0, sizeof(seen_full));
  memset(full_counts, 0, sizeof(full_counts));

  bool full_limit_hit = false;
  for (int i = 0; i < link_spool_count; i++) {
    UnlinkedSpool &s = link_spools[i];
    if (linkSpoolSkip(s)) continue;
    if (strncasecmp(s.vendor, vendor_name, strlen(vendor_name)) != 0) continue;
    if (!s.material[0]) continue;
    if (strncasecmp(s.material, material_prefix, strlen(material_prefix)) != 0) continue;

    bool found = false;
    for (int j = 0; j < full_seen_count; j++) {
      if (strcasecmp(seen_full[j], s.material) == 0) { full_counts[j]++; found = true; break; }
    }
    if (!found) {
      if (full_seen_count >= LINK_GROUP_MAX) { full_limit_hit = true; continue; }
      strncpy(seen_full[full_seen_count], s.material, sizeof(seen_full[0])-1);
      full_counts[full_seen_count] = 1;
      full_seen_count++;
    }
  }

  // Auto-skip stage 3 when only one full name found - go directly to stage 4
  if (full_seen_count == 1 && !full_limit_hit) {
    logSDf("MaterialSubList auto-skip: only %s", seen_full[0]);
    strncpy(link_selected_material_full, seen_full[0], sizeof(link_selected_material_full)-1);
    link_selected_material_full[sizeof(link_selected_material_full)-1] = '\0';
    link_stage3_shown = false;  // not actually rendered - back from stage 4 must skip stage 3
    showFilteredSpoolList(vendor_name, material_prefix, link_selected_material_full);
    return;
  }

  link_stage3_shown = true;  // actually rendered
  releaseScreen(&scr_link_mat_sub);
  scr_link_mat_sub = buildLinkOverlay();

  char title_buf[48], v16[17];
  utf8Cut(vendor_name, 16, v16, sizeof(v16));
  snprintf(title_buf, sizeof(title_buf), "%s | %.4s", v16, material_prefix);

  // Header with Back + Cancel
  lv_obj_t *hdr_ms = lv_obj_create(scr_link_mat_sub);
  lv_obj_set_size(hdr_ms, 480, 52); lv_obj_set_pos(hdr_ms, 0, 0);
  lv_obj_set_style_bg_color(hdr_ms, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(hdr_ms, 0, 0);
  lv_obj_set_style_pad_all(hdr_ms, 0, 0);
  lv_obj_set_style_radius(hdr_ms, 0, 0);
  lv_obj_clear_flag(hdr_ms, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_title = lv_label_create(hdr_ms);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *btn_ms_back = lv_btn_create(hdr_ms);
  lv_obj_set_size(btn_ms_back, 44, 44); lv_obj_set_pos(btn_ms_back, 4, 4);
  lv_obj_set_style_bg_color(btn_ms_back, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(btn_ms_back, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_ms_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_ms_back, 0, 0);
  lv_obj_set_style_border_width(btn_ms_back, 0, 0);
  lv_obj_add_event_cb(btn_ms_back, [](lv_event_t *e) {
    logSD("BTN: MatSubList -> Back");
    link_nav_pending = LNAV_MATSUB_BACK;
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_ms_back); lv_label_set_text(l, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0); lv_obj_center(l); }

  lv_obj_t *btn_ms_cancel = lv_btn_create(hdr_ms);
  lv_obj_set_size(btn_ms_cancel, 44, 44);
  lv_obj_align(btn_ms_cancel, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_ms_cancel, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_ms_cancel, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_ms_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_ms_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_ms_cancel, 0, 0);
  lv_obj_add_event_cb(btn_ms_cancel, [](lv_event_t *e) {
    logSD("BTN: MatSubList -> Cancel");
    copy_flow_via_list = false;
    releaseScreen(&scr_link_mat_sub);
    releaseScreen(&scr_link_mat);
    releaseScreen(&scr_link_vendor);
    releaseScreen(&scr_link_entry);
    releaseScreen(&scr_copy_entry);
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_ms_cancel); lv_label_set_text(l, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0); lv_obj_center(l); }

  lv_obj_t *div = lv_obj_create(scr_link_mat_sub);
  lv_obj_set_size(div, 480, 1); lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t *list = lv_obj_create(scr_link_mat_sub);
  lv_obj_set_size(list, 460, 264);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  logLvMem("matsublist/pre", 0);

  for (int m = 0; m < full_seen_count; m++) {
    // A row is five objects. LVGL 8.3 answers an exhausted pool with NULL and
    // asserts nothing, and every widget constructor writes through that pointer
    // one line later - so running out here is a panic reboot, not the freeze the
    // assert handler suggests. Asked before the row rather than after each of its
    // objects, because the pool ran out between the second and the third.
    if (!lvPoolHasRoomForRow()) {
      logSDf("MaterialSubList: LVGL pool low, list cut at %d rows", m);
      break;
    }
    lv_obj_t *row = lv_btn_create(list);
    if (!row) { logSDf("MaterialSubList: no room for a row, list cut at %d", m); break; }
    lv_obj_set_size(row, 452, 50);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COL_LINE_SOFT), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *lbl_full = lv_label_create(row);
    lv_label_set_text(lbl_full, seen_full[m]);
    lv_obj_set_style_text_color(lbl_full, lv_color_hex(UI_COL_WARN), 0);
    lv_obj_set_style_text_font(lbl_full, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl_full, LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_long_mode(lbl_full, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_full, 340);

    lv_obj_t *lbl_cnt = lv_label_create(row);
    char cnt_buf[12]; snprintf(cnt_buf, sizeof(cnt_buf), "%d x", full_counts[m]);
    lv_label_set_text(lbl_cnt, cnt_buf);
    lv_obj_set_style_text_color(lbl_cnt, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_add_event_cb(row, [](lv_event_t *e) {
      int idx = (intptr_t)lv_event_get_user_data(e);
      strncpy(link_selected_material_full, seen_full[idx], sizeof(link_selected_material_full)-1);
      link_selected_material_full[sizeof(link_selected_material_full)-1] = '\0';
      link_nav_pending = LNAV_MATSUB_ROW;
    }, LV_EVENT_CLICKED, (void*)(intptr_t)m);
  }

  logLvMem("matsublist/post", full_seen_count);
  if (full_seen_count == 0) {
    lv_obj_t *lbl_empty = lv_label_create(scr_link_mat_sub);
    lv_label_set_text(lbl_empty, T(STR_NO_MATERIALS));
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(UI_COL_WARN), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (full_limit_hit) {
    addListMoreInfo(list, STR_LIST_MORE_MATS);
  }
}

// ============================================================
//  LINK-FLOW: FLOW B PFAD 2 - HERSTELLER-AUSWAHL (Stufe 1)
// ============================================================
void showVendorList() {
  crumbSet("vendor list build");
  logSD("SHOW: VendorList");
  releaseScreen(&scr_link_vendor);

  scr_link_vendor = buildLinkOverlay();

  // Zaehle Spulen gesamt (ohne bereits verknuepft)
  int total_unlinked = 0;
  for (int i = 0; i < link_spool_count; i++) {
    if (linkSpoolSkip(link_spools[i])) continue;
    total_unlinked++;
  }

  char title_buf[40];
  snprintf(title_buf, sizeof(title_buf), T(STR_VENDOR_TITLE), total_unlinked);

  lv_obj_t *hdr_vnd = lv_obj_create(scr_link_vendor);
  lv_obj_set_size(hdr_vnd, 480, 52); lv_obj_set_pos(hdr_vnd, 0, 0);
  lv_obj_set_style_bg_color(hdr_vnd, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(hdr_vnd, 0, 0);
  lv_obj_set_style_pad_all(hdr_vnd, 0, 0);
  lv_obj_set_style_radius(hdr_vnd, 0, 0);
  lv_obj_clear_flag(hdr_vnd, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl_title = lv_label_create(hdr_vnd);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);
  // Back: go back to entry popup
  lv_obj_t *btn_vnd_back = lv_btn_create(hdr_vnd);
  lv_obj_set_size(btn_vnd_back, 44, 44); lv_obj_set_pos(btn_vnd_back, 4, 4);
  lv_obj_set_style_bg_color(btn_vnd_back, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(btn_vnd_back, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_vnd_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_vnd_back, 0, 0);
  lv_obj_set_style_border_width(btn_vnd_back, 0, 0);
  lv_obj_add_event_cb(btn_vnd_back, [](lv_event_t *e) {
    logSD("BTN: VendorList -> Back");
    releaseScreen(&scr_link_vendor);
    if (scr_link_entry)  lv_obj_clear_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
    if (scr_copy_entry)  lv_obj_clear_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_vnd_back); lv_label_set_text(l, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0); lv_obj_center(l); }
  lv_obj_t *btn_vnd_x = lv_btn_create(hdr_vnd);
  lv_obj_set_size(btn_vnd_x, 44, 44);
  lv_obj_align(btn_vnd_x, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_vnd_x, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_vnd_x, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_vnd_x, 8, 0);
  lv_obj_set_style_shadow_width(btn_vnd_x, 0, 0);
  lv_obj_set_style_border_width(btn_vnd_x, 0, 0);
  lv_obj_add_event_cb(btn_vnd_x, [](lv_event_t *e) {
    logSD("BTN: VendorList -> Cancel");
    copy_flow_via_list = false;
    releaseScreen(&scr_link_vendor);
    releaseScreen(&scr_link_entry);
    releaseScreen(&scr_copy_entry);
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_vnd_x); lv_label_set_text(l, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0); lv_obj_center(l); }
  lv_obj_t *div = lv_obj_create(scr_link_vendor);
  lv_obj_set_size(div, 480, 1); lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  lv_obj_t *list = lv_obj_create(scr_link_vendor);
  lv_obj_set_size(list, 460, 264);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  logLvMem("vendorlist/pre", 0);

  // Dedupliziere Vendors
  static char seen_vendors[LINK_GROUP_MAX][32] = {};
  static int  vendor_counts[LINK_GROUP_MAX]    = {};
  static int  seen_v               = 0;
  seen_v = 0;
  memset(seen_vendors, 0, sizeof(seen_vendors));
  memset(vendor_counts, 0, sizeof(vendor_counts));

  bool vendor_limit_hit = false;
  for (int i = 0; i < link_spool_count; i++) {
    UnlinkedSpool &s = link_spools[i];
    if (linkSpoolSkip(s)) continue;
    const char* vn = s.vendor[0] ? s.vendor : T(STR_STATUS_UNKNOWN);
    bool found = false;
    for (int j = 0; j < seen_v; j++) {
      if (strcasecmp(seen_vendors[j], vn) == 0) { vendor_counts[j]++; found = true; break; }
    }
    if (!found) {
      if (seen_v >= LINK_GROUP_MAX) { vendor_limit_hit = true; continue; }
      utf8Cut(vn, 31, seen_vendors[seen_v], sizeof(seen_vendors[seen_v]));
      vendor_counts[seen_v] = 1;
      seen_v++;
    }
  }

  for (int v = 0; v < seen_v; v++) {
    // A row is five objects. LVGL 8.3 answers an exhausted pool with NULL and
    // asserts nothing, and every widget constructor writes through that pointer
    // one line later - so running out here is a panic reboot, not the freeze the
    // assert handler suggests. Asked before the row rather than after each of its
    // objects, because the pool ran out between the second and the third.
    if (!lvPoolHasRoomForRow()) {
      logSDf("VendorList: LVGL pool low, list cut at %d rows", v);
      break;
    }
    lv_obj_t *row = lv_btn_create(list);
    if (!row) { logSDf("VendorList: no room for a row, list cut at %d", v); break; }
    lv_obj_set_size(row, 452, 50);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COL_LINE_SOFT), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *lbl_vnd = lv_label_create(row);
    lv_label_set_text(lbl_vnd, seen_vendors[v]);
    lv_obj_set_style_text_color(lbl_vnd, lv_color_hex(UI_COL_INK), 0);
    lv_obj_set_style_text_font(lbl_vnd, &lv_font_montserrat_ext_18, 0);
    lv_obj_align(lbl_vnd, LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_long_mode(lbl_vnd, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_vnd, 320);

    lv_obj_t *lbl_cnt = lv_label_create(row);
    char cnt_buf[12]; snprintf(cnt_buf, sizeof(cnt_buf), "%d x", vendor_counts[v]);
    lv_label_set_text(lbl_cnt, cnt_buf);
    lv_obj_set_style_text_color(lbl_cnt, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_add_event_cb(row, [](lv_event_t *e) {
      int idx = (intptr_t)lv_event_get_user_data(e);
      if (idx < 0 || idx >= LINK_GROUP_MAX) return;
      strncpy(link_nav_vendor, seen_vendors[idx], sizeof(link_nav_vendor) - 1);
      link_nav_vendor[sizeof(link_nav_vendor) - 1] = '\0';
      link_nav_pending = LNAV_VENDOR_ROW;
    }, LV_EVENT_CLICKED, (void*)(intptr_t)v);
  }

  logLvMem("vendorlist/post", seen_v);
  if (seen_v == 0) {
    lv_obj_t *lbl_empty = lv_label_create(scr_link_vendor);
    { char eb[80]; backendText(T(STR_NO_VENDORS), eb, sizeof(eb)); lv_label_set_text(lbl_empty, eb); }
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(UI_COL_WARN), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (vendor_limit_hit) {
    addListMoreInfo(list, STR_LIST_MORE_VENDORS);
  }

}

// ============================================================
//  LINK-FLOW: EINSTIEGS-POPUP (Flow A + B)
// ============================================================
void closeLinkEntryPopup() {
  releaseScreen(&scr_link_entry);
}

// The X of a flow screen: top right, 44 px, red, and always "out to the main
// screen". The list screens carry it in their header; the two entry screens
// have no header and get it through this, beside their Cancel at the bottom.
void flowCloseButton(lv_obj_t* parent, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 44, 44);
  lv_obj_set_pos(btn, 480 - 4 - 44, 4);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(l, UI_FONT_TITLE, 0);
  lv_obj_center(l);
}

void showLinkEntryPopup(bool is_bambu) {
  logSDf("SHOW: LinkEntryPopup bambu=%d", (int)is_bambu);
  link_selected_material[0] = 0;  // reset material selection for each new flow
  link_selected_material_full[0] = 0;
  link_stage3_shown = false;
  closeLinkEntryPopup();
  link_flow_is_bambu = is_bambu;
  link_id_input[0]   = '\0';
  // A copy through the picker left by some way that does not reset it must
  // not turn this link into a copy.
  copy_flow_via_list = false;

  scr_link_entry = buildLinkOverlay();

  // Header-Titel
  lv_obj_t *lbl_title = lv_label_create(scr_link_entry);
  lv_label_set_text(lbl_title, is_bambu ? T(STR_LINK_BAMBU_TITLE) : T(STR_LINK_NTAG_TITLE));
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 22);

  // The same as Cancel below.
  flowCloseButton(scr_link_entry, [](lv_event_t *e) {
    logSD("BTN: LinkEntry -> X");
    link_popup_dismissed = true;
    closeLinkEntryPopup();
  });

  // Separator line
  lv_obj_t *div = lv_obj_create(scr_link_entry);
  lv_obj_set_size(div, 472, 1); lv_obj_set_pos(div, 4, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  // Kontext-Info (Material/UID)
  lv_obj_t *lbl_ctx = lv_label_create(scr_link_entry);
  char ctx_buf[56];
  if (is_bambu) {
    char fmt_c[48]; backendText(T(STR_LINK_CTX_NOT_IN_SM), fmt_c, sizeof(fmt_c));
    snprintf(ctx_buf, sizeof(ctx_buf), fmt_c,
      g_tag.material[0] ? g_tag.material : "Bambu Tag");
  } else {
    snprintf(ctx_buf, sizeof(ctx_buf), "UID: %s", link_tag_uid);
  }
  lv_label_set_text(lbl_ctx, ctx_buf);
  lv_obj_set_style_text_color(lbl_ctx, lv_color_hex(UI_COL_CAPTION), 0);
  lv_obj_set_style_text_font(lbl_ctx, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_ctx, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_ctx, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_ctx, 450);
  lv_obj_align(lbl_ctx, LV_ALIGN_TOP_MID, 0, 62);

  // Button-Layout: 3 Buttons zentriert, je 380x60
  const int BTN_W = 380, BTN_H = 60, BTN_GAP = 10;
  const int Y1 = 100, Y2 = Y1 + BTN_H + BTN_GAP, Y3 = Y2 + BTN_H + BTN_GAP;

  // Button 1: Spool-ID eingeben
  lv_obj_t *btn1 = lv_btn_create(scr_link_entry);
  lv_obj_set_size(btn1, BTN_W, BTN_H);
  lv_obj_align(btn1, LV_ALIGN_TOP_MID, 0, Y1);
  lv_obj_set_style_bg_color(btn1, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn1, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn1, 10, 0);
  lv_obj_set_style_shadow_width(btn1, 0, 0);
  lv_obj_set_style_border_width(btn1, 1, 0);
  lv_obj_set_style_border_color(btn1, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_add_event_cb(btn1, [](lv_event_t *e) {
    link_id_input[0] = '\0';
    showIdInputPopup(link_flow_is_bambu);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l1 = lv_label_create(btn1);
  lv_label_set_text(l1, T(STR_BTN_ENTER_ID));
  lv_obj_set_style_text_color(l1, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(l1, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(l1);

  // Button 2: Aus Liste waehlen
  lv_obj_t *btn2 = lv_btn_create(scr_link_entry);
  lv_obj_set_size(btn2, BTN_W, BTN_H);
  lv_obj_align(btn2, LV_ALIGN_TOP_MID, 0, Y2);
  lv_obj_set_style_bg_color(btn2, lv_color_hex(UI_COL_ROW), 0);
  lv_obj_set_style_bg_color(btn2, lv_color_hex(UI_COL_ROW_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn2, 10, 0);
  lv_obj_set_style_shadow_width(btn2, 0, 0);
  lv_obj_set_style_border_width(btn2, 1, 0);
  lv_obj_set_style_border_color(btn2, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_add_event_cb(btn2, [](lv_event_t *e) {
    // The inventory fetch takes seconds; it runs from the loop, then the
    // list or the vendor picker opens.
    link_list_fetch_pending = true;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l2 = lv_label_create(btn2);
  lv_label_set_text(l2, T(STR_BTN_FROM_LIST));
  lv_obj_set_style_text_color(l2, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(l2, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(l2);

  // Button 3: Abbrechen
  lv_obj_t *btn3 = lv_btn_create(scr_link_entry);
  lv_obj_set_size(btn3, BTN_W, BTN_H - 14);  // etwas kleiner
  lv_obj_align(btn3, LV_ALIGN_TOP_MID, 0, Y3);
  lv_obj_set_style_bg_color(btn3, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn3, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn3, 10, 0);
  lv_obj_set_style_shadow_width(btn3, 0, 0);
  lv_obj_set_style_border_width(btn3, 0, 0);
  lv_obj_add_event_cb(btn3, [](lv_event_t *e) {
    link_popup_dismissed = true;
    closeLinkEntryPopup();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l3 = lv_label_create(btn3);
  lv_label_set_text(l3, T(STR_CANCEL));
  lv_obj_set_style_text_color(l3, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(l3, &lv_font_montserrat_ext_16, 0);
  lv_obj_center(l3);
}

// ============================================================
//  UI BAUEN - Redesign Beta_0.4.100
// Main screen construction lives in ui/main_screen.cpp.
