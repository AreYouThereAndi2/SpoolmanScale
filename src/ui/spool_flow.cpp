#include "spool_flow.h"
#include "ui_common.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <cstring>

#include "app_config.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/server_reach.h"
#include "services/spool_cache.h"
#include "services/tag_field.h"
#include "services/tag_uid.h"
#include "services/user_options.h"
#include "services/backend_api.h"
#include "ui/spool_flow_internal.h"
#include "services/tag_write.h"
#include "ui/confirm_popup.h"
#include "ui/info_popup.h"
#include "ui/second_tag_popup.h"
#include "services/spool_tare.h"
#include "ui/tag_write_popup.h"
#include "ui/theme.h"

// The link flow: its shared state, the rules about what a spool holds in its
// tag fields, and the loop's half of every button in
// handleSpoolFlowDeferredActions(). The rest stands beside it, one file per
// part of the flow:
//   spool_flow_fetch.cpp   loading the spool list, cache or backend worker
//   spool_flow_patch.cpp   the link write and what follows it, moving a tag
//   spool_flow_id.cpp      link by id: the keypad, the lookup, the warnings
//   spool_flow_list.cpp    the spool list and the question in front of a link
//   spool_flow_picker.cpp  the entry popup, the vendor and material lists
//   spool_flow_newtag.cpp  a new spool made from the tag
// What they share is declared in spool_flow_internal.h.

// UnlinkedSpool, the row of the link and copy lists, stands in spool_flow_internal.h.

// True when the filament name already opens with the material. Spoolman's own
// naming does this, and BamBuddy's mapping always does ("PETG HF Orange"), so
// prefixing the material a second time would read "PETG PETG HF Orange".
bool nameStartsWithMaterial(const char* name, const char* material) {
  if (!name || !name[0]) return false;
  if (!material || !material[0]) return true;   // nothing left to prefix with
  return strncasecmp(name, material, strlen(material)) == 0;
}

// Material and filament name as one line, without saying anything twice.
// Besides a name that repeats the whole material, a name may repeat its
// subtype: Spoolman libraries hold "PLA Tough+" with "Tough+ Cyan" and
// "PETG Translucent" with "Translucent Gray", which read "PLA Tough+ Tough+
// Cyan". The words the two share are written once, and only whole words.
void joinMaterialName(const char* material, const char* name, char* out, size_t out_size) {
  if (!name) name = "";
  if (!material) material = "";
  if (nameStartsWithMaterial(name, material)) { snprintf(out, out_size, "%s", name); return; }
  for (const char* p = strchr(material, ' '); p; p = strchr(p + 1, ' ')) {
    const char* tail = p + 1;
    const size_t n = strlen(tail);
    if (n && strncasecmp(name, tail, n) == 0 && (name[n] == '\0' || name[n] == ' ')) {
      snprintf(out, out_size, "%s%s", material, name + n);
      return;
    }
  }
  // No name: the material alone, without a trailing space.
  snprintf(out, out_size, "%s%s%s", material, name[0] ? " " : "", name);
}

// Whether a second UID can be appended at all right now: the selected tag
// field has a list format, the switch is on, and the server actually has that
// field. Probed where the list is loaded,
// never in a button callback - backendHasCardUidsField() can reach the network
// on the first call, and it is the same answer patchSpoolTag() decides on.
bool link_cu_ok = false;

UnlinkedSpool* link_spools = nullptr;  // PSRAM-allocated at fetch time, freed after link flow
int            link_spool_count = 0;
// Slots actually allocated in link_spools. The fetch functions allocate an
// exact fit for what they load, so anything appending afterwards has to grow
// the block first instead of writing at link_spool_count.
int            link_spools_capacity = 0;

// Release the spool list and reset both counters together. Splitting these up
// is how the capacity drifted out of sync with the allocation in the first place.
void linkSpoolsFree() {
  if (link_spools) { free(link_spools); link_spools = nullptr; }
  link_spool_count    = 0;
  link_spools_capacity = 0;
}

char          link_tag_uid[24] = "";   // UID of the tag to be linked

// Neuer Link-Flow Overlays
lv_obj_t *scr_link_entry   = nullptr;  // Entry popup
lv_obj_t *scr_link_id      = nullptr;  // Numeric keypad
lv_obj_t *scr_link_warn_a  = nullptr;  // Warning popup A (already linked)
lv_obj_t *scr_link_warn_b  = nullptr;  // Warning popup B (material mismatch)
lv_obj_t *scr_link_vendor  = nullptr;  // Vendor-Auswahl (Flow B Pfad 2)
lv_obj_t *scr_link_mat     = nullptr;  // Material selection (flow B path 2)
lv_obj_t *scr_link_mat_sub = nullptr;  // Material sub-name selection (Stufe 3)
lv_obj_t *scr_link_spools  = nullptr;  // Spool list (all path 2)

// State for ID input
char link_id_input[8] = "";            // Input buffer for numeric keypad
lv_obj_t *lbl_link_id_display = nullptr; // Label for digit display
lv_obj_t *lbl_link_id_status  = nullptr; // Error label in numeric keypad

// State for path-2 navigation
char link_selected_vendor[32]   = "";   // selected vendor
char link_selected_material[8]  = "";   // 3-char material prefix
char link_selected_material_full[32] = ""; // full material name (Stufe 3)
// What the spool list on screen was built with, so that it can be built again
// after a reload - see linkReloadSpoolList().
char link_list_vendor[32]   = "";
char link_list_mat[8]       = "";
char link_list_mat_full[32] = "";
bool link_stage3_shown = false;          // true if stage 3 actually rendered (not auto-skipped)
bool link_flow_is_bambu = false;         // which flow is active

// Copy spool flow state
// scr_copy_entry, scr_copy_list, scr_copy_confirm and copy_flow_archived
// belong to ui/copy_flow.cpp, see spool_flow_internal.h.
bool copy_flow_via_list = false;        // true = copy flow using vendor/material list path
bool copy_confirm_pending = false;      // deferred showCopyConfirmPopup from list row click
int  copy_confirm_fid = 0;
// The template's own spool id. Carried alongside the filament id because the
// two backends anchor a copy differently: Spoolman points the new spool at the
// template's filament, BamBuddy has no filament as an object and reads the
// template spool back instead. In BamBuddy the filament id is always 0.
int  copy_confirm_spool_id = 0;
float copy_confirm_remaining = 0, copy_confirm_initial = 0, copy_confirm_spool_w = 0;
char copy_confirm_name[80] = {};
CopyLook copy_confirm_look = {};

void copyLookFromRow(CopyLook& look, const UnlinkedSpool& s) {
  snprintf(look.material,  sizeof(look.material),  "%s", s.material);
  snprintf(look.name,      sizeof(look.name),      "%s", s.name);
  snprintf(look.vendor,    sizeof(look.vendor),    "%s", s.vendor);
  snprintf(look.color_hex, sizeof(look.color_hex), "%s", s.color_hex);
}

// Opening the popup costs an HTTP round trip for the colour name, so the
// button only raises a flag and loop() does the work - same reason as
// copy_confirm_pending above.
bool newtag_open_pending    = false;
// btn_copy: global for show/hide alongside btn_link
lv_obj_t *btn_copy = nullptr;
// Configurable list limit - loaded from NVS, adjustable via webserver /listlimit

// Popup control: prevents immediate re-display after cancel
bool id_popup_is_bambu = false;  // shared between numpad lambdas
bool id_popup_is_copy   = false;  // true = copy flow, false = link flow
int  copy_id_lookup_pending = 0;  // >0 = deferred copy ID fetch (avoids stack overflow in lambda)
int  link_id_lookup_pending = 0;  // >0 = deferred linkIdLookupAndPatch (avoids stack overflow in lambda)
bool link_id_lookup_is_bambu = false;
// Link overlays that hideSpoolFlowOverlays() has hidden and emptied, waiting to
// be deleted on the loop task. They must not be deleted where they are hidden:
// that call arrives from LVGL callbacks too.
// The last fetch found nothing for the tag's material and dropped that part of
// the filter. The list says so at the top - a long list of the right vendor is
// workable, a list of nothing is a dead end, but the user still has to know
// which of the two they are looking at.
bool link_material_ignored = false;
// Whether the list on screen gets the strip with its time and "Reload": one
// out of the cache, and one just downloaded into it. Only the second used to
// be missing, so a reload took the button away with it - and with it the way
// to ask again after a change in the backend that still did not show.
bool link_list_reloadable = false;
// One line on top of the next spool list, then cleared: a row was tapped, the
// server said something else about that spool, and the list was built again.
bool link_list_changed_note = false;
static bool link_overlays_close_pending = false;
bool show_id_input_pending = false;   // deferred re-open of IdInputPopup from Back button

// Parked by the flow's buttons for handleSpoolFlowDeferredActions(). Every
// one of these reaches the backend - a link PATCH, an inventory fetch, a
// spool create - and used to run inside the callback of the button that
// asked for it, with the touch panel dead for the length of the request.
bool  link_patch_pending      = false;   // doLinkPatch(id, bambu)
int   link_patch_id           = 0;
bool  link_patch_bambu        = false;
bool  link_list_fetch_pending = false;   // "from the list" on the link entry
int   link_row_refresh_pending = -1;     // a cached row was tapped: index into link_spools
bool  link_reload_pending      = false;  // "Reload" under a list that came out of the cache
bool  newtag_create_pending   = false;   // new-from-tag OK

// The confirmation that sits on top of the spool list ("link this one?").
// Built inside the row's callback and, until now, held by nothing: a
// navigation away - a backend switch from the browser, say - left it drawn
// over whatever came next. Hidden and freed with the other link overlays.
lv_obj_t *scr_link_confirm = nullptr;

// The step parked for the loop, see LinkNav in spool_flow_internal.h.
LinkNav link_nav_pending = LNAV_NONE;
char    link_nav_vendor[32] = "";   // the vendor a row picked
bool show_id_input_rebuild = false;   // deferred re-open from WarnPopupA retry (rebuild after del)
bool id_input_open = false;           // true while IdInputPopup is visible - suppresses NFC Spoolman query

bool link_popup_dismissed = false;              // user dismissed the popup
unsigned long link_tag_first_seen_ms = 0;       // time of first detection

// Is this spool already bound to a tag?
//
// Any of the tag fields can hold one, and which one depends on what wrote it:
// this firmware writes the selected field, SpoolLink writes card_uids, FilaMan
// and SpoolSense write nfc_id. Asking only about the selected one would offer
// a spool bound elsewhere as free and let it collect a second, redundant
// binding.
//
// Not static: the tag lookup hands it to the list cache together with the
// inventory it has just scanned, so that there stays one version of this rule.
bool spoolHasAnyTag(JsonObjectConst spool) {
  JsonObjectConst extra = spool["extra"];
  if (extra.isNull()) return false;

  for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++) {
    const char* key = tagFieldSpec(f).key;
    if (extra[key].isNull()) continue;
    // Spoolman stores extra values JSON encoded, so an unset field arrives as
    // a pair of literal quotes rather than as an empty string.
    String v = extra[key].as<String>();
    v.replace("\"", "");
    v.trim();
    if (v.length() > 0) return true;
  }
  return false;
}

// What the spool holds in each tag field, into the row. Which field binds a
// spool decides everything the flow does next: whether it is offered at all,
// whether the warning says "overwrite" or "add", and whether the write appends
// to a list or migrates a UID out of one field into another.
//
// Never keep a shortened value: appending to a truncated list would drop the
// entries that fell off the end. Empty means "unknown", and the write then
// treats the spool as unbound rather than acting on half a list.
//
// s.id has to be set, the log line names it.
void linkFillTagValues(UnlinkedSpool& s, JsonObjectConst spool) {
  for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++) {
    s.tag_values[f][0] = '\0';
    const char* key = tagFieldSpec(f).key;
    if (spool["extra"][key].isNull()) continue;
    String v = spool["extra"][key].as<String>();
    v.replace("\"",""); v.trim();
    if (v.length() == 0) continue;
    if (v.length() < CARD_UIDS_MAX) {
      strncpy(s.tag_values[f], v.c_str(), CARD_UIDS_MAX - 1);
      s.tag_values[f][CARD_UIDS_MAX - 1] = '\0';
    } else {
      logSDf("link: %s of spool %d too long (%d), ignored",
             key, s.id, (int)v.length());
    }
  }
}

// True when any tag field of this spool holds something, whichever one it is.
static bool linkSpoolBound(const UnlinkedSpool& s) {
  for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++)
    if (s.tag_values[f][0]) return true;
  return false;
}

// Whether a row should be left out of a rendered list.
//
// Has to agree with the fetch filter in fetchAllSpoolsForLink(): the fetch
// keeps bound spools whenever a second UID can be appended, and a render pass
// that dropped them anyway would put them in the array and then hide them -
// which is exactly what happened when this was an open coded
// "is extra.tag set" check in six places. Bound spools simply stopped
// appearing, with nothing in the log to say why.
//
// The copy-from-archived flow is the exception it always was: there a tagged
// spool is a template, not a conflict.
bool linkSpoolSkip(const UnlinkedSpool& s) {
  if (copy_flow_via_list && copy_flow_archived) return false;
  if (link_cu_ok) return false;
  return linkSpoolBound(s);
}

// The target spool's fields as patchSpoolTag() wants them: one pointer per
// TagFieldId, null where the field is empty. Both fetch paths fill the entry,
// so this is the single place the write decision reads from.
//
// The pointer array is static because it has to outlive the call and there is
// only ever one link in flight; the strings it points at live in the spool
// list, which is not freed until the flow closes.
static const char* s_target_values[TAG_FIELD_COUNT];

const char* const* linkTargetValues(int spool_id) {
  for (int i = 0; i < link_spool_count; i++) {
    if (link_spools[i].id != spool_id) continue;
    for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++)
      s_target_values[f] = link_spools[i].tag_values[f][0]
                         ? link_spools[i].tag_values[f] : nullptr;
    return s_target_values;
  }

  // The list is gone by the time a second tag is linked: closeLinkOverlays()
  // frees it, and that has already run. What is left is what the reload after
  // the first link captured, and for the spool on the pad that is the same
  // document read one request ago.
  //
  // It matters for the list field. Without a base to append to, the write
  // would start a fresh list and drop the tag it was supposed to join.
  if (sm_found && sm_id == spool_id) {
    for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++)
      s_target_values[f] = sm_tag_values[f][0] ? sm_tag_values[f] : nullptr;
    return s_target_values;
  }
  return nullptr;
}

// What the next UID would be added to, for the popup to describe: the selected
// field if the spool is already bound there, otherwise whichever field does
// bind it, because that UID becomes the first entry once it is migrated.
// nullptr for an unbound spool. The popup counts what is in here and
// patchSpoolTag() writes onto it, so the number shown and the value written
// cannot drift apart.
const char* linkTargetBase(int spool_id) {
  const char* const* v = linkTargetValues(spool_id);
  if (!v) return nullptr;
  const uint8_t eff = tagFieldEffective();
  if (v[eff]) return v[eff];
  for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++) if (v[f]) return v[f];
  return nullptr;
}

void hideSpoolFlowOverlays() {
  // The spool array is what every link overlay reads from, so freeing it while
  // one of them is still on screen leaves a list that renders from nothing and
  // a confirm button that dereferences a null pointer - a panic reboot, and
  // for a while the only explanation for three of them in one test session.
  //
  // Hidden first, because a hidden object takes no input: from this line on
  // the overlays cannot reach the freed array no matter what the user taps.
  // Deleting them here is not allowed - hideAllOverlays() is reached from
  // LVGL callbacks as well, so the delete is parked for the next loop pass.
  //
  // The copy flow reads the same array and lives on the same rules, so its
  // three screens and the new-tag popup are in the list: left out, they
  // stayed drawn on top of whatever was built next, with rows pointing into
  // the freed list.
  lv_obj_t *const link_scr[] = {
    scr_link_entry, scr_link_id, scr_link_warn_a, scr_link_warn_b,
    scr_link_vendor, scr_link_mat, scr_link_mat_sub, scr_link_spools,
    scr_link_confirm, scr_tag_move,
    scr_copy_entry, scr_copy_list, scr_copy_confirm, scr_newtag
  };
  for (unsigned i = 0; i < sizeof(link_scr) / sizeof(link_scr[0]); i++)
    if (link_scr[i]) {
      lv_obj_add_flag(link_scr[i], LV_OBJ_FLAG_HIDDEN);
      link_overlays_close_pending = true;
    }

  linkSpoolsFree();
}

void deleteSpoolFlowOverlays() {
  closeNewTagPopup();
  releaseScreen(&scr_link_confirm);
  releaseScreen(&scr_tag_move);
  releaseScreen(&scr_copy_entry);
  releaseScreen(&scr_copy_list);
  releaseScreen(&scr_copy_confirm);
}

void handleSpoolFlowDeferredActions() {
  // A link list on its way from the backend worker.
  linkFetchTick();
  // First, so nothing below builds on top of an overlay that is already dead.
  // closeLinkOverlays() deletes exactly the nine screens hidden there and frees
  // the array again, which is a no-op the second time around.
  if (link_overlays_close_pending) {
    link_overlays_close_pending = false;
    logSD("Link flow: overlays closed after the spool list was freed");
    closeLinkOverlays();
    deleteSpoolFlowOverlays();
  }

  // ---- stepping between the link lists -----------------------------------
  if (link_nav_pending != LNAV_NONE) {
    const LinkNav nav = link_nav_pending;
    link_nav_pending = LNAV_NONE;
    // Every list below is built from this array. Gone between the tap and
    // this pass - a backend switch from the browser does that - there is
    // nothing left to step through, and the overlays went with it.
    if (link_spools) {
      // A copy, because showMaterialList() writes the name it is given into
      // link_selected_vendor - the buffer some of these would hand it.
      char vendor[sizeof(link_selected_vendor)];
      strncpy(vendor, link_selected_vendor, sizeof(vendor) - 1);
      vendor[sizeof(vendor) - 1] = '\0';
      switch (nav) {
        case LNAV_SPOOLS_BACK:
          if (scr_link_spools) { lv_obj_del(scr_link_spools); scr_link_spools = nullptr; }
          if (link_flow_is_bambu) {
            if (scr_link_entry) lv_obj_clear_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN);
          } else if (link_stage3_shown) {
            // NTAG: back to stage 3 if it was really shown, else to stage 2
            showMaterialSubList(vendor, link_selected_material);
          } else {
            showMaterialList(vendor);
          }
          break;
        case LNAV_MAT_BACK:
          if (scr_link_mat) { lv_obj_del(scr_link_mat); scr_link_mat = nullptr; }
          showVendorList();
          break;
        case LNAV_MAT_ROW:
          if (scr_link_mat) { lv_obj_del(scr_link_mat); scr_link_mat = nullptr; }
          showMaterialSubList(vendor, link_selected_material);
          break;
        case LNAV_MATSUB_BACK:
          if (scr_link_mat_sub) { lv_obj_del(scr_link_mat_sub); scr_link_mat_sub = nullptr; }
          showMaterialList(vendor);
          break;
        case LNAV_MATSUB_ROW:
          if (scr_link_mat_sub) { lv_obj_del(scr_link_mat_sub); scr_link_mat_sub = nullptr; }
          showFilteredSpoolList(vendor, link_selected_material, link_selected_material_full);
          break;
        case LNAV_VENDOR_ROW:
          if (scr_link_vendor) { lv_obj_del(scr_link_vendor); scr_link_vendor = nullptr; }
          showMaterialList(link_nav_vendor);
          break;
        case LNAV_NONE:
          break;
      }
    }
  }

  // ---- a row out of the cache was tapped ----------------------------------
  // Before the patch below, so a row is read before anything can write to it.
  if (link_row_refresh_pending >= 0) {
    const int idx = link_row_refresh_pending;
    link_row_refresh_pending = -1;
    // Between the tap and this pass a backend switch from the browser can have
    // taken the list away, and the screen with it.
    if (link_spools && idx < link_spool_count && scr_link_spools) {
      switch (linkRefreshRow(idx)) {
        case LINK_ROW_OK:
          linkRowChosen(idx);
          break;
        case LINK_ROW_CHANGED:
          link_list_changed_note = true;
          linkReloadSpoolList();
          break;
        case LINK_ROW_NETWORK:
          break;   // said by a popup, the list stays as it is
      }
    }
  }

  // ---- "Reload" under a list out of the cache ------------------------------
  if (link_reload_pending) {
    link_reload_pending = false;
    // The same guard as above: the list and its screen can be gone by now.
    if (link_spools && scr_link_spools) {
      spoolCacheForget("reload asked for");
      linkReloadSpoolList();
    }
  }

  // ---- what the flow's buttons parked ----------------------------------
  if (link_patch_pending) {
    link_patch_pending = false;
    // The warning popups and the numpad go first; doLinkPatch() then reaches
    // the backend and repaints the main screen.
    releaseScreen(&scr_link_warn_a);
    releaseScreen(&scr_link_warn_b);
    closeIdInputPopup();
    doLinkPatch(link_patch_id, link_patch_bambu);
  }
  if (link_list_fetch_pending) {
    link_list_fetch_pending = false;
    // Load and pre-filter spools, then start the appropriate flow.
    // A list that never arrived is not an empty one: the popup says why, and
    // the picker is not opened on nothing that reads as "no spools".
    linkFetchRun(link_flow_is_bambu, link_flow_is_bambu ? g_tag.material : "", false,
                 link_flow_is_bambu ? LF_NEXT_DIRECT_LIST     // Flow A: direct list
                                    : LF_NEXT_VENDOR_LIST);   // Flow B: 3-step
  }
  // The copy flow's own: the list it asked for, and the spool to create.
  copyFlowDeferredActions();
  if (newtag_create_pending) {
    newtag_create_pending = false;
    closeNewTagPopup();
    closeCopyListPopup();
    closeCopyEntryPopup();
    doCreateSpoolFromTag();
  }
  // ---- a tag another spool holds ------------------------------------------
  if (tagmove_ask_pending) {
    tagmove_ask_pending = false;
    // Two small requests, on the loop, before the question goes up.
    fetchSpoolSummary(s_move_from_id,  s_move_from_sum);
    fetchSpoolSummary(s_move_spool_id, s_move_to_sum);
    showTagMovePopup();
  }
  if (tagmove_close_pending) {
    tagmove_close_pending = false;
    releaseScreen(&scr_tag_move);
    if (tagmove_yes_pending) {
      tagmove_yes_pending = false;
      runTagMove();
    }
  }

  // ---- a link that arrived from outside ---------------------------------
  if (s_remote_link_ms) {
    if (!tag_present || millis() - s_remote_link_ms > REMOTE_LINK_EXPECT_MS) {
      s_remote_link_ms = 0;
    } else if (sm_found && sm_id > 0) {
      s_remote_link_ms = 0;
      logSDf("Link: spool %d was bound from outside, arming the follow-ups", sm_id);
      armLinkFollowUps(sm_id, tagIsBambu(g_tag.tray_uuid));
    }
  }

  if (tagwrite_after_link_id > 0) {
    const int id = tagwrite_after_link_id;
    tagwrite_after_link_id = 0;
    // "Always" skips the question, not the answer: the write is parked the same
    // way and its result still appears, because a tag that silently failed to
    // take the record would be the worst of both.
    if (g_tagwrite_mode == TAGWRITE_ALWAYS) startTagWriteNoAsk(id);
    else                                    showTagWriteAskPopup(id);
  }
  // Behind the tag write on purpose, and behind whatever it started. The
  // record goes onto the tag that is still lying on the reader, so asking for
  // the next one first would be asking the user to take it away mid write.
  //
  // "pending" is the state tagWriteTick() reports while a write is in flight;
  // the popup covers the question in front of it.
  if (secondtag_after_link_id > 0 &&
      !isTagWritePopupOpen() && strcmp(tagWriteState(), "pending") != 0) {
    const int id = secondtag_after_link_id;
    secondtag_after_link_id = 0;
    // Not on top of the numpad or the link entry. Both own the reader for
    // their own purpose, and the popup would take the next tag away from them.
    if (isConfirmPopupOpen() || isSpoolFlowIdInputOpen() || isSpoolFlowLinkEntryOpen()) {
      logSDf("TAG2: not asking for spool %d, another popup has the screen", id);
    } else {
      showSecondTagPopup(id, g_tag.uid_str);
    }
  }
  // Behind every other modal, and only when none is up: this explains a link
  // that already succeeded, so it can wait for the screen to be free rather
  // than stack on top of the tag write question.
  if (nativemissing_pending && !uiModalWaiting()) {
    nativemissing_pending = false;
    logSD("Link: selected tag source missing on this server, said so");
    showInfoPopup(STR_TF_NOREL_TITLE, STR_TF_NOREL_TEXT, INFO_WARN);
  }
  if (show_id_input_pending) {
    show_id_input_pending = false;
    id_input_open = false;
    link_id_lookup_pending = 0;
    copy_id_lookup_pending = 0;
    releaseScreen(&scr_link_id);
    lbl_link_id_display = nullptr;
    lbl_link_id_status  = nullptr;
    // Clean up entry popups if they were hidden by X button
    if (scr_copy_entry && (lv_obj_has_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN))) {
      lv_obj_del(scr_copy_entry); scr_copy_entry = nullptr;
    }
    if (scr_link_entry && (lv_obj_has_flag(scr_link_entry, LV_OBJ_FLAG_HIDDEN))) {
      lv_obj_del(scr_link_entry); scr_link_entry = nullptr;
    }
  }
  if (show_id_input_rebuild) {
    show_id_input_rebuild = false;
    link_id_lookup_pending = 0;
    copy_id_lookup_pending = 0;
    link_id_input[0] = '\0';
    lbl_link_id_display = nullptr;
    lbl_link_id_status  = nullptr;
    // The old numpad goes first. From the loop this may be synchronous, and
    // it should be: the new one is built a few lines down, and two numpads
    // in the pool at once is room the spool list would have used.
    if (scr_link_id) { lv_obj_del(scr_link_id); scr_link_id = nullptr; }
    // Free the link_spools buffer grown during the failed lookup so the next
    // lookup starts with a clean slate and no out-of-bounds risk.
    linkSpoolsFree();
    // Pump LVGL twice to flush all residual events from the deleted screen.
    lv_timer_handler();
    lv_timer_handler();
    // Clear all pending flags AFTER pump; residual confirm callbacks may have re-set them.
    link_id_lookup_pending = 0;
    copy_id_lookup_pending = 0;
    showIdInputPopup(link_flow_is_bambu);
  }
  if (newtag_open_pending) {
    newtag_open_pending = false;
    showNewFromTagPopup();
  }
  if (copy_confirm_pending) {
    copy_confirm_pending = false;
    // The list the template was picked from stays, hidden, so that Cancel on
    // the confirmation goes back to it - the copy list or the picker's spool
    // list, whichever this came from. It used to delete the picker's, and
    // Cancel then ended the whole flow.
    if (scr_copy_list)   lv_obj_add_flag(scr_copy_list, LV_OBJ_FLAG_HIDDEN);
    if (scr_link_spools) lv_obj_add_flag(scr_link_spools, LV_OBJ_FLAG_HIDDEN);
    // The entry stood under the picker and would show through the scrim.
    if (scr_copy_entry)  lv_obj_add_flag(scr_copy_entry, LV_OBJ_FLAG_HIDDEN);
    showCopyConfirmPopup(copy_confirm_spool_id, copy_confirm_fid, copy_confirm_name,
                        copy_confirm_remaining, copy_confirm_initial, copy_confirm_spool_w,
                        &copy_confirm_look);
  }
  // link_id_lookup_pending removed - direct call in callback (was causing PANIC)
  if (link_id_lookup_pending > 0 && scr_link_warn_a == nullptr && scr_link_warn_b == nullptr) {
    int pid = link_id_lookup_pending;
    bool pbambu = link_id_lookup_is_bambu;
    link_id_lookup_pending = 0;
    // Close numpad before HTTP call
    releaseScreen(&scr_link_id);
    lbl_link_id_display = nullptr; lbl_link_id_status = nullptr;
    id_input_open = false;
    linkIdLookupAndPatch(pid, pbambu);
  }
  if (copy_id_lookup_pending > 0) {
    int cid = copy_id_lookup_pending;
    copy_id_lookup_pending = 0;
    // Fetch spool data for copy confirm - done in loop to avoid stack overflow in lambda.
    JsonDocument cdoc;
    DeserializationError derr2 = DeserializationError::Ok;
    int hcode = serverReachNote(backendGetSpoolJson(cfg_spoolman_base, cid, cdoc, 5000, &derr2), true);
    if (hcode != 200) {
      if (lbl_link_id_status)
        lv_label_set_text(lbl_link_id_status, T(serverReachIsNetworkFailure(hcode)
                                                ? STR_NO_CONNECTION : STR_LINK_ID_NOT_FOUND));
    } else {
      if (!derr2) {
        int cfid   = cdoc["filament"]["id"] | 0;
        float cini = cdoc["filament"]["weight"] | 1000.0f;
        float cspw = spoolTare(cdoc);
        float crem = cdoc["remaining_weight"] | 0.0f;
        const char *cfname = cdoc["filament"]["name"] | "?";
        const char *cfmat  = cdoc["filament"]["material"] | "";
        const char *cfvnd  = cdoc["filament"]["vendor"]["name"] | "";
        char ctmpl[80];
        char cjoined[64];
        joinMaterialName(cfmat, cfname, cjoined, sizeof(cjoined));
        snprintf(ctmpl, sizeof(ctmpl), "%s (%s)", cjoined, cfvnd);
        lbl_link_id_display = nullptr;
        lbl_link_id_status  = nullptr;
        releaseScreen(&scr_link_id);
        CopyLook look = {};
        snprintf(look.material, sizeof(look.material), "%s", cfmat);
        snprintf(look.name,     sizeof(look.name),     "%s", cfname);
        snprintf(look.vendor,   sizeof(look.vendor),   "%s", cfvnd);
        const char *ccol = cdoc["filament"]["color_hex"] | "";
        if (ccol[0] == '#') ccol++;
        if (ccol[0]) snprintf(look.color_hex, sizeof(look.color_hex), "#%s", ccol);
        showCopyConfirmPopup(cid, cfid, ctmpl, crem, cini, cspw, &look);
      } else {
        if (lbl_link_id_status) lv_label_set_text(lbl_link_id_status, T(STR_LINK_JSON_ERR));
      }
    }
  }
}

void setSpoolFlowIdInputOpen(bool open) {
  id_input_open = open;
}

bool isSpoolFlowIdInputOpen() {
  return id_input_open;
}

bool isSpoolFlowLinkEntryOpen() {
  return scr_link_entry != nullptr;
}

bool spoolFlowTakeScannedId(int spool_id, bool unknown_tag_present) {
  if (spool_id <= 0) return false;
  // A warning about the last id is open: that question comes first.
  if (scr_link_warn_a || scr_link_warn_b) return false;
  if (id_input_open) {
    // The keypad is open: the scan is the id that would have been typed,
    // for the copy flow as much as for the link.
    if (id_popup_is_copy) {
      copy_id_lookup_pending = spool_id;
    } else {
      link_id_lookup_pending  = spool_id;
      link_id_lookup_is_bambu = id_popup_is_bambu;
    }
    logSDf("Scan: spool %d taken as the keypad id", spool_id);
    return true;
  }
  if (scr_link_entry) {
    const bool is_bambu = link_flow_is_bambu;
    closeLinkEntryPopup();
    link_id_lookup_pending  = spool_id;
    link_id_lookup_is_bambu = is_bambu;
    logSDf("Scan: spool %d answers the open link entry", spool_id);
    return true;
  }
  if (unknown_tag_present) {
    link_id_lookup_pending  = spool_id;
    link_id_lookup_is_bambu = strlen(g_tag.material) > 0;
    logSDf("Scan: spool %d links the unknown tag on the reader", spool_id);
    return true;
  }
  return false;
}
