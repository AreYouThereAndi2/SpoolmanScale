#pragma once

// ============================================================
//  THE LINK FLOW AND THE COPY FLOW, WHAT THEY SHARE
//
//  ui/copy_flow.cpp came out of spool_flow.cpp (v0.8.0-beta.47). The two
//  flows share the spool list in PSRAM and a handful of flags: an NTAG copy
//  runs through the link flow's vendor and material picker, the link flow's
//  ID input doubles as the copy's, and the spool created from the tag is
//  finished the way a copy is. What they share is declared here; the rest
//  stays static in its own file.
//
//  The link flow itself stands in several files as well, one per part of it
//  (see the top of spool_flow.cpp), and what those share among themselves is
//  declared here too.
//
//  Included by those files only, and in front of lang.h: it pulls in
//  ArduinoJson, whose templates do not survive the T() macro.
// ============================================================

#include <ArduinoJson.h>
#include <lvgl.h>
#include <time.h>

#include "lang.h"
#include "services/spool_color.h"
#include "services/tag_field.h"
#include "services/tag_uid.h"

// One row of the link and copy lists, in PSRAM.
struct UnlinkedSpool {
  int   id;
  char  name[48];      // filament.name
  char  vendor[32];    // filament.vendor.name
  char  material[16];  // filament.material (PLA, PETG, ABS...)
  char  color_hex[SPOOL_COLOR_HEX_MAX];  // filament.color_hex, #RRGGBB or #RRGGBBAA
  // The row was handed out by services/spool_cache and nobody has read the
  // spool from the server since. Its tag_values are then EMPTY whatever the
  // spool holds, because the cache keeps no tag values - and empty is what a
  // write takes for "unbound". So a row with this set must never reach
  // patchSpoolTag(): linkRefreshRow() reads the spool and clears it.
  //
  // In the row and not beside it, because qsort() moves whole rows. In the two
  // bytes of padding that were here anyway, the row stays at 896. Every place
  // that fills a row has to set it: the block comes from heap_caps_malloc(),
  // which does not zero.
  bool  from_cache;
  // The spool's filament carries the article number Bambu's catalog names
  // for the tag on the reader: exactly this product in exactly this colour.
  // Sorted to the top and framed in the list. In the padding too.
  bool  article_hit;
  float remaining;     // remaining_weight
  float total;         // filament.weight
  // What the spool holds in each tag field, indexed by TagFieldId, quote
  // stripped, empty where the field holds nothing. Three jobs: it says whether
  // the spool is bound at all, it is what the overwrite warning offers to
  // replace, and patchSpoolTag() reads it to decide between appending to a
  // list and migrating a UID out of the field it currently sits in.
  //
  // One size for all three rather than a tight fit per field: it keeps the row
  // indexable by TagFieldId instead of needing a switch at every use site, and
  // the list lives in PSRAM where the difference does not matter. Overlong
  // values are stored as empty rather than shortened, see the fetch below -
  // a truncated list would send the write somewhere it does not belong.
  char  tag_values[TAG_FIELD_COUNT][CARD_UIDS_MAX];
  int   filament_id;   // filament.id (for copy flow)
  float spool_weight;  // spool_weight (for copy flow)
};

// ---- in spool_flow.cpp -------------------------------------------------------

extern UnlinkedSpool* link_spools;
extern int            link_spool_count;
extern int            link_spools_capacity;
void linkSpoolsFree();

extern char link_id_input[8];

// The copy confirmation, raised from a row of the copy list or of the link
// flow's picker in copy mode, opened by the link flow's deferred handler.
extern bool  copy_confirm_pending;
extern int   copy_confirm_fid;
extern int   copy_confirm_spool_id;
extern float copy_confirm_remaining, copy_confirm_initial, copy_confirm_spool_w;
extern char  copy_confirm_name[80];

// What the copy confirmation shows of its template besides the numbers.
struct CopyLook {
  char material[16];
  char name[48];
  char vendor[32];
  char color_hex[SPOOL_COLOR_HEX_MAX];   // #RRGGBB, empty when unknown
};
extern CopyLook copy_confirm_look;
void copyLookFromRow(CopyLook& look, const UnlinkedSpool& s);

bool nameStartsWithMaterial(const char* name, const char* material);
void joinMaterialName(const char* material, const char* name, char* out, size_t out_size);
// "New from tag" on the copy entry opens the popup of spool_flow_newtag.cpp.
extern bool newtag_open_pending;

// The link flow's own state, for its other files. Each is described where it
// is defined.
extern bool      link_cu_ok;
extern lv_obj_t *scr_link_entry, *scr_link_id, *scr_link_warn_a, *scr_link_warn_b;
extern lv_obj_t *scr_link_vendor, *scr_link_mat, *scr_link_mat_sub, *scr_link_spools;
extern lv_obj_t *scr_link_confirm;
extern lv_obj_t *lbl_link_id_display, *lbl_link_id_status;
extern char link_selected_vendor[32];
extern char link_selected_material[8];
extern char link_selected_material_full[32];
extern char link_list_vendor[32];
extern char link_list_mat[8];
extern char link_list_mat_full[32];
extern bool link_stage3_shown;
extern bool link_flow_is_bambu;
extern bool copy_flow_via_list;
extern bool id_popup_is_bambu;
extern bool id_popup_is_copy;
extern int  copy_id_lookup_pending;
extern int  link_id_lookup_pending;
extern bool link_id_lookup_is_bambu;
extern bool link_material_ignored;
extern bool link_list_reloadable;
extern bool link_list_changed_note;
extern bool show_id_input_pending;
extern bool show_id_input_rebuild;
extern bool id_input_open;

// Parked by the flow's buttons for handleSpoolFlowDeferredActions().
extern bool link_patch_pending;
extern int  link_patch_id;
extern bool link_patch_bambu;
extern bool link_list_fetch_pending;
extern int  link_row_refresh_pending;
extern bool link_reload_pending;
extern bool newtag_create_pending;

// Stepping between the link lists (vendor, material, sub-material, spools) is
// parked and carried out by the loop. A row or back button used to delete the
// list it sat on and build the next one in the same callback; done
// asynchronously instead, the old list would still be in the 96 kB pool while
// the new one is built, and the spool list is cut to leave 3 kB free - not
// enough for a material list on top. So the loop deletes the old list first,
// synchronously, and builds the next one into the room that frees.
enum LinkNav {
  LNAV_NONE = 0,
  LNAV_SPOOLS_BACK,    // spool list -> material (sub) list or entry popup
  LNAV_MAT_BACK,       // material list -> vendor list
  LNAV_MAT_ROW,        // material list -> sub-material list
  LNAV_MATSUB_BACK,    // sub-material list -> material list
  LNAV_MATSUB_ROW,     // sub-material list -> spool list
  LNAV_VENDOR_ROW      // vendor list -> material list
};
extern LinkNav link_nav_pending;
extern char    link_nav_vendor[32];

// What a spool holds in its tag fields, see the definitions.
bool spoolHasAnyTag(JsonObjectConst spool);
void linkFillTagValues(UnlinkedSpool& s, JsonObjectConst spool);
bool linkSpoolSkip(const UnlinkedSpool& s);
const char* const* linkTargetValues(int spool_id);
const char* linkTargetBase(int spool_id);

// ---- in spool_flow_fetch.cpp -------------------------------------------------

// What opens once the link list is in, see linkFetchRun().
enum LinkFetchNext : uint8_t {
  LF_NEXT_NONE = 0,
  LF_NEXT_DIRECT_LIST,   // Flow A, a Bambu tag: the list itself
  LF_NEXT_VENDOR_LIST,   // Flow B: the three step picker
  LF_NEXT_RELOAD,        // the list on screen, built again at its filter level
  LF_NEXT_COPY_VENDOR    // the copy flow through the picker
};
void linkFetchRun(bool is_bambu, const char* material_filter, bool archived_only,
                  LinkFetchNext next);
void linkFetchTick();
void linkReloadSpoolList();

// A row out of the cache, read again before it is used, see linkRefreshRow().
enum LinkRowRefresh : uint8_t {
  LINK_ROW_OK = 0,     // read, the row is true again
  LINK_ROW_CHANGED,    // not what the list said: gone, archived, bound meanwhile
  LINK_ROW_NETWORK,    // no answer worth acting on, so nothing is known
};
LinkRowRefresh linkRefreshRow(int idx);

void linkPickerReset();
void linkPickerForCopy(bool archived);
void linkPickerClose();
// Shows the picker's spool list again if it is there, hidden behind a question.
void linkPickerShowList();

// ---- in spool_flow_patch.cpp -------------------------------------------------

// The link write and everything after it, see doLinkPatch(). True when the
// tag was written.
bool doLinkPatchUid(int spool_id, bool is_bambu, const char* link_uuid);
void closeLinkOverlays();
void armLinkFollowUps(int spool_id, bool is_bambu);
extern int  tagwrite_after_link_id;
extern int  secondtag_after_link_id;
extern bool nativemissing_pending;

// A link that Spoolman refused because the tag is on another spool.
extern lv_obj_t *scr_tag_move;
extern bool      tagmove_ask_pending;
extern bool      tagmove_close_pending;
extern bool      tagmove_yes_pending;
extern int       s_move_spool_id;
extern int       s_move_from_id;
// What the popup shows of each spool: id, vendor, material, name, and the
// colour as a swatch. Fetched when the question is about to be asked, so the
// user decides between two spools they can recognise, not between two ids.
struct SpoolSummary { char text[72]; char color[SPOOL_COLOR_HEX_MAX]; };
extern SpoolSummary s_move_from_sum, s_move_to_sum;
void fetchSpoolSummary(int spool_id, SpoolSummary &out);
void showTagMovePopup();
void runTagMove();

// A link made from outside, waited for this long, see s_remote_link_ms.
extern unsigned long s_remote_link_ms;
#define REMOTE_LINK_EXPECT_MS  20000UL

// ---- in spool_flow_id.cpp ----------------------------------------------------

lv_obj_t* buildLinkOverlay();

// ---- in spool_flow_list.cpp --------------------------------------------------

// Under a spool list that is kept in the cache: when it was loaded, and a
// button that loads it again. A list the cache does not hold keeps its full
// height.
#define LINK_LIST_H        264
#define LINK_STRIP_H        36
void listReloadStrip(lv_obj_t* scr, time_t at, uint32_t age_ms, lv_event_cb_t on_reload);

void addListMoreInfo(lv_obj_t* list, StringID str_id);
// What a tap on a row of the spool list leads to, once the row is true.
void linkRowChosen(int idx);

// ---- in spool_flow_picker.cpp ------------------------------------------------

// The X of a screen without a header: top right, "out to the main screen".
void flowCloseButton(lv_obj_t* parent, lv_event_cb_t cb);

// ---- in spool_flow_newtag.cpp ------------------------------------------------

extern lv_obj_t *scr_newtag;

// ---- in copy_flow.cpp and copy_confirm_popup.cpp --------------------------------------------------------

extern lv_obj_t *scr_copy_entry;     // entry screen (ID / active / archived)
extern lv_obj_t *scr_copy_list;      // spool list
extern lv_obj_t *scr_copy_confirm;   // confirm popup
extern bool      copy_flow_archived; // true = showing archived spools

void closeCopyListPopup();
void closeCopyConfirmPopup();
// Patch newly created spool with tag UID and query it on main screen
// True when the tag was bound; the spool exists either way.
bool finishCopyFlow(int new_spool_id, const char* tray_uuid_override = nullptr);
// From handleSpoolFlowDeferredActions(): the list the entry asked for, the
// spool the confirmation asked for.
void copyFlowDeferredActions();
// The confirmation's yes: create the spool from this template, from the loop.
void copyCreateRequest(int template_spool_id, int template_filament_id,
                       float template_initial, float template_spool_w);
