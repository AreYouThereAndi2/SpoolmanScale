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
#include "services/list_limits.h"
#include "services/spool_cache.h"
#include "ui/spool_flow_internal.h"
#include "ui/tag_spool_compare.h"
#include "ui/theme.h"
#include "services/breadcrumb.h"
#include "services/tag_spool_match.h"
#include "services/spool_extras.h"

// The spool list at the end of the picker, and the question in front
// of a link or a copy.
// Moved out of spool_flow.cpp word for word; what it shares with the rest
// of the link flow is declared in spool_flow_internal.h.

// ============================================================
//  LINK-FLOW: FLOW B PFAD 2 - SPULEN-LISTE (Stufe 3)
// ============================================================
// Helper: adds a non-clickable info row at the bottom of a list when limit was hit
void addListMoreInfo(lv_obj_t* list, StringID str_id) {
  char buf[96];
  copyT(buf, sizeof(buf), str_id);

  lv_obj_t *row = lv_obj_create(list);
  lv_obj_set_size(row, 452, 48);
  lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_AMBER_ROW), 0);
  lv_obj_set_style_radius(row, 6, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(UI_COL_AMBER_LINE), 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, buf);
  lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_ext_12, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl, 440);
  lv_obj_center(lbl);
}

// Whether a spool belongs in the filtered list. One function, because the
// count above the list and the rows in it used to decide separately and did
// not agree: the count skipped Bambu vendors in the non-Bambu flow while the
// rows kept them, so picking "Bambu Lab" from the vendor list produced a
// heading that said 0 over two rows. In the Bambu flow the count matched on
// material_prefix and the rows on g_tag.material, which drifted the same way.
static bool linkRowMatches(const UnlinkedSpool &s, const char* vendor_name,
                           const char* material_prefix, const char* material_full) {
  if (linkSpoolSkip(s)) return false;

  if (link_flow_is_bambu) {
    // The tag names its own vendor, so only Bambu spools can answer to it.
    if (strncasecmp(s.vendor, "Bambu", 5) != 0) return false;
    // The fetch already gave up on matching the material - applying the same
    // test again here would hide every row it just let through, and the list
    // would be empty despite the header counting them.
    if (link_material_ignored) return true;
    if (g_tag.material[0] && s.material[0]) {
      if (isSupportMaterial(g_tag.material)) {
        // Support tags: match Spoolman materials ending in "-S"
        if (!isSupportSpoolmanMat(s.material)) return false;
      } else {
        if (strncasecmp(s.material, g_tag.material, 3) != 0) return false;
        // Exclude support materials from non-support display
        if (isSupportSpoolmanMat(s.material)) return false;
      }
    }
    return true;
  }

  // Flow B: whatever the user picked in the vendor and material lists. Bambu
  // is a vendor like any other here - the tag is an NTAG and says nothing
  // about who made the filament.
  if (vendor_name[0] &&
      strncasecmp(s.vendor, vendor_name, strlen(vendor_name)) != 0) return false;
  if (material_prefix[0] &&
      strncasecmp(s.material, material_prefix, strlen(material_prefix)) != 0) return false;
  // Stage 3: full material name match (exact, case-insensitive)
  if (material_full && material_full[0] &&
      strcasecmp(s.material, material_full) != 0) return false;
  return true;
}

// LINK_LIST_H and LINK_STRIP_H stand in spool_flow_internal.h, the copy list
// has the same strip.
#define LINK_RELOAD_W      110
#define LINK_RELOAD_H       28

// The strip under a spool list that is kept in the cache: when it was loaded,
// how long ago, and a button that loads it again. For the link list and the
// copy list alike. `at` is 0 when the clock was not set at the time.
void listReloadStrip(lv_obj_t* scr, time_t at, uint32_t age_ms, lv_event_cb_t on_reload) {
  const int strip_y = 56 + LINK_LIST_H - LINK_STRIP_H;
  if (at) {
    struct tm ti;
    localtime_r(&at, &ti);
    // "<1" rather than "0": a list loaded seconds ago is not zero minutes old.
    char age[8];
    const unsigned long age_min = age_ms / 60000UL;
    if (age_min) snprintf(age, sizeof(age), "%lu", age_min);
    else         snprintf(age, sizeof(age), "<1");
    char as_of[48];
    snprintf(as_of, sizeof(as_of), T(STR_LIST_AS_OF), ti.tm_hour, ti.tm_min, age);
    lv_obj_t *lbl_as_of = lv_label_create(scr);
    lv_label_set_text(lbl_as_of, as_of);
    lv_obj_set_style_text_color(lbl_as_of, lv_color_hex(UI_COL_INK_SOFT), 0);
    lv_obj_set_style_text_font(lbl_as_of, UI_FONT_SMALL, 0);
    lv_obj_set_pos(lbl_as_of, 16, strip_y + (LINK_STRIP_H - 16) / 2);
  }

  lv_obj_t *btn_reload = lv_btn_create(scr);
  lv_obj_set_size(btn_reload, LINK_RELOAD_W, LINK_RELOAD_H);
  lv_obj_set_pos(btn_reload, 480 - 12 - LINK_RELOAD_W, strip_y + (LINK_STRIP_H - LINK_RELOAD_H) / 2);
  lv_obj_set_style_bg_color(btn_reload, lv_color_hex(UI_COL_ROW_PRESSED), 0);
  lv_obj_set_style_bg_color(btn_reload, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_reload, UI_RADIUS_INPUT, 0);
  lv_obj_set_style_shadow_width(btn_reload, 0, 0);
  lv_obj_set_style_border_width(btn_reload, 0, 0);
  lv_obj_add_event_cb(btn_reload, on_reload, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_reload = lv_label_create(btn_reload);
  lv_label_set_text(lbl_reload, T(STR_LIST_RELOAD));
  lv_obj_set_style_text_color(lbl_reload, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_reload, UI_FONT_SMALL, 0);
  lv_obj_center(lbl_reload);
}


// The question in front of a link or a copy: this spool, yes or no. Opened by
// a row of the spool list. A function of its own rather than the body of the
// row's callback, so that it can be opened from the loop as well.
// A row of the picker chosen in copy mode: straight to the copy confirmation.
// It used to open the link question first ("create new spool?") and the copy
// confirmation only behind its yes - two questions for one decision. The
// first one was a guard against a scroll that ended as a tap on a row, which
// the touch driver no longer turns into a click (beta.33). Flags only: this
// runs from a row's event callback.
static void linkCopyConfirmFromRow(int cidx) {
  if (!link_spools || cidx < 0 || cidx >= link_spool_count) return;
  UnlinkedSpool &cs = link_spools[cidx];
  logSDf("CopyConfirm via list: spool_id=%d fid=%d spw=%.0f", cs.id, cs.filament_id, cs.spool_weight);
  copy_confirm_fid = cs.filament_id;
  copy_confirm_spool_id = cs.id;
  copy_confirm_remaining = cs.remaining;
  copy_confirm_initial = cs.total;
  copy_confirm_spool_w = cs.spool_weight;
  {
    bool nm = (cs.material[0] && cs.name[0] &&
               strncasecmp(cs.name, cs.material, strlen(cs.material)) == 0);
    if (nm)
      snprintf(copy_confirm_name, sizeof(copy_confirm_name), "%s (%s)", cs.name, cs.vendor);
    else
      snprintf(copy_confirm_name, sizeof(copy_confirm_name), "%s %s (%s)", cs.material, cs.name, cs.vendor);
  }
  copyLookFromRow(copy_confirm_look, cs);
  copy_confirm_pending = true;
}

static void showLinkConfirmPopup(int idx);

// What a tap on a row of the spool list leads to, once the row is true.
void linkRowChosen(int idx) {
  if (copy_flow_via_list) linkCopyConfirmFromRow(idx);
  else                    showLinkConfirmPopup(idx);
}

// Where the tag-against-spool rows start in the link confirmation, right
// under the two lines of spool info, and the room kept under them.
#define LINK_CONFIRM_ROWS_Y    96
#define LINK_CONFIRM_ROWS_GAP   6

static void showLinkConfirmPopup(int idx) {
  if (idx < 0 || idx >= link_spool_count) return;
  UnlinkedSpool &s = link_spools[idx];

  // A Bambu tag says what is on the spool; the row says what the spool on
  // file is. When the list fell back to "without the material filter" every
  // row is some other filament, and a tap linked it without a word. The row
  // carries all the verdict needs, a row out of the cache included.
  TagSpoolVerdict verdict;
  if (link_flow_is_bambu && !copy_flow_via_list && strlen(g_tag.tray_uuid) == 32) {
    verdict = tagSpoolCompare(g_tag.material, g_tag.color_hex, s.material, s.name,
                              s.vendor, s.color_hex, s.article_hit);
  }
  const bool mismatch = verdict.any();
  if (mismatch) {
    char why[32];
    tagSpoolVerdictText(verdict, why, sizeof(why));
    logSDf("Link confirm: spool %d does not match the tag (%s)", s.id, why);
  }

  // Sicherheits-Popup (halbtransparentes Overlay)
  releaseScreen(&scr_link_confirm);
  lv_obj_t *popup = lv_obj_create(lv_scr_act());
  scr_link_confirm = popup;
  lv_obj_set_size(popup, 480, 320);
  lv_obj_set_pos(popup, 0, 0);
  lv_obj_set_style_bg_color(popup, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(popup, LV_OPA_70, 0);
  lv_obj_set_style_border_width(popup, 0, 0);
  lv_obj_set_style_radius(popup, 0, 0);
  lv_obj_set_style_pad_all(popup, 0, 0);
  lv_obj_clear_flag(popup, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *box = lv_obj_create(popup);
  lv_obj_set_size(box, 440, 220);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(mismatch ? UI_COL_BAD_TEXT : UI_COL_GOOD), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_q = lv_label_create(box);
  lv_label_set_text(lbl_q, copy_flow_via_list ? T(STR_COPY_CONFIRM_TITLE)
                         : mismatch           ? T(STR_LINK_MISMATCH_TITLE)
                                              : T(STR_CONFIRM_LINK));
  lv_obj_set_style_text_color(lbl_q, lv_color_hex(mismatch ? UI_COL_BAD_TEXT : UI_COL_GOOD), 0);
  lv_obj_set_style_text_font(lbl_q, &lv_font_montserrat_ext_18, 0);
  lv_obj_set_style_text_align(lbl_q, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_q, LV_ALIGN_TOP_MID, 0, 16);

  // Spulen-Info
  char info[80];
  char joined[64];
  joinMaterialName(s.material, s.name, joined, sizeof(joined));
  snprintf(info, sizeof(info), "#%d  %s\n%.0f g / %.0f g",
    s.id, joined, s.remaining, s.total);
  lv_obj_t *lbl_info = lv_label_create(box);
  lv_label_set_text(lbl_info, info);
  lv_obj_set_style_text_color(lbl_info, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_style_text_align(lbl_info, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_info, 400);
  lv_obj_align(lbl_info, LV_ALIGN_TOP_MID, 0, 48);

  // The tag and the spool side by side, and the buttons below them move down
  // by as much. Built only when there is something to show.
  int extra = 0;
  if (mismatch) {
    extra = tagSpoolCompareRows(box, LINK_CONFIRM_ROWS_Y, verdict, s.material,
                                s.color_hex, s.vendor) + LINK_CONFIRM_ROWS_GAP;
    lv_obj_set_height(box, 220 + extra);
  }

  // Link button - y=110, h=46
  lv_obj_t *btn_yes = lv_btn_create(box);
  lv_obj_set_size(btn_yes, 420, 46);
  lv_obj_set_pos(btn_yes, 10, 110 + extra);
  lv_obj_set_style_bg_color(btn_yes, lv_color_hex(mismatch ? UI_COL_BAD_BG : UI_COL_GO_BG), 0);
  lv_obj_set_style_bg_color(btn_yes, lv_color_hex(mismatch ? UI_COL_BAD_BG_PRESSED : UI_COL_GO_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_yes, 8, 0);
  lv_obj_set_style_shadow_width(btn_yes, 0, 0);
  lv_obj_set_style_border_width(btn_yes, 0, 0);
  lv_obj_set_user_data(btn_yes, (void*)(intptr_t)idx);
  lv_obj_add_event_cb(btn_yes, [](lv_event_t *e) {
    int cidx = (intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    // Asynchronously: the popup is this button's grandparent, and the rest
    // of this callback still runs inside its event dispatch.
    releaseScreen(&scr_link_confirm);
    // The same test the row callback does before it opens this popup. It is
    // needed twice because the array can be freed between the two taps:
    // anything that reaches hideAllOverlays() does that, a backend switch
    // from the browser among them, and the popup outlives it.
    if (!link_spools || cidx < 0 || cidx >= link_spool_count) {
      logSDf("Link confirm: spool list gone, ignoring idx=%d", cidx);
      return;
    }
    if (copy_flow_via_list) {
      linkCopyConfirmFromRow(cidx);
    } else if (link_cu_ok && linkTargetBase(link_spools[cidx].id)) {
      // Only bound spools get a second dialog, and only because they are
      // the ones the list would have hidden before the switch existed.
      // The confirmation behind us says which spool, this one says that it
      // is already bound and that nothing will be replaced.
      showWarnPopupA(link_spools[cidx].id, linkTargetBase(link_spools[cidx].id),
                     link_flow_is_bambu, "", true);
    } else {
      // The id is copied now; the list it came from may be freed before
      // the handler runs.
      link_patch_id    = link_spools[cidx].id;
      link_patch_bambu = link_flow_is_bambu;
      link_patch_pending = true;
    }
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_yes = lv_label_create(btn_yes);
  lv_label_set_text(lbl_yes, copy_flow_via_list ? T(STR_BTN_CONFIRMED)
                          : mismatch           ? T(STR_BTN_LINK_ANYWAY)
                                               : T(STR_LINK_OK));
  lv_obj_set_style_text_color(lbl_yes, lv_color_hex(mismatch ? UI_COL_BAD_TEXT : UI_COL_OK_TEXT_2), 0);
  lv_obj_set_style_text_font(lbl_yes, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_yes);

  // Cancel button - y=164 (gap=8 after btn_yes ends at 156)
  lv_obj_t *btn_no = lv_btn_create(box);
  lv_obj_set_size(btn_no, 420, 40);
  lv_obj_set_pos(btn_no, 10, 164 + extra);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_no, 8, 0);
  lv_obj_set_style_shadow_width(btn_no, 0, 0);
  lv_obj_set_style_border_width(btn_no, 0, 0);
  lv_obj_add_event_cb(btn_no, [](lv_event_t *e) {
    releaseScreen(&scr_link_confirm);
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_no = lv_label_create(btn_no);
  lv_label_set_text(lbl_no, T(STR_CANCEL));
  lv_obj_set_style_text_color(lbl_no, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_no, &lv_font_montserrat_ext_14, 0);
  lv_obj_center(lbl_no);
}

// What is on the scale as the list opens, or -1 when nothing may be compared
// against it. Read here and not in the fetch: a list out of the cache is there
// before the spool has settled on the pad. A copy picks a template, and a
// template's stored weight says nothing about the spool on the scale.
static float linkListGross() {
  if (copy_flow_via_list || !scale_ready) return -1.0f;
  return spoolExtrasBareReading();
}

// The spool's stored remaining weight matches what is on the scale. Compared
// is the number the row shows, so a hit is one the user can check by eye. The
// empty weight is the spool's own; a Bambu spool without one falls back to the
// core weight the new-tag flow assumes too, which is also where BamBuddy
// leaves its spools, with the core weight on the filament only.
static bool linkWeightHit(const UnlinkedSpool& s, float gross) {
  if (gross <= 0.0f) return false;
  float tare = s.spool_weight;
  if (tare <= 0.0f && strncasecmp(s.vendor, "Bambu", 5) == 0) tare = (float)BAMBU_CORE_WEIGHT_G;
  if (tare <= 0.0f) return false;
  const float net = gross - tare;
  if (net < 0.0f) return false;
  const float stored = (s.remaining <= 0 && s.total > 0) ? s.total : s.remaining;
  return fabsf(net - stored) <= LINK_WEIGHT_TOLERANCE_G;
}

// Order of the rows in the final list: article number and weight, article
// number only, weight only, the rest. Within one rank the fetch's order stands.
static int linkRowRank(bool article_hit, bool weight_hit) {
  return (article_hit ? 2 : 0) + (weight_hit ? 1 : 0);
}
#define LINK_ROW_RANKS 4

void showFilteredSpoolList(const char* vendor_name, const char* material_prefix, const char* material_full) {
  crumbSet("spool list build");
  // snprintf() and not strncpy(): the reload hands these very buffers' copies
  // back in, and the material may be null.
  { char v[sizeof(link_list_vendor)], m[sizeof(link_list_mat)], mf[sizeof(link_list_mat_full)];
    snprintf(v,  sizeof(v),  "%s", vendor_name     ? vendor_name     : "");
    snprintf(m,  sizeof(m),  "%s", material_prefix ? material_prefix : "");
    snprintf(mf, sizeof(mf), "%s", material_full   ? material_full   : "");
    memcpy(link_list_vendor,   v,  sizeof(v));
    memcpy(link_list_mat,      m,  sizeof(m));
    memcpy(link_list_mat_full, mf, sizeof(mf)); }
  logSDf("SHOW: FilteredSpoolList vendor=%s mat=%s matf=%s", vendor_name, material_prefix, material_full ? material_full : "");
  releaseScreen(&scr_link_spools);

  scr_link_spools = buildLinkOverlay();

  // Count matching spools for title
  int display_count = 0;
  for (int i = 0; i < link_spool_count; i++) {
    if (!linkRowMatches(link_spools[i], vendor_name, material_prefix, material_full)) continue;
    display_count++;
  }

  char title_buf[48];
  if (link_flow_is_bambu) {
    snprintf(title_buf, sizeof(title_buf), "Bambu %s - %d",
      g_tag.material[0] ? g_tag.material : "", display_count);
  } else if (material_full && material_full[0]) {
    char v8[9], m10[11];
    utf8Cut(vendor_name, 8, v8, sizeof(v8));
    utf8Cut(material_full, 10, m10, sizeof(m10));
    snprintf(title_buf, sizeof(title_buf), "%s %s - %d", v8, m10, display_count);
  } else if (material_prefix[0]) {
    char v8[9];
    utf8Cut(vendor_name, 8, v8, sizeof(v8));
    snprintf(title_buf, sizeof(title_buf), "%s %.4s - %d", v8, material_prefix, display_count);
  } else {
    snprintf(title_buf, sizeof(title_buf), "%s - %d", T(STR_SPOOLS_ALL), display_count);
  }

  // Header: 52px, Back left, Cancel/X right, title center
  lv_obj_t *hdr = lv_obj_create(scr_link_spools);
  lv_obj_set_size(hdr, 480, 52);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(hdr);
  lv_label_set_text(lbl_title, title_buf);
  lv_obj_set_style_text_color(lbl_title, lv_color_hex(UI_COL_ACCENT), 0);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_ext_16, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

  // Back button top-left
  lv_obj_t *btn_hdr_back = lv_btn_create(hdr);
  lv_obj_set_size(btn_hdr_back, 44, 44);
  lv_obj_set_pos(btn_hdr_back, 4, 4);
  lv_obj_set_style_bg_color(btn_hdr_back, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_bg_color(btn_hdr_back, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_hdr_back, 8, 0);
  lv_obj_set_style_shadow_width(btn_hdr_back, 0, 0);
  lv_obj_set_style_border_width(btn_hdr_back, 0, 0);
  lv_obj_add_event_cb(btn_hdr_back, [](lv_event_t *e) {
    logSD("BTN: SpoolList -> Back");
    link_nav_pending = LNAV_SPOOLS_BACK;
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_hdr_back);
    lv_label_set_text(l, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0);
    lv_obj_center(l); }

  // Cancel/X button top-right
  lv_obj_t *btn_hdr_cancel = lv_btn_create(hdr);
  lv_obj_set_size(btn_hdr_cancel, 44, 44);
  lv_obj_align(btn_hdr_cancel, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(btn_hdr_cancel, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_hdr_cancel, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_hdr_cancel, 8, 0);
  lv_obj_set_style_shadow_width(btn_hdr_cancel, 0, 0);
  lv_obj_set_style_border_width(btn_hdr_cancel, 0, 0);
  lv_obj_add_event_cb(btn_hdr_cancel, [](lv_event_t *e) {
    logSD("BTN: SpoolList -> Cancel");
    // Out to the main screen like every other X of the flow. It left the copy
    // entry behind, which then stood there again, and the copy mode on: the
    // next link through the picker would have opened a copy confirmation.
    copy_flow_via_list = false;
    releaseScreen(&scr_copy_entry);
    releaseScreen(&scr_link_spools);
    releaseScreen(&scr_link_entry);
    releaseScreen(&scr_link_vendor);
    releaseScreen(&scr_link_mat);
    releaseScreen(&scr_link_mat_sub);
  }, LV_EVENT_CLICKED, NULL);
  { lv_obj_t *l = lv_label_create(btn_hdr_cancel);
    lv_label_set_text(l, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_BAD_TEXT), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_18, 0);
    lv_obj_center(l); }

  // Separator
  lv_obj_t *div = lv_obj_create(scr_link_spools);
  lv_obj_set_size(div, 480, 1); lv_obj_set_pos(div, 0, 52);
  lv_obj_set_style_bg_color(div, lv_color_hex(UI_COL_LINE), 0);
  lv_obj_set_style_border_width(div, 0, 0);
  lv_obj_set_style_radius(div, 0, 0);
  lv_obj_set_style_pad_all(div, 0, 0);

  // Scrollable list - full height below header
  lv_obj_t *list = lv_obj_create(scr_link_spools);
  lv_obj_set_size(list, 460, link_list_reloadable ? LINK_LIST_H - LINK_STRIP_H : LINK_LIST_H);
  lv_obj_set_pos(list, 10, 56);
  lv_obj_set_style_bg_color(list, lv_color_hex(UI_COL_GROUND), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_radius(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  // A list that was not fetched just now says so, and offers the way out.
  //
  // Built before the rows, not after them: when the LVGL pool runs short the
  // row loop below cuts itself off, and built last, the one thing missing
  // would be the button that gets the user out of a stale list. A child of
  // the screen rather than of the list, so it is there for an empty list too.
  //
  // The clock time of the download, and its age as of building this screen.
  // The time stays true for as long as the list is open; the age is not kept
  // current, which would need a tick and a pointer to a label whose life hangs
  // on this screen. Every list screen is built anew, so it is right whenever
  // the list comes up, which is when someone reads it.
  if (link_list_reloadable) {
    listReloadStrip(scr_link_spools, spoolCacheFilledAt(), spoolCacheAgeMs(), [](lv_event_t *e) {
      logSD("BTN: SpoolList -> Reload");
      link_reload_pending = true;
    });
  }
  logLvMem("spoollist/pre", 0);

  // Before the rows, not after them: it explains what the whole list is, and
  // at the bottom of a long list nobody would find it.
  if (link_material_ignored) addListMoreInfo(list, STR_LIST_MAT_IGNORED);
  // Said once, on the list that was rebuilt because of it.
  if (link_list_changed_note) {
    link_list_changed_note = false;
    addListMoreInfo(list, STR_LIST_SPOOL_CHANGED);
  }

  // One walk per rank, best first, folded into one loop so a cut below still
  // ends the whole list with a single break. The array keeps its order: the
  // vendor and material lists are built from it, and a spool that happens to
  // weigh the same must not reorder those.
  const float gross = linkListGross();
  int weight_hits = 0;
  int count = 0;
  for (int k = 0; k < LINK_ROW_RANKS * link_spool_count; k++) {
    const int rank = LINK_ROW_RANKS - 1 - k / link_spool_count;
    const int i = k % link_spool_count;
    if (count >= spool_list_limit) break;  // render limit - full data is still in link_spools[]
    UnlinkedSpool &s = link_spools[i];

    // The same question the count above asked, asked once.
    if (!linkRowMatches(s, vendor_name, material_prefix, material_full)) continue;
    const bool weight_hit = linkWeightHit(s, gross);
    if (linkRowRank(s.article_hit, weight_hit) != rank) continue;

    // A row is five objects. LVGL 8.3 answers an exhausted pool with NULL and
    // asserts nothing, and every widget constructor writes through that pointer
    // one line later - so running out here is a panic reboot, not the freeze the
    // assert handler suggests. Asked before the row rather than after each of its
    // objects, because the pool ran out between the second and the third.
    if (!lvPoolHasRoomForRow()) {
      logSDf("FilteredSpoolList: LVGL pool low, list cut at %d rows", count);
      break;
    }
    lv_obj_t *row = lv_btn_create(list);
    if (!row) { logSDf("FilteredSpoolList: no room for a row, list cut at %d", count); break; }
    // Counted once the row exists, so a cut at the very first candidate leaves
    // count == 0 and the "no spools" message below still appears.
    count++;
    lv_obj_set_size(row, 452, 56);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_SURFACE), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COL_PRESS_FILL), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    // An article number hit is framed in the house green: the same row, no
    // extra object, which the pool could not spare for every row.
    lv_obj_set_style_border_width(row, s.article_hit ? 2 : 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(s.article_hit ? UI_COL_GOOD : UI_COL_LINE_SOFT), 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    // ── Zeile 1: #ID + Material+Name ──────────────────────
    lv_obj_t *lbl_id = lv_label_create(row);
    char id_buf[10]; snprintf(id_buf, sizeof(id_buf), "%d", s.id);
    lv_label_set_text(lbl_id, id_buf);
    lv_obj_set_style_text_color(lbl_id, lv_color_hex(UI_COL_ACCENT), 0);
    lv_obj_set_style_text_font(lbl_id, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_id, LV_ALIGN_TOP_LEFT, 6, 5);

    // Material + Name zusammen, abgeschnitten wenn zu lang
    // Avoid duplication when the filament name already starts with the material
    // (Spoolman often stores names like "PLA+ White" while material is "PLA+")
    lv_obj_t *lbl_name = lv_label_create(row);
    char full_name[64];
    if (s.material[0]) {
      joinMaterialName(s.material, s.name, full_name, sizeof(full_name));
    } else {
      strncpy(full_name, s.name, sizeof(full_name)-1);
    }
    full_name[sizeof(full_name)-1] = '\0';
    lv_label_set_text(lbl_name, full_name);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(UI_COL_INK), 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_ext_16, 0);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 50, 5);
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_name, 396);  // volle Breite - kein Hersteller in Zeile 1

    // ── Zeile 2: Farbkachel + Gewicht + Hersteller rechts ─
    // Color tile (14x14px)
    lv_obj_t *swatch = lv_obj_create(row);
    lv_obj_set_size(swatch, 14, 14);
    lv_obj_align(swatch, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_obj_set_style_radius(swatch, 3, 0);
    lv_obj_set_style_border_width(swatch, 1, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(UI_COL_RULE), 0);
    lv_obj_set_style_pad_all(swatch, 0, 0);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
    swatchPaintHex(swatch, s.color_hex);

    // Gewicht neben Kachel
    lv_obj_t *lbl_rest = lv_label_create(row);
    char rest_buf[24];
    if (s.remaining <= 0 && s.total > 0)
      snprintf(rest_buf, sizeof(rest_buf), T(STR_NEW_SPOOL_WEIGHT_FMT), s.total);
    else
      snprintf(rest_buf, sizeof(rest_buf), "%.0f g", s.remaining);
    lv_label_set_text(lbl_rest, rest_buf);
    // A weight that matches the scale in the house green, on the label that
    // is there anyway: the frame already says "article number".
    if (weight_hit) weight_hits++;
    lv_obj_set_style_text_color(lbl_rest, lv_color_hex(weight_hit ? UI_COL_GOOD : UI_COL_CAPTION), 0);
    lv_obj_set_style_text_font(lbl_rest, &lv_font_montserrat_ext_14, 0);
    lv_obj_align(lbl_rest, LV_ALIGN_BOTTOM_LEFT, 26, -5);

    // Click → Sicherheits-Popup
    lv_obj_add_event_cb(row, [](lv_event_t *e) {
      const int idx = (int)(intptr_t)lv_event_get_user_data(e);
      // A row that was downloaded a moment ago opens the question as it always
      // did. One out of the cache is read from the server first, and that is a
      // request: parked for the loop, which opens the question afterwards.
      if (link_spools && idx >= 0 && idx < link_spool_count && link_spools[idx].from_cache) {
        link_row_refresh_pending = idx;
        return;
      }
      linkRowChosen(idx);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  logLvMem("spoollist/post", count);
  logSDf("SHOW: FilteredSpoolList rows=%d gross=%.0f weight_hits=%d", count, gross, weight_hits);
  if (count == 0) {
    lv_obj_t *lbl_empty = lv_label_create(scr_link_spools);
    lv_label_set_text(lbl_empty, T(STR_NO_SPOOLS));
    lv_obj_set_style_text_color(lbl_empty, lv_color_hex(UI_COL_WARN), 0);
    lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_ext_16, 0);
    lv_obj_set_style_text_align(lbl_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_empty, LV_ALIGN_CENTER, 0, -20);
  } else if (count >= spool_list_limit) {
    addListMoreInfo(list, STR_LIST_MORE_SPOOLS);
  }
}
