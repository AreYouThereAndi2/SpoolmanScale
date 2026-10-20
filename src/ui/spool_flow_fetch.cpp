#include "spool_flow.h"
#include "ui_common.h"
#include "app/app_state.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <cstring>

#include "bambu/bambu_catalog.h"
#include "bambu/material_match.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/server_reach.h"
#include "services/spool_cache.h"
#include "services/tag_field.h"
#include "services/user_options.h"
#include "services/backend_api.h"
#include "services/backend_job.h"
#include "app/backend_switch.h"
#include "ui/link_wait_card.h"
#include "ui/spool_flow_internal.h"
#include "ui/info_popup.h"
#include "ui/loading_overlay.h"
#include "services/spool_tare.h"
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

// The link list: the filter, the fetch from the cache or the backend
// worker, and the fresh read of a row out of the cache.
// Moved out of spool_flow.cpp word for word; what it shares with the rest
// of the link flow is declared in spool_flow_internal.h.

// Sort order of the link and copy lists: vendor, material, name, id, all
// case insensitive except the id. Spools without a vendor go last rather
// than first, because they are shown as "unknown" and belong at the end of
// a list, not at the top of it.
static int compareLinkSpools(const void* a, const void* b) {
  const UnlinkedSpool* x = (const UnlinkedSpool*)a;
  const UnlinkedSpool* y = (const UnlinkedSpool*)b;

  // An exact product match first, whatever its vendor or name: it is the
  // spool the tag is most likely to belong to.
  if (x->article_hit != y->article_hit) return x->article_hit ? -1 : 1;

  const bool xv = (x->vendor[0] != '\0');
  const bool yv = (y->vendor[0] != '\0');
  if (xv != yv) return xv ? -1 : 1;

  int c = strcasecmp(x->vendor, y->vendor);
  if (c != 0) return c;
  c = strcasecmp(x->material, y->material);
  if (c != 0) return c;
  c = strcasecmp(x->name, y->name);
  if (c != 0) return c;
  return (x->id > y->id) - (x->id < y->id);
}

// ============================================================
//  SPOOLMAN: LOAD ALL SPOOLS (for new link flow)
//  Loads all active spools including extra.tag status
// ============================================================
// Why a spool did not make it into the link list, or LINK_KEEP when it did.
// The reasons are separate because the caller counts them and because the
// fallback below re-runs the same test while ignoring exactly one of them.
enum LinkFilterVerdict : uint8_t {
  LINK_KEEP = 0,
  LINK_SKIP_ARCHIVED,
  LINK_SKIP_TAG,       // already bound and no second UID can be appended
  LINK_SKIP_VENDOR,    // Bambu flow, and this is not a Bambu spool
  LINK_SKIP_MATERIAL,  // material, subtype or colour did not match the tag
};

// The pre-filter, in one place. It used to live twice - once in the counting
// pass and once in the filling pass, character for character - which is a
// standing invitation for the two to drift apart.
//
// `ignore_material` drops the material, subtype and colour tests but keeps
// everything else. That is the fallback for a library whose material names no
// longer look like what the tag says: with FilamentDB imports renaming
// "PLA Tough+" to "Tough Plus", the three-character test at the heart of this
// rejects every spool, and an empty list is worse than a long one.
// The article number Bambu's catalog names for the tag on the reader, taken
// once per list; empty when there is no catalog, no hit, or no Bambu tag.
static char s_link_article[16] = "";

static bool linkArticleHit(JsonObjectConst spool) {
  if (!s_link_article[0]) return false;
  String art = spool["filament"]["article_number"] | String("");
  art.trim();
  return art.length() && strcasecmp(art.c_str(), s_link_article) == 0;
}

static LinkFilterVerdict linkFilterVerdict(JsonObjectConst spool, bool is_bambu,
                                           const char* material_filter,
                                           bool archived_only, bool ignore_material) {
  const bool sp_archived = spool["archived"] | false;
  if (archived_only) { if (!sp_archived) return LINK_SKIP_ARCHIVED; }
  else               { if (sp_archived)  return LINK_SKIP_ARCHIVED; }

  // Skip already-linked spools - only in the normal link flow. In the
  // copy-archived flow archived spools are templates and typically still
  // tagged, so they must stay. While a second UID can be appended, an already
  // bound spool stays in the list whichever field binds it: that is the only
  // way to add the tag on the other flange from the scale, and WarnPopupA
  // catches the selection before anything is written.
  // Nor in the copy flow at all: a spool that already has its tag is the usual
  // template - the one on the shelf that is being bought again - and the
  // Bambu copy list never hid it either.
  if (!archived_only && !copy_flow_via_list && !link_cu_ok && spoolHasAnyTag(spool))
    return LINK_SKIP_TAG;

  if (!is_bambu) return LINK_KEEP;

  String vname = "";
  if (!spool["filament"]["vendor"].isNull())
    vname = spool["filament"]["vendor"]["name"] | String("");
  vname.trim();
  if (strncasecmp(vname.c_str(), "Bambu", 5) != 0) return LINK_SKIP_VENDOR;

  // The article number names product and colour at once, so it settles what
  // material, subtype and colour below would only approximate: a FilaMan
  // designation "Cyan (12601)" names no subtype at all.
  if (linkArticleHit(spool)) return LINK_KEEP;

  if (ignore_material || !material_filter || !material_filter[0]) return LINK_KEEP;

  String mat = spool["filament"]["material"] | String("");
  mat.trim();

  // Support materials: match Spoolman materials ending in "-S" (PLA-S, ABS-S)
  if (isSupportMaterial(material_filter)) {
    if (!isSupportSpoolmanMat(mat.c_str())) return LINK_SKIP_MATERIAL;
    return LINK_KEEP;   // no colour filter for support filaments
  }

  // Standard 3-char material prefix match ("PLA", "PET", "ABS")
  if (strncasecmp(mat.c_str(), material_filter, 3) != 0) return LINK_SKIP_MATERIAL;
  // Exclude support materials from a non-support filter (ABS-GF must not show ABS-S)
  if (isSupportSpoolmanMat(mat.c_str())) return LINK_SKIP_MATERIAL;

  // Subtype filter: only for known technical subtypes, see bambu_blacklist.h
  //
  // Three places are asked, because three spellings are in circulation for the
  // same thing: the material, the designation, and FilaMan's material_subgroup
  // - the last one is what identifies a spool whose designation is only a
  // colour. bambuSubtypeMatches() reads "Tough+", "Tough Plus" and "tough-plus"
  // as one, which is what the tag and the FilamentDB actually spell.
  char subkw[16];
  if (extractBambuSubtype(material_filter, subkw, sizeof(subkw))) {
    String fname = spool["filament"]["name"] | String("");
    String fsub  = spool["filament"]["material_subgroup"] | String("");
    if (!bambuSubtypeMatches(mat.c_str(), subkw) &&
        !bambuSubtypeMatches(fname.c_str(), subkw) &&
        !bambuSubtypeMatches(fsub.c_str(), subkw)) {
      if (sd_verbose)
        logSDf("[verbose] link fetch: subtype skip mat='%s' name='%.20s' sub='%.16s' kw='%s'",
               mat.c_str(), fname.c_str(), fsub.c_str(), subkw);
      return LINK_SKIP_MATERIAL;
    }
  }

  // Colour filter: a tag that names a colour skips spools far away from it.
  // A clear filament names none - its color_hex stays empty - and is matched
  // on material and subtype alone, like a support filament. So is a spool
  // without a colour of its own: a multi-colour filament keeps its colours
  // in multi_color_hexes and leaves color_hex empty, and "#" alone used to
  // measure as far away from everything.
  if (g_tag.color_hex[0] == '#') {
    const char* col = spool["filament"]["color_hex"] | "";
    if (col[0] == '#') col++;   // a server that sends it with the '#'
    if (col[0]) {
      char col_buf[8]; snprintf(col_buf, sizeof(col_buf), "#%s", col);
      if (colorDistance(g_tag.color_hex, col_buf) > 120) return LINK_SKIP_MATERIAL;
    }
  }
  return LINK_KEEP;
}

static bool linkFetchBuild(JsonDocument& doc, bool from_cache, bool is_bambu,
                           const char* material_filter, bool archived_only);
static void linkFetchTryStart();

// A link list on its way from the backend worker, see fetchAllSpoolsForLink().
enum LinkFetchState : uint8_t { LF_IDLE, LF_WAIT_SLOT, LF_LOADING };
static LinkFetchState s_lf_state      = LF_IDLE;
static bool           s_lf_cancelled  = false;
static bool           s_lf_is_bambu   = false;
static char           s_lf_material[24] = "";
static bool           s_lf_archived   = false;
static bool           s_lf_may_cache  = false;
static bool           s_lf_have_stamp = false;
static InventoryStamp s_lf_stamp      = { -1, 0 };
static JsonDocument   s_lf_filter;

LinkFetch fetchAllSpoolsForLink(bool is_bambu, const char* material_filter, bool archived_only) {
  crumbSet("link fetch");
  // Free any previous allocation
  linkSpoolsFree();
  // Cleared with the list, not further down where the fallback decides: every
  // return between here and there would otherwise leave the last fetch's
  // verdict standing, and the next list would drop its material filter for no
  // reason and say so.
  link_material_ignored = false;
  link_list_reloadable  = false;
  if (!wifi_ok) return LINK_FETCH_FAILED;

  // Settled here, once, for every decision the flow makes afterwards. Offering
  // an already bound spool the scale then could not append to would put the
  // second UID straight on top of the first, which is what this guards.
  //
  // All three inputs go into the log: when a bound spool does not turn up in
  // the list, this line is what says which of them said no.
  { const bool is_list = tagFieldIsList();
    const bool present = is_list && backendHasExtraField(tagFieldKey());
    link_cu_ok = is_list && g_card_uids_write && present;
    logSDf("link fetch: append=%d (field=%s list=%d write=%d present=%d)",
           link_cu_ok ? 1 : 0, tagFieldKeyName(), is_list ? 1 : 0,
           g_card_uids_write ? 1 : 0, present ? 1 : 0); }

  logSDf("link fetch: is_bambu=%d material_filter='%s' archived_only=%d",
    is_bambu, material_filter ? material_filter : "", (int)archived_only);

  s_link_article[0] = '\0';
  BambuCatalogHit hit;
  if (is_bambu && bambuCatalogFind(g_tag.material_id, g_tag.material_variant_id,
                                   g_tag.color, &hit)) {
    snprintf(s_link_article, sizeof(s_link_article), "%s", hit.article);
    logSDf("link fetch: catalog names article %s (%s %s)", s_link_article,
           hit.product, hit.color_name);
  }

  JsonDocument filterL;
  JsonArray filterL_arr = filterL.to<JsonArray>();
  JsonObject fL = filterL_arr.add<JsonObject>();
  fL["id"] = true;
  fL["archived"] = true;
  fL["remaining_weight"] = true;
  // Every tag field: a spool can be bound through any of them, and the link
  // flow has to see that whichever one the user has selected right now.
  for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++)
    fL["extra"][tagFieldSpec(f).key] = true;
  fL["filament"]["id"] = true;
  fL["filament"]["name"] = true;
  fL["filament"]["material"] = true;
  fL["filament"]["weight"] = true;
  fL["filament"]["color_hex"] = true;
  fL["filament"]["vendor"]["name"] = true;
  fL["filament"]["article_number"] = true;
  fL["spool_weight"] = true;
  // The two levels below the spool's own tare, for spoolTare(): a copy built
  // from a spool without one would otherwise book the core as filament.
  fL["filament"]["spool_weight"] = true;
  fL["filament"]["vendor"]["empty_spool_weight"] = true;
  if (filterL.overflowed())
    logSD("link fetch: filter overflowed, fields will be missing");
  SpiRamAllocator psram_alloc;
  JsonDocument doc(&psram_alloc);

  // ---- from the copy kept in PSRAM, if it can be proven ---------------------
  //
  // The stamp first and exactly once: it either proves the copy, or it becomes
  // the stamp of the download that follows. Never taken after the download -
  // see spoolCacheFill().
  //
  // The cache must never be what a write is built on. For a link that is
  // linkRefreshRow(), which reads a row out of the cache again before its
  // question opens. A copy through the picker goes through the very same
  // tap, so it may be served from the cache as well: the template's weights
  // and filament are read fresh before the copy confirmation shows them.
  // The archive is not in the cache.
  const bool may_cache = !archived_only;
  // Known from here on, for the fresh read of a row out of the cache and for
  // a reload: an archive list is the archive, whichever way it came.
  s_lf_archived = archived_only;
  InventoryStamp stamp = { -1, 0 };
  bool have_stamp = false;
  bool from_cache = false;
  // Taken for the archive as well now: it has a cache of its own.
  {
    const uint32_t t0 = millis();
    const int sc = backendInventoryStamp(cfg_spoolman_base, &stamp);
    logSDf("link fetch: stamp code=%d count=%d witness=%d (%lu ms)",
           sc, stamp.count, stamp.witness_id, (unsigned long)(millis() - t0));
    // The server is gone. The download would only find that out again, eight
    // seconds later and behind a loading overlay.
    if (serverReachIsNetworkFailure(sc)) {
      serverReachNote(sc, true);
      return LINK_FETCH_FAILED;
    }
    have_stamp = (sc == 200);
    // The placeholder of a bound spool goes under the first tag field. Which
    // one does not matter: spoolHasAnyTag() asks all of them.
    if (may_cache && spoolCacheUsable(have_stamp ? &stamp : nullptr))
      from_cache = spoolCacheToJson(doc, tagFieldSpec(0).key);
    else if (archived_only && spoolCacheArchiveUsable(have_stamp ? &stamp : nullptr))
      from_cache = spoolCacheArchiveToJson(doc);
  }

  // ---- or off the wire, on the backend worker -------------------------------
  //
  // The loop keeps running while it comes in: the card counts the kilobytes
  // and offers Cancel. linkFetchTick() takes the list when it is in.
  if (!from_cache) {
    s_lf_filter.set(filterL.as<JsonVariantConst>());
    s_lf_is_bambu   = is_bambu;
    snprintf(s_lf_material, sizeof(s_lf_material), "%s", material_filter ? material_filter : "");
    s_lf_archived   = archived_only;
    s_lf_may_cache  = may_cache;
    s_lf_have_stamp = have_stamp;
    s_lf_stamp      = stamp;
    s_lf_cancelled  = false;
    s_lf_state      = LF_WAIT_SLOT;
    linkWaitCardShow();
    linkFetchTryStart();
    return LINK_FETCH_PENDING;
  }
  return linkFetchBuild(doc, true, is_bambu, material_filter, archived_only)
           ? LINK_FETCH_DONE : LINK_FETCH_FAILED;
}

// Everything after the list is in, whichever way it came: the two filter
// passes, the rows in PSRAM, the sort.
static bool linkFetchBuild(JsonDocument& doc, bool from_cache, bool is_bambu,
                           const char* material_filter, bool archived_only) {
  // A download may be kept exactly when a list out of the cache could have
  // been served: not the archive, not the copy flow.
  link_list_reloadable = from_cache || s_lf_may_cache;
  // The line that says afterwards whether the cache was hit.
  logSDf("link fetch: list %s", from_cache ? "from cache" : "downloaded");

  JsonArray spools = doc.as<JsonArray>();
  int total_in_api = 0;
  int skipped_tag = 0, skipped_vendor = 0, skipped_material = 0;
  int count_bambu = 0, count_linked = 0;

  // ── Pass 1: count matching spools (pre-filter) ──────────────
  //
  // Run twice when the first pass finds nothing: a library whose material
  // names no longer resemble what the tag says would otherwise produce an
  // empty list and a dead end. The second run keeps the vendor and the
  // already-bound tests and drops only material, subtype and colour. It costs
  // nothing - the same document, already parsed, walked once more.
  int matched = 0;
  int skipped_archived = 0;
  bool material_ignored = false;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (attempt == 1) {
      // Nothing matched, and the material test is the only thing that could
      // have been too strict. Without a material filter there is nothing to
      // relax, so there is no second attempt.
      if (matched > 0 || !is_bambu || !material_filter || !material_filter[0]) break;
      material_ignored = true;
      link_material_ignored = true;
      total_in_api = 0; skipped_archived = 0; skipped_tag = 0;
      skipped_vendor = 0; skipped_material = 0; count_linked = 0; count_bambu = 0;
      logSDf("link fetch: nothing matched '%s', retrying without the material filter",
             material_filter);
    }
    for (JsonObject spool : spools) {
      total_in_api++;
      String vname_c = "";
      if (!spool["filament"]["vendor"].isNull())
        vname_c = spool["filament"]["vendor"]["name"] | String("");
      vname_c.trim();
      if (strncasecmp(vname_c.c_str(), "Bambu", 5) == 0) count_bambu++;

      switch (linkFilterVerdict(spool, is_bambu, material_filter, archived_only,
                                material_ignored)) {
        case LINK_SKIP_ARCHIVED: skipped_archived++; continue;
        case LINK_SKIP_TAG:      skipped_tag++; count_linked++; continue;
        case LINK_SKIP_VENDOR:   skipped_vendor++; continue;
        case LINK_SKIP_MATERIAL: skipped_material++; continue;
        default: break;
      }
      matched++;
    }
  }

  { char buf[48];
    snprintf(buf, sizeof(buf), T(STR_LOADING_FILTER), total_in_api);
    loadingOverlaySetText(buf); }

  logSDf("Inventory: %d total | %d linked | %d unlinked | %d Bambu",
    total_in_api, count_linked, total_in_api - count_linked, count_bambu);
  Serial.printf("Inventory: %d total | %d linked | %d unlinked | %d Bambu\n",
    total_in_api, count_linked, total_in_api - count_linked, count_bambu);
  logSDf("link fetch: total=%d matched=%d (skip_tag=%d skip_vendor=%d skip_mat=%d)",
    total_in_api, matched, skipped_tag, skipped_vendor, skipped_material);
  Serial.printf("link fetch: total=%d matched=%d (skip_tag=%d skip_vendor=%d skip_mat=%d)\n",
    total_in_api, matched, skipped_tag, skipped_vendor, skipped_material);

  if (matched == 0) { loadingOverlayHide(); return true; }

  // Store ALL matched spools - the display limit is applied at render time (showFilteredSpoolList)
  // This allows Vendor and Material lists to see the full dataset
  int alloc_count = matched;
  logSDf("link fetch: matched=%d, allocating all for vendor/material dedupe", matched);

  // ── Allocate exactly the needed size in PSRAM ───────────────
  link_spools = (UnlinkedSpool*)heap_caps_malloc(alloc_count * sizeof(UnlinkedSpool), MALLOC_CAP_SPIRAM);
  if (!link_spools) {
    // Fallback: internal RAM
    link_spools = (UnlinkedSpool*)malloc(alloc_count * sizeof(UnlinkedSpool));
    logSD("link fetch: PSRAM alloc failed, using internal RAM");
  }
  if (!link_spools) { logSD("link fetch: alloc failed completely"); loadingOverlayHide(); return true; }
  link_spools_capacity = alloc_count;

  // ── Pass 2: fill array (same filter, same verdict) ─────────
  // material_ignored carries the first pass's decision, so the rows are
  // exactly the spools that were counted.
  for (JsonObject spool : spools) {
    if (link_spool_count >= alloc_count) break;
    if (linkFilterVerdict(spool, is_bambu, material_filter, archived_only,
                          material_ignored) != LINK_KEEP) continue;

    UnlinkedSpool &s = link_spools[link_spool_count];
    s.id = spool["id"] | 0;

    // A row out of the cache carries no tag values, only the placeholder that
    // made the filter above answer as it did for the original. That must not
    // land in the row: a write would take it for a UID to append to.
    s.from_cache = from_cache;
    s.article_hit = linkArticleHit(spool);
    if (from_cache) {
      for (uint8_t f = 0; f < TAG_FIELD_EXTRA_COUNT; f++) s.tag_values[f][0] = '\0';
    } else {
      linkFillTagValues(s, spool);
    }

    String fname = spool["filament"]["name"] | String("?");
    fname.trim();
    strncpy(s.name, fname.c_str(), sizeof(s.name)-1);
    s.name[sizeof(s.name)-1] = '\0';

    // Read here rather than carried over from the filter: the vendor object is
    // optional and a spool without one keeps an empty string, which the vendor
    // list turns into its "unknown" row.
    String vname = "";
    if (!spool["filament"]["vendor"].isNull())
      vname = spool["filament"]["vendor"]["name"] | String("");
    vname.trim();
    strncpy(s.vendor, vname.c_str(), sizeof(s.vendor)-1);
    s.vendor[sizeof(s.vendor)-1] = '\0';

    String mat = spool["filament"]["material"] | String("");
    mat.trim();
    strncpy(s.material, mat.c_str(), sizeof(s.material)-1);
    s.material[sizeof(s.material)-1] = '\0';

    String col = spool["filament"]["color_hex"] | String("");
    col.trim();
    if (col.length() > 0 && col[0] != '#') col = "#" + col;
    strncpy(s.color_hex, col.c_str(), sizeof(s.color_hex)-1);
    s.color_hex[sizeof(s.color_hex)-1] = '\0';

    s.remaining = spool["remaining_weight"] | 0.0f;
    s.total = spool["filament"]["weight"] | 1000.0f;
    s.filament_id = spool["filament"]["id"] | 0;
    s.spool_weight = spoolTare(spool);

    // On Serial, not on the card. A line to the card is an open, an append and
    // a close, 26 ms each on the loop task: for an inventory of 227 spools that
    // was 6 s of a frozen screen after a download that took one, and a third
    // of a day's log. The card keeps the totals written above and below.
    if (sd_verbose) {
      Serial.printf("link spool %d: vendor='%s' mat='%s' name='%s' fid=%d spw=%.0f\n",
        s.id, s.vendor, s.material, s.name, s.filament_id, s.spool_weight);
    }

    link_spool_count++;
  }
  // One sort puts all three levels of the link flow in order. The vendor and
  // material lists are built from the order of first appearance in this
  // array, so they inherit whatever order it has, and that used to be
  // whatever the server happened to return. Spoolman answers in database
  // order, FilaMan in its own, and neither is meaningful to a user.
  //
  // vendor, then material, then name, then id. Where the name is already
  // fixed, which is the case in the final list, that leaves plain ascending
  // ids.
  qsort(link_spools, link_spool_count, sizeof(UnlinkedSpool), compareLinkSpools);

  loadingOverlayHide();
  Serial.printf("fetchAllSpoolsForLink: %d spools loaded (PSRAM, sorted)\n", link_spool_count);
  logSDf("link fetch done: %d spools in list", link_spool_count);
  return true;
}

// ============================================================
//  THE LINK LIST ON THE BACKEND WORKER
//
//  What opens once the list is in depends on the way into it, so each caller
//  hands that over instead of acting on a return value: the list may come in
//  passes later. Cancel on the card ends the wait, not the download - the
//  list still comes in and fills the spool cache, so the next "Link" is
//  instant.
// ============================================================
// The link list always had eight seconds.
#define LINK_FETCH_TIMEOUT_MS  8000

static LinkFetchNext s_lf_next = LF_NEXT_NONE;
// The filter level of a reload, copied: showFilteredSpoolList() writes into
// the buffers they come from.
static char s_lf_vendor[sizeof(link_list_vendor)]     = "";
static char s_lf_mat[sizeof(link_list_mat)]           = "";
static char s_lf_mat_full[sizeof(link_list_mat_full)] = "";
// "Link" pressed again while a cancelled list is still coming in: run once it
// is in, which then finds it in the cache.
static bool          s_lf_queued          = false;
static bool          s_lf_queued_bambu    = false;
static char          s_lf_queued_mat[sizeof(s_lf_material)] = "";
static bool          s_lf_queued_archived = false;

static void linkFetchFinish(bool ok) {
  const LinkFetchNext next = s_lf_next;
  s_lf_next = LF_NEXT_NONE;
  switch (next) {
    case LF_NEXT_DIRECT_LIST:
      if (ok) showFilteredSpoolList("", "", "");
      break;
    case LF_NEXT_VENDOR_LIST:
      if (ok) showVendorList();
      break;
    case LF_NEXT_RELOAD:
      // No list, and the pickers underneath were built from the one that is
      // gone. The flow ends here rather than on screens that point into
      // nothing.
      if (ok) {
        showFilteredSpoolList(s_lf_vendor, s_lf_mat, s_lf_mat_full);
      } else {
        link_list_changed_note = false;
        closeLinkOverlays();
      }
      break;
    case LF_NEXT_COPY_VENDOR:
      // Without a picker there is no copy flow either. Left set, the flag
      // would turn the next plain link through the same picker into a copy.
      if (ok) showVendorList();
      else    copy_flow_via_list = false;
      break;
    default:
      break;
  }
}

// Loads the list and opens `next` on it: at once when it came out of the
// cache, from linkFetchTick() when the worker had to fetch it. A list that
// never arrived is not an empty one: the popup says why, and nothing opens.
void linkFetchRun(bool is_bambu, const char* material_filter, bool archived_only,
                         LinkFetchNext next) {
  if (s_lf_state == LF_LOADING && s_lf_cancelled) {
    s_lf_queued          = true;
    s_lf_queued_bambu    = is_bambu;
    snprintf(s_lf_queued_mat, sizeof(s_lf_queued_mat), "%s", material_filter ? material_filter : "");
    s_lf_queued_archived = archived_only;
    s_lf_next            = next;
    linkWaitCardShow();
    return;
  }
  if (s_lf_state != LF_IDLE) return;   // the card is up, it swallows the second tap
  s_lf_next = next;
  const LinkFetch f = fetchAllSpoolsForLink(is_bambu, material_filter, archived_only);
  if (f != LINK_FETCH_PENDING) linkFetchFinish(f == LINK_FETCH_DONE);
}

// The worker is free: take the list from the cache if a scan has just filled
// it, otherwise fetch it. Retried on every pass while it cannot start.
static void linkFetchTryStart() {
  if (s_lf_state != LF_WAIT_SLOT) return;
  if (backendListBusy() || backendJobState() != BJS_IDLE) return;

  // What held the worker was most likely this very inventory: the scan of the
  // unknown tag on the pad, which fills the spool cache as it comes in.
  if (s_lf_may_cache && spoolCacheUsable(s_lf_have_stamp ? &s_lf_stamp : nullptr)) {
    SpiRamAllocator psram_alloc;
    JsonDocument doc(&psram_alloc);
    if (spoolCacheToJson(doc, tagFieldSpec(0).key)) {
      logSD("link fetch: list from the cache a scan has just filled");
      s_lf_state = LF_IDLE;
      linkWaitCardHide();
      linkFetchFinish(linkFetchBuild(doc, true, s_lf_is_bambu, s_lf_material, s_lf_archived));
      return;
    }
  }
  if (!backendJobStartList(s_lf_archived, &s_lf_filter, LINK_FETCH_TIMEOUT_MS, 1, 0)) return;
  s_lf_state = LF_LOADING;
}

void linkFetchTick() {
  if (s_lf_state == LF_IDLE) return;

  // Only while the card is this flow's: after a cancel, with the list still
  // coming in, it may already be standing for the copy list.
  const bool card_is_ours = (s_lf_state == LF_WAIT_SLOT || !s_lf_cancelled || s_lf_queued);
  if (card_is_ours && linkWaitCardCancelTake()) {
    linkWaitCardHide();
    if (s_lf_state == LF_LOADING && !s_lf_cancelled) {
      logSD("link fetch: cancelled, the list still comes in for the cache");
      s_lf_cancelled = true;
    } else if (s_lf_state == LF_WAIT_SLOT) {
      logSD("link fetch: cancelled while waiting for the worker");
      s_lf_state = LF_IDLE;
    } else {
      s_lf_queued = false;   // the second "Link", cancelled as well
    }
    linkFetchFinish(false);
    return;
  }

  // Whichever list is coming in, this one or the scan it waits for.
  if (card_is_ours) linkWaitCardBytes(backendJobBytes());
  if (s_lf_state == LF_WAIT_SLOT) { linkFetchTryStart(); return; }
  if (backendJobState() != BJS_DONE) return;

  const BackendListResult r = backendJobResult();
  JsonDocument& doc = backendJobDoc();
  const bool cancelled = s_lf_cancelled;
  s_lf_state     = LF_IDLE;
  s_lf_cancelled = false;

  if (r.gen != backendGeneration()) {
    backendJobTake();
    logSD("link fetch: the list is from before a backend switch, dropped");
    s_lf_queued = false;
    linkWaitCardHide();
    linkFetchFinish(false);
    return;
  }
  const bool ok = (r.code == 200 && !r.err);
  // Not a list FilaMan gave up on halfway: its stamp would vouch for spools
  // that were never read.
  // An archive list carries the active spools too, so it fills both.
  if (ok && !r.partial) {
    spoolCacheFill(doc.as<JsonArrayConst>(), spoolHasAnyTag,
                   s_lf_have_stamp ? &s_lf_stamp : nullptr);
    if (s_lf_archived)
      spoolCacheArchiveFill(doc.as<JsonArrayConst>(), s_lf_have_stamp ? &s_lf_stamp : nullptr);
  }

  if (cancelled) {
    backendJobTake();
    if (s_lf_queued) {
      s_lf_queued = false;
      const LinkFetchNext next = s_lf_next;
      linkWaitCardHide();
      linkFetchRun(s_lf_queued_bambu, s_lf_queued_mat, s_lf_queued_archived, next);
    }
    return;
  }

  linkWaitCardHide();
  if (!ok) {
    backendJobTake();
    serverReachNote(r.code, true);
    linkFetchFinish(false);
    return;
  }
  const bool built = linkFetchBuild(doc, false, s_lf_is_bambu, s_lf_material, s_lf_archived);
  backendJobTake();
  linkFetchFinish(built);
}

// The picker starts from its first level, for a link and for a copy alike.
void linkPickerReset() {
  link_selected_material[0] = 0;
  link_selected_material_full[0] = 0;
  link_stage3_shown = false;
}

// An NTAG copy goes through the vendor and material picker of the link flow;
// the row it ends on opens the copy confirmation instead of a link.
void linkPickerForCopy(bool archived) {
  copy_flow_via_list = true;
  link_flow_is_bambu = false;
  linkPickerReset();
  linkFetchRun(false, "", archived, LF_NEXT_COPY_VENDOR);
}

void linkPickerShowList() {
  if (scr_link_spools) lv_obj_clear_flag(scr_link_spools, LV_OBJ_FLAG_HIDDEN);
}

// The picker's screens and its copy mode, once a copy through it is done.
void linkPickerClose() {
  copy_flow_via_list = false;
  closeLinkOverlays();
}

// The spool list on screen, fetched and built again at the same filter level.
// From the loop only: it reaches the network and deletes a screen.
void linkReloadSpoolList() {
  // Copies, because showFilteredSpoolList() writes into the buffers these
  // come from.
  char vendor[sizeof(link_list_vendor)], mat[sizeof(link_list_mat)], mat_full[sizeof(link_list_mat_full)];
  memcpy(vendor,   link_list_vendor,   sizeof(vendor));
  memcpy(mat,      link_list_mat,      sizeof(mat));
  memcpy(mat_full, link_list_mat_full, sizeof(mat_full));

  // The rows hold indexes into the array the fetch is about to free.
  if (scr_link_spools) { lv_obj_del(scr_link_spools); scr_link_spools = nullptr; }

  memcpy(s_lf_vendor,   vendor,   sizeof(s_lf_vendor));
  memcpy(s_lf_mat,      mat,      sizeof(s_lf_mat));
  memcpy(s_lf_mat_full, mat_full, sizeof(s_lf_mat_full));
  // The list it rebuilds: the archive stays the archive.
  linkFetchRun(link_flow_is_bambu, link_flow_is_bambu ? g_tag.material : "", s_lf_archived,
               LF_NEXT_RELOAD);
}

// ============================================================
//  A ROW OUT OF THE CACHE IS READ AGAIN BEFORE IT IS USED
// ============================================================
// How long a tapped row may hold the loop while it is read. Shorter than the
// usual five seconds: this runs without a loading overlay. It covers the read
// only - the connect has the client's own five seconds, as everywhere else.
#define LINK_ROW_REFRESH_TIMEOUT_MS 3000

// The cache decides what a list shows, never what is written. Between a row
// out of the cache and anything that describes it or writes to it stands this:
// the spool is read from the server, the row gets its real tag values and its
// numbers, and from_cache is cleared.
//
// Two callers. The tap on a row, before the confirmation opens - that is the
// place that matters, because what the dialogs then show (the grams, and in
// the warning behind them the number of UIDs the spool already holds) is what
// the write is built on. And doLinkPatch(), as the bolt on the one door every
// write goes through.
//
// Reaches the network: from the loop only, never from an event callback.
LinkRowRefresh linkRefreshRow(int idx) {
  if (!link_spools || idx < 0 || idx >= link_spool_count) return LINK_ROW_CHANGED;
  UnlinkedSpool &s = link_spools[idx];

  JsonDocument doc;
  DeserializationError err = DeserializationError::Ok;
  const uint32_t t0 = millis();
  const int code = serverReachNote(
      backendGetSpoolJson(cfg_spoolman_base, s.id, doc, LINK_ROW_REFRESH_TIMEOUT_MS, &err), true);
  const unsigned long took = (unsigned long)(millis() - t0);

  // The server did not answer. serverReachNote() has asked for the popup; the
  // cache is kept, because nothing was learned that speaks against it.
  if (serverReachIsNetworkFailure(code)) {
    logSDf("link row: spool %d not read, no connection (%d, %lu ms)", s.id, code, took);
    return LINK_ROW_NETWORK;
  }
  if (code == 404) {
    logSDf("link row: spool %d is gone (%lu ms)", s.id, took);
    spoolCacheForget("a listed spool is gone");
    return LINK_ROW_CHANGED;
  }
  // Anything else that is not a spool: a 500, a body that did not parse. Not
  // a statement about the spool, so the list stays - but it has to be said,
  // or the tap looks as if it had not been felt.
  if (code != 200 || err) {
    logSDf("link row: spool %d not read, HTTP %d %s (%lu ms)", s.id, code,
           err ? err.c_str() : "", took);
    showInfoPopup(STR_SERVER_DOWN_TITLE, STR_SERVER_DOWN_TEXT, INFO_WARN);
    return LINK_ROW_NETWORK;
  }

  JsonObjectConst spool = doc.as<JsonObjectConst>();
  // Not where the list put it: archived meanwhile, or - on the archive list of
  // the copy flow - brought back.
  if ((spool["archived"] | false) != s_lf_archived) {
    logSDf("link row: spool %d was %s meanwhile (%lu ms)", s.id,
           s_lf_archived ? "brought back" : "archived", took);
    spoolCacheForget("a listed spool changed its archive state");
    return LINK_ROW_CHANGED;
  }
  const bool bound = spoolHasAnyTag(spool);
  if (bound && !link_cu_ok && !copy_flow_via_list) {
    // It would never have been in this list. Only this one row of the cache
    // was wrong, so only that is put right: the list that follows comes out of
    // the cache again, without the spool.
    logSDf("link row: spool %d was bound meanwhile (%lu ms)", s.id, took);
    spoolCacheSetBound(s.id, true);
    return LINK_ROW_CHANGED;
  }

  linkFillTagValues(s, spool);
  s.remaining    = spool["remaining_weight"] | 0.0f;
  s.total        = spool["filament"]["weight"] | 1000.0f;
  s.filament_id  = spool["filament"]["id"] | 0;
  s.spool_weight = spoolTare(spool);
  s.from_cache   = false;
  spoolCacheSetBound(s.id, bound);
  spoolCacheSetRemaining(s.id, s.remaining);
  logSDf("link row: spool %d read fresh, bound=%d (%lu ms)", s.id, bound ? 1 : 0, took);
  return LINK_ROW_OK;
}
