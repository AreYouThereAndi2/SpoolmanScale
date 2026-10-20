#include "spool_flow.h"
#include "ui_common.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <cstring>

#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/spoolman_actions.h"
#include "services/server_reach.h"
#include "services/spool_cache.h"
#include "services/uid_index.h"
#include "services/tag_field.h"
#include "services/tag_uid.h"
#include "services/user_options.h"
#include "services/backend_api.h"
#include "ui/spool_flow_internal.h"
#include "services/tag_write.h"
#include "ui/info_popup.h"
#include "ui/main_screen_helpers.h"
#include "ui/second_tag_popup.h"
#include "ui/spoolman_lookup.h"
#include "ui/theme.h"
#include "services/breadcrumb.h"

namespace {

struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) ptr = malloc(size);
    return ptr;
  }
  void deallocate(void* pointer) override { heap_caps_free(pointer); }
  void* reallocate(void* ptr, size_t new_size) override {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    if (!p) p = realloc(ptr, new_size);
    return p;
  }
};

}

// The link write and what follows it: the tag write and second tag
// questions, and moving a tag that another spool holds.
// Moved out of spool_flow.cpp word for word; what it shares with the rest
// of the link flow is declared in spool_flow_internal.h.

// ============================================================
//  SPOOLMAN: SAVE TAG UUID (extra.tag)
// ============================================================
// Tears down every screen of the link flow and frees the PSRAM spool list.
// Both the successful and the aborted path need exactly this.
void closeLinkOverlays() {
  releaseScreen(&scr_link_entry);
  // Through its own close: that one also clears id_input_open and the two
  // labels. Deleting the numpad by hand left the flag standing, and every
  // tag lookup in appLoop() stands aside while it is set - a backend switch
  // from the web page with the numpad open meant no tag was looked up again
  // until somebody happened to reach the main screen.
  closeIdInputPopup();
  releaseScreen(&scr_link_warn_a);
  releaseScreen(&scr_link_warn_b);
  releaseScreen(&scr_link_vendor);
  releaseScreen(&scr_link_mat);
  releaseScreen(&scr_link_mat_sub);
  releaseScreen(&scr_link_spools);
  linkSpoolsFree();
}

// ============================================================
//  LINK FLOW: COMPLETE LINKING
//  PATCH + update main screen
// ============================================================
// Set at the end of a link, picked up on the next pass. Not built there and
// then: doLinkPatch() is reached from LVGL callbacks as well as from this
// handler, and an overlay must never be built out of the callback of the one
// it covers.
int tagwrite_after_link_id = 0;

// Same idea for the second tag question, and picked up in the same handler -
// but only once the tag write above is out of the way, or the two would fight
// over the screen and over the tag on the reader.
int secondtag_after_link_id = 0;

// Set while linkAdditionalTag() is running. It marks the write as an addition
// rather than a binding, and it keeps doLinkPatch() from arming the question
// that led here - a link made in answer to the question must not ask it again.
static bool s_additional_link = false;

// A link that Spoolman refused because the tag is on another spool. Offered
// as a move rather than left at a red line in the status bar: the line is
// repainted by the NFC poll a moment later, and a tag on the wrong spool is
// something the user can decide about.
lv_obj_t *scr_tag_move        = nullptr;
bool      tagmove_ask_pending   = false;
bool      tagmove_close_pending = false;
bool      tagmove_yes_pending   = false;
int       s_move_spool_id   = 0;      // where the tag should go
int       s_move_from_id    = 0;      // where Spoolman says it is
static bool      s_move_is_bambu   = false;
static bool      s_move_additional = false;  // it was the second tag
static char      s_move_uid[40]    = "";

SpoolSummary s_move_from_sum, s_move_to_sum;

void fetchSpoolSummary(int spool_id, SpoolSummary &out) {
  snprintf(out.text, sizeof(out.text), "#%d", spool_id);
  out.color[0] = '\0';
  if (!wifi_ok || spool_id <= 0) return;
  SpiRamAllocator psram_alloc;
  JsonDocument doc(&psram_alloc);
  DeserializationError err = DeserializationError::Ok;
  if (backendGetSpoolJson(cfg_spoolman_base, spool_id, doc, 5000, &err) != 200 || err) return;
  JsonVariant f = doc["filament"];
  const char* vendor   = f["vendor"]["name"] | "";
  const char* material = f["material"]       | "";
  const char* name     = f["name"]           | "";
  const char* color    = f["color_hex"]      | "";
  snprintf(out.text, sizeof(out.text), "#%d  %s %s  %s", spool_id, vendor, material, name);
  snprintf(out.color, sizeof(out.color), "%s", color);
}

// One spool of the move question: caption column, swatch, words.
static void moveRow(lv_obj_t *box, int y, StringID caption_id, const SpoolSummary &sum) {
  lv_obj_t *cap = lv_label_create(box);
  { char cb[24]; copyT(cb, sizeof(cb), caption_id);
    lv_label_set_text(cap, cb); }
  lv_obj_set_style_text_color(cap, lv_color_hex(UI_COL_INK_SOFT), 0);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_pos(cap, 20, y + 2);
  int x = 20 + 104;
  if (sum.color[0]) {
    lv_obj_t *sw = lv_obj_create(box);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 18, 18);
    lv_obj_set_pos(sw, x, y + 1);
    lv_obj_set_style_radius(sw, 4, 0);
    swatchPaintHex(sw, sum.color);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_set_style_border_color(sw, lv_color_hex(UI_COL_CAPTION), 0);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    x += 26;
  }
  lv_obj_t *l = lv_label_create(box);
  lv_label_set_text(l, sum.text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, 400 - 20 - x);
  lv_obj_set_style_text_color(l, lv_color_hex(UI_COL_INK_2), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_ext_16, 0);
  lv_obj_set_pos(l, x, y);
}

// Set when a lookup that stood at "not in Spoolman" resolves without the
// scale having linked anything: somebody bound the tag from outside. The loop
// re-reads the tag; once that has found the spool, the follow-ups a link from
// the scale gets are armed. Expires when the tag leaves or nothing comes.
unsigned long s_remote_link_ms = 0;

bool isSpoolFlowTagMoveOpen() { return scr_tag_move != nullptr; }

void spoolFlowExpectRemoteLink() {
  s_remote_link_ms = millis() ? millis() : 1;
}

void spoolFlowAskSecondTag(int spool_id) {
  if (spool_id > 0 && g_tag2_ask && backendCanHoldSecondTag())
    secondtag_after_link_id = spool_id;
}

// Raised when the link went into the default field because the selected source
// is not on this server. Shown once, and only when the screen is free - an
// overlay must not be built out of the callback the link came from.
bool nativemissing_pending = false;

// Whether the last doLinkPatch() actually wrote. It has no return value and
// three ways out, and linkAdditionalTag() must not report success over an
// error message doLinkPatch() has just put on the screen.
static bool s_last_link_ok = false;

// What comes after a link, whichever way it was made: the tag write question
// and the second tag question. Called by doLinkPatch() for a link from the
// scale, and from the loop for one that arrived from outside - Spoolman's own
// web page binding the tag that lies on the reader.
void armLinkFollowUps(int spool_id, bool is_bambu) {
  // The spool is known and an NTAG is lying on the reader, so its data could go
  // onto the tag as well. Three things have to hold before that is worth
  // asking about:
  //
  //  - the user asked for the question at all. It used to appear after every
  //    link in every backend mode, which is the complaint this switch answers.
  //  - the tag can be written. Bambu tags are read only and MIFARE is not ours
  //    to write, both ruled out by the same test the tag page uses.
  //  - the record fits. An NTAG213 holds 144 bytes and an OpenSpool record
  //    needs around 190, so on those tags the question could only ever end in
  //    "OpenSpool needs 176 bytes, this tag holds 144". Asked in bytes rather
  //    than by tag type, so a tag reporting its size honestly decides for
  //    itself. A tag that reports nothing at all (0) is not ruled out - the
  //    write says what happened either way.
  if (g_tagwrite_mode != TAGWRITE_OFF && !is_bambu && tagIsWritableNtag()) {
    const uint16_t have = tagCachedBytes();
    const bool fits = !TAG_FMT_IS_NDEF(g_tagwrite_fmt) ||
                      have == 0 || have >= TAGWRITE_NDEF_MIN_BYTES;
    if (fits) tagwrite_after_link_id = spool_id;
    else logSDf("Tag write: not asking, %u bytes on the tag is too small for %s",
                (unsigned)have, tagFormatLabel(g_tagwrite_fmt));
  }

  // The tag on the other flange, once the write above has settled. Three
  // conditions, and the order is the cheap ones first: the user asked for the
  // question, this is not itself the answer to one, and the server can hold a
  // further tag at all - that last one may probe, which is why it runs here in
  // the loop rather than where the settings row is drawn.
  if (g_tag2_ask && !s_additional_link && backendCanHoldSecondTag())
    secondtag_after_link_id = spool_id;

}

void doLinkPatch(int spool_id, bool is_bambu) {
  doLinkPatchUid(spool_id, is_bambu, is_bambu ? g_tag.tray_uuid : link_tag_uid);
}

// The link itself, for a link from the scale and for the end of a copy: the
// write with its conflict and network answers, the index, the screen, and the
// follow-ups (tag write, second tag). A copy used to write the tag on its own
// and skipped every one of those. True when the tag was written.
bool doLinkPatchUid(int spool_id, bool is_bambu, const char* link_uuid) {
  crumbSet("link patch");
  Serial.printf("doLinkPatch: ID=%d uuid='%s'\n", spool_id, link_uuid ? link_uuid : "");

  // Linking without a UID must never reach patchSpoolTag. An empty value is
  // the unlink signal there: FilaMan is sent rfid_uid null, Spoolman an empty
  // extra.tag, and both answer 200. The screen afterwards reloads the spool
  // by id, so it looks linked while the tag field is in fact empty.
  //
  // The UID comes from a buffer that is cleared whenever the tag display is
  // reset, so it can legitimately be gone by the time the user finishes
  // picking a spool. That is a failed link, not an unlink.
  s_last_link_ok = false;
  if (!link_uuid || !link_uuid[0]) {
    logSDf("LINK ABORT: no tag UID for spool %d (bambu=%d)", spool_id, is_bambu ? 1 : 0);
    Serial.println("doLinkPatch: aborted, no tag UID");
    statusMessageShow(T(STR_LINK_NO_TAG), UI_COL_BAD_TEXT);
    closeLinkOverlays();
    return false;
  }

  // The bolt on the one door every write goes through. A row out of the cache
  // holds no tag values, and linkTargetValues() below would hand that on as
  // "unbound": a spool with two UIDs would come out with one. Every way here
  // has read the row already - the tap on it, the numpad - so this should
  // never find anything. If it does, it reads the spool itself rather than
  // refuse, and the line in the log says that a way around the refresh exists.
  //
  // Not for a further tag: its spool is not picked from a list, and it is
  // bound by the first tag on purpose. A row left from an earlier list read
  // that as "bound meanwhile" and turned the second tag away (26.09.2026).
  for (int i = 0; i < link_spool_count && !s_additional_link; i++) {
    if (link_spools[i].id != spool_id || !link_spools[i].from_cache) continue;
    logSDf("link patch: row of spool %d still cached, refreshed late", spool_id);
    const LinkRowRefresh r = linkRefreshRow(i);
    if (r != LINK_ROW_OK) {
      logSDf("LINK ABORT: spool %d could not be read before the write (%d)", spool_id, (int)r);
      statusMessageShow(T(r == LINK_ROW_NETWORK ? STR_LINK_NO_CONNECTION : STR_LIST_SPOOL_CHANGED),
                        UI_COL_BAD_TEXT);
      closeLinkOverlays();
      return false;
    }
    break;
  }

  // Both stores go along: the list is appended to when there is one, and the
  // tag field's UID becomes the list's first entry when there is not. Null for
  // an unbound spool, and for every spool at all while the switch is off.
  if (!patchSpoolTag(spool_id, link_uuid, linkTargetValues(spool_id),
                     s_additional_link)) {
    // Nothing was written - the list was full, or the request failed. Saying
    // nothing here would look like a successful link right up to the next scan.
    logSDf("LINK ABORT: tag field of spool %d not written", spool_id);
    char buf[48];
    // Spoolman refuses a UID that another spool holds and says which one.
    // Naming it is the difference between "that did not work" and something
    // the user can act on - and the move question below is the acting.
    if (sm_tag_conflict_spool > 0) {
      snprintf(buf, sizeof(buf), T(STR_TAG_ON_OTHER_SPOOL), sm_tag_conflict_spool);
      if (sm_tag_conflict_spool != spool_id) {
        s_move_spool_id   = spool_id;
        s_move_from_id    = sm_tag_conflict_spool;
        s_move_is_bambu   = is_bambu;
        s_move_additional = s_additional_link;
        snprintf(s_move_uid, sizeof(s_move_uid), "%s", link_uuid);
        tagmove_ask_pending = true;
      }
      sm_tag_conflict_spool = 0;
    } else if (sm_tag_conflict_filament > 0) {
      // Spoolman 0.27 binds tags to filaments too. Such a tag cannot be moved
      // from here, but naming the filament says where to look.
      snprintf(buf, sizeof(buf), T(STR_TAG_ON_FILAMENT), sm_tag_conflict_filament);
      sm_tag_conflict_filament = 0;
    } else if (tagBindingFailedOnNetwork()) {
      // The server never answered. "Not added" sent people looking for a
      // fault in the spool or the tag, when the link simply has to be retried.
      copyT(buf, sizeof(buf), STR_LINK_NO_CONNECTION);
    } else {
      copyT(buf, sizeof(buf), STR_CU_NOT_WRITTEN);
    }
    buf[sizeof(buf) - 1] = '\0';
    statusMessageShow(buf, UI_COL_BAD_TEXT);
    closeLinkOverlays();
    return false;
  }

  // Asked before the reload below, because the answer to it is already in.
  if (patchSpoolTagTakeNativeMissing()) nativemissing_pending = true;

  // The next list comes out of the cache, and this spool is no longer free.
  // Not for Spoolman's own tag relation: spoolHasAnyTag() does not see it, so
  // a downloaded list keeps offering such a spool, and the cache has to say
  // what a download would say - no more and no less.
  if (!tagFieldSelected().is_native) spoolCacheSetBound(spool_id, true);

  // And the uid index learns the tag it has just watched being bound. The
  // searches find it from now on, but the index must not be what calls it
  // unknown on the day one of them says no by mistake.
  {
    const char* linked[3] = { link_uuid, tagNativeUid(link_uuid), g_tag.uid_str };
    uidIndexNote(linked, 3);
  }

  closeLinkOverlays();

  // Re-query Spoolman - use single-spool endpoint since we know the ID
  link_popup_dismissed = false;
  if (is_bambu) {
    tagLookupForget();
    querySpoolmanById(spool_id);
  } else {
    strncpy(g_tag.uid_str, link_tag_uid, sizeof(g_tag.uid_str)-1);
    g_tag.uid_str[sizeof(g_tag.uid_str)-1] = '\0';
    strncpy(g_tag.tray_uuid, link_tag_uid, sizeof(g_tag.tray_uuid)-1);
    g_tag.tray_uuid[sizeof(g_tag.tray_uuid)-1] = '\0';
    tagLookupForget();
    querySpoolmanById(spool_id);
  }

  armLinkFollowUps(spool_id, is_bambu);

  s_last_link_ok = true;
  Serial.printf("Linking complete! ID=%d\n", spool_id);
  return true;
}

void linkAdditionalTag(int spool_id, const char* uid) {
  if (spool_id <= 0 || !uid || !uid[0]) return;

  // One source for the tag type, the same one doLinkPatch() reads. A Bambu
  // spool keeps its 32 character tray uuid across both chips, while a second
  // NTAG cleared the field when it was seen - so the length answers this
  // without anybody having to carry a flag along.
  const bool is_bambu = tagIsBambu(g_tag.tray_uuid);

  strncpy(link_tag_uid, uid, sizeof(link_tag_uid) - 1);
  link_tag_uid[sizeof(link_tag_uid) - 1] = '\0';
  logSDf("TAG2: linking '%s' to spool %d (bambu=%d)", uid, spool_id, is_bambu ? 1 : 0);

  s_additional_link = true;
  doLinkPatch(spool_id, is_bambu);
  s_additional_link = false;

  if (!s_last_link_ok) {
    // doLinkPatch put the reason on the status line, which the NFC poll
    // repaints a moment later. A conflict opens the move question instead;
    // everything else is said in a modal, or it was never seen.
    if (!tagmove_ask_pending) showInfoPopup(STR_TAG2_FAILED, STR_CU_NOT_WRITTEN, INFO_WARN);
    secondTagLinked(false);
    return;
  }
  secondTagLinked(true);

  // The chip that is now on the reader belongs in the field an MMU gate reads
  // as well. querySpoolmanById() above refreshed sm_hw_uid_value but does not
  // write, and waiting for the next placement would leave the spool answering
  // at one gate only. Free while the switch is off, which it checks first.
  syncHwUidField(spool_id, g_tag.tray_uuid);

  if (lbl_status) {
    char buf[48];
    copyT(buf, sizeof(buf), STR_TAG2_LINKED);
    lv_label_set_text(lbl_status, buf);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(UI_COL_GOOD), 0);
  }
  // Said in a modal as well, because that status line is repainted by the
  // NFC poll on the next pass and the link had no visible outcome. Not when a
  // tag write follows: that ends in a result popup of its own, and two in a
  // row would be one too many.
  if (tagwrite_after_link_id == 0)
    showInfoPopup(STR_TAG2_LINKED, STR_TAG2_LINKED_INFO, INFO_DONE);
}

// ------------------------------------------------------------------
//  Moving a tag another spool holds
// ------------------------------------------------------------------
void showTagMovePopup() {
  releaseScreen(&scr_tag_move);
  scr_tag_move = lv_obj_create(lv_scr_act());
  lv_obj_set_size(scr_tag_move, 480, 320);
  lv_obj_set_pos(scr_tag_move, 0, 0);
  lv_obj_set_style_bg_color(scr_tag_move, lv_color_hex(UI_COL_SCRIM), 0);
  lv_obj_set_style_bg_opa(scr_tag_move, LV_OPA_70, 0);
  lv_obj_set_style_border_width(scr_tag_move, 0, 0);
  lv_obj_set_style_radius(scr_tag_move, 0, 0);
  lv_obj_set_style_pad_all(scr_tag_move, 0, 0);
  lv_obj_clear_flag(scr_tag_move, LV_OBJ_FLAG_SCROLLABLE);

  // The house measurements of the tag write question, so the two read as
  // the same kind of thing.
  const int box_w = 400, box_h = 260, btn_w = 170, btn_h = 56;
  lv_obj_t *box = lv_obj_create(scr_tag_move);
  lv_obj_set_size(box, box_w, box_h);
  lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COL_SURFACE), 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COL_POPUP_BORDER), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, 12, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *icon = lv_label_create(box);
  lv_label_set_text(icon, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(icon, lv_color_hex(UI_COL_WARN), 0);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_ext_24, 0);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 14);

  lv_obj_t *lbl_q = lv_label_create(box);
  { char qb[64]; snprintf(qb, sizeof(qb), T(STR_TAG_ON_OTHER_SPOOL), s_move_from_id);
    lv_label_set_text(lbl_q, qb); }
  lv_obj_set_style_text_color(lbl_q, lv_color_hex(UI_COL_INK), 0);
  lv_obj_set_style_text_font(lbl_q, &lv_font_montserrat_ext_20, 0);
  lv_obj_set_style_text_align(lbl_q, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lbl_q, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_q, box_w - 40);
  lv_obj_align(lbl_q, LV_ALIGN_TOP_MID, 0, 52);

  // The two spools, so the decision is between things the user recognises.
  moveRow(box, 96,  STR_TAGMOVE_FROM, s_move_from_sum);
  moveRow(box, 124, STR_TAGMOVE_TO,   s_move_to_sum);

  lv_obj_t *lbl_hint = lv_label_create(box);
  { char hb[64]; snprintf(hb, sizeof(hb), T(STR_TAGMOVE_HINT), s_move_from_id);
    lv_label_set_text(lbl_hint, hb); }
  lv_obj_set_style_text_color(lbl_hint, lv_color_hex(UI_COL_INK_SOFT), 0);
  lv_obj_set_style_text_font(lbl_hint, &lv_font_montserrat_ext_14, 0);
  lv_obj_set_style_text_align(lbl_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lbl_hint, box_w - 40);
  lv_obj_align(lbl_hint, LV_ALIGN_TOP_MID, 0, 158);

  lv_obj_t *btn_ok = lv_btn_create(box);
  lv_obj_set_size(btn_ok, btn_w, btn_h);
  lv_obj_set_pos(btn_ok, 12, box_h - btn_h - 18);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(UI_COL_OK_BG), 0);
  lv_obj_set_style_bg_color(btn_ok, lv_color_hex(UI_COL_OK_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_ok, 8, 0);
  lv_obj_set_style_shadow_width(btn_ok, 0, 0);
  lv_obj_add_event_cb(btn_ok, [](lv_event_t *e) {
    // Flags only: the move costs two requests, and this button sits on the
    // popup that has to go first.
    tagmove_yes_pending   = true;
    tagmove_close_pending = true;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_ok = lv_label_create(btn_ok);
  { char bb[32]; copyT(bb, sizeof(bb), STR_TAGMOVE_BTN); lv_label_set_text(lbl_ok, bb); }
  lv_obj_set_style_text_color(lbl_ok, lv_color_hex(UI_COL_OK_TEXT), 0);
  lv_obj_set_style_text_font(lbl_ok, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_ok);

  lv_obj_t *btn_no = lv_btn_create(box);
  lv_obj_set_size(btn_no, btn_w, btn_h);
  lv_obj_set_pos(btn_no, box_w - btn_w - 12, box_h - btn_h - 18);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(UI_COL_BAD_BG), 0);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(UI_COL_BAD_BG_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(btn_no, 8, 0);
  lv_obj_set_style_shadow_width(btn_no, 0, 0);
  lv_obj_add_event_cb(btn_no, [](lv_event_t *e) {
    tagmove_close_pending = true;
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_no = lv_label_create(btn_no);
  { char cb[32]; copyT(cb, sizeof(cb), STR_CANCEL); lv_label_set_text(lbl_no, cb); }
  lv_obj_set_style_text_color(lbl_no, lv_color_hex(UI_COL_BAD_TEXT), 0);
  lv_obj_set_style_text_font(lbl_no, &lv_font_montserrat_ext_18, 0);
  lv_obj_center(lbl_no);

  logSDf("SHOW: TagMovePopup tag '%s' from spool %d to %d", s_move_uid,
         s_move_from_id, s_move_spool_id);
}

// Takes the tag off the spool that holds it and runs the link again. On the
// loop task: two requests.
void runTagMove() {
  const char* native_uid = tagNativeUid(s_move_uid);
  const int code = serverReachNote(backendUnlinkTag(cfg_spoolman_base, s_move_from_id, native_uid, 8000), true);
  logSDf("TAG MOVE: '%s' off spool %d HTTP %d", native_uid, s_move_from_id, code);
  if (code < 200 || code >= 300) {
    showInfoPopup(STR_TAGMOVE_FAILED, STR_CU_NOT_WRITTEN, INFO_WARN);
    return;
  }
  if (s_move_additional) {
    linkAdditionalTag(s_move_spool_id, s_move_uid);
  } else {
    strncpy(link_tag_uid, s_move_uid, sizeof(link_tag_uid) - 1);
    link_tag_uid[sizeof(link_tag_uid) - 1] = '\0';
    doLinkPatch(s_move_spool_id, s_move_is_bambu);
  }
}
