#include "scan_action.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>

#include "app/app_loop.h"
#include "app/app_state.h"
#include "hardware/barcode_scanner.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/bambuddy_api.h"
#include "services/f2b_api.h"
#include "services/scan_code.h"
#include "services/server_reach.h"
#include "services/spool_cache.h"
#include "services/spoolman_api.h"
#include "services/spool_extras.h"
#include "ui/main_screen_helpers.h"
#include "ui/spool_flow.h"
#include "ui/question_box.h"
#include "ui/spoolman_lookup.h"
#include "ui/ui_common.h"
#include "ui/theme.h"

// Below this the pad counts as empty: the same floor the location question
// uses, and well under the lightest spool core anyone weighs.
static constexpr float SCAN_LOAD_MIN_G = 50.0f;

// A code no spool answers to, offered as a new spool from what
// filament_to_bambuddy knows about it. One question at a time.
static lv_obj_t*        s_ns_scr = nullptr;
static volatile uint8_t s_ns_answer = QB_NONE;
static F2bProduct       s_ns;

static void handleSpoolId(int spool_id);

// The Spoolman server an article number can be asked of: the backend itself,
// or the one BamBuddy keeps its inventory in. Empty when there is none -
// FilaMan and BamBuddy's own inventory have no article number to match.
static const char* scanSpoolmanBase() {
  switch (backendMode()) {
    case BACKEND_SPOOLMAN:
      return cfg_spoolman_base;
    case BACKEND_BAMBUDDY:
      return (bbInventoryMode() == BB_INV_SPOOLMAN) ? bbSpoolmanUrl() : "";
    default:
      return "";
  }
}

static void scanStatus(uint32_t color, int id, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, id);
  vsnprintf(buf, sizeof(buf), T(id), ap);
  va_end(ap);
  statusMessageShow(buf, color);
}

// The empty spool weight a candidate is weighed against: its own, else its
// filament's, else its maker's. The same order the main screen resolves.
static float candidateTare(JsonObjectConst s) {
  float w = s["spool_weight"] | 0.0f;
  if (w > 0) return w;
  w = s["filament"]["spool_weight"] | 0.0f;
  if (w > 0) return w;
  return s["filament"]["vendor"]["empty_spool_weight"] | 0.0f;
}

// Who a failed request went to, for the reason the status line gives.
enum ScanPeer { PEER_F2B, PEER_SPOOLMAN, PEER_BACKEND };

// What an HTTP code means in words: a transport error by whom could not be
// reached, a status by what the user can do about it. -2 from the app is an
// answer that did not parse.
static int scanWhy(int code, ScanPeer peer) {
  if (code == -11) return peer == PEER_F2B ? STR_WHY_F2B_SLOW : peer == PEER_SPOOLMAN ? STR_WHY_SM_NET : STR_WHY_BB_NET;
  if (code == -2 && peer == PEER_F2B) return STR_WHY_ANSWER;
  if (code < 0) return peer == PEER_F2B ? STR_WHY_F2B_NET : peer == PEER_SPOOLMAN ? STR_WHY_SM_NET : STR_WHY_BB_NET;
  if (code == 401 || code == 403) return STR_WHY_AUTH;
  if (code == 400) return STR_WHY_MODE;
  if (code == 404) return STR_WHY_NOT_FOUND;
  if (code == 503) return STR_WHY_SM_DOWN;
  return STR_WHY_SERVER;
}

static void scanFailed(int fmt, int code, ScanPeer peer) {
  scanStatus(UI_COL_BAD_TEXT, fmt, T(scanWhy(code, peer)), code);
}

static void handleSpoolId(int spool_id) {
  logSDf("Scan: spool %d", spool_id);
  // An unknown tag on the reader, or the link flow open for one: the scan
  // answers "which spool is this", exactly as the keypad would.
  const bool unknown_tag = tag_present && !sm_found && !lookupPending();
  if (spoolFlowTakeScannedId(spool_id, unknown_tag)) {
    scanStatus(UI_COL_GOOD, STR_SCAN_LINKING, spool_id);
    return;
  }
  if (tag_present) {
    scanStatus(UI_COL_WARN, STR_SCAN_TAG_WINS, spool_id);
    return;
  }
  if (appAdoptSpoolForWeighing(spool_id)) {
    scanStatus(UI_COL_GOOD, STR_SCAN_SPOOL_LOADED, spool_id);
  } else {
    scanStatus(UI_COL_BAD_TEXT, STR_SCAN_SPOOL_MISSING, spool_id);
  }
}

// Several spools of the same filament: the one whose stored remaining weight
// is closest to what lies on the pad. 0 when the pad is empty.
static int pickByWeight(JsonArrayConst spools, float* out_diff) {
  if (!scale_ready || scale_weight_g < SCAN_LOAD_MIN_G) return 0;
  int best = 0;
  float best_diff = 1e9f;
  for (JsonObjectConst s : spools) {
    const int id = s["id"] | 0;
    if (id <= 0) continue;
    const float net = spoolExtrasBareReading() - candidateTare(s);
    const float diff = fabsf(net - (float)(s["remaining_weight"] | 0.0f));
    if (diff < best_diff) { best_diff = diff; best = id; }
  }
  if (out_diff) *out_diff = best_diff;
  return best;
}

// Looks the code up in filament_to_bambuddy and, when it knows the product,
// asks whether to create the spool. Without an address, or for a product
// nobody knows, the status line says what to do instead.
static void offerNewSpool(const char* code, int filament_id = 0) {
  if (!g_f2b_url[0]) {
    scanStatus(UI_COL_WARN, STR_SCAN_UNKNOWN_ARTICLE, code);
    return;
  }
  if (!backendCanCreateFromTag()) {
    scanStatus(UI_COL_WARN, STR_NS_ONLY_BB, 0);
    return;
  }
  scanStatus(UI_COL_GOOD, STR_NS_LOOKING, code);
  lv_timer_handler();   // the lookup can take seconds on a cold cache
  const int code_http = f2bLookup(code, s_ns);
  if (code_http != 200) {
    scanFailed(STR_SCAN_FAIL_WHY, code_http, PEER_F2B);
    return;
  }
  if (!s_ns.found) {
    scanStatus(UI_COL_WARN, STR_NS_NOT_FOUND, code);
    return;
  }
  if (s_ns.label_weight <= 0) s_ns.label_weight = 1000;
  // Bought again: the filament the box's code was found on.
  if (filament_id > 0 && s_ns.spoolman_filament_id <= 0) s_ns.spoolman_filament_id = filament_id;

  char title[48], q[112], hint[128];
  snprintf(title, sizeof(title), T(STR_NS_TITLE), s_ns.barcode);
  snprintf(q, sizeof(q), "%s %s%s%s%s%s", s_ns.brand, s_ns.material,
           s_ns.subtype[0] ? " " : "", s_ns.subtype,
           s_ns.color_name[0] ? " - " : "", s_ns.color_name);
  int n = snprintf(hint, sizeof(hint), T(STR_NS_HINT), s_ns.label_weight, s_ns.source);
  releaseScreen(&s_ns_scr);
  // A shop price is only a suggestion: it goes onto the spool when the
  // green answer is tapped, the middle one creates the spool without it.
  if (s_ns.price_avg > 0.0f && n > 0 && n < (int)sizeof(hint)) {
    hint[n++] = '\n';
    snprintf(hint + n, sizeof(hint) - n, T(STR_NS_PRICE), s_ns.price_avg, s_ns.price_cur);
    s_ns_scr = questionBoxCreate(title, q, hint, STR_NS_WITH_PRICE, STR_NS_NO_PRICE, STR_CANCEL,
                                 &s_ns_answer);
  } else {
    s_ns_scr = questionBoxCreate(title, q, hint, STR_NS_CREATE, 0, STR_CANCEL, &s_ns_answer);
  }
}

// The answer, one pass after the tap.
static void newSpoolAnswerTick() {
  if (!s_ns_scr || s_ns_answer == QB_NONE) return;
  const uint8_t a = s_ns_answer;
  releaseScreen(&s_ns_scr);
  s_ns_answer = QB_NONE;
  if (a != QB_LEFT && a != QB_MID) return;

  // The price is for the whole spool, BamBuddy wants it per kilogram.
  const float cost_per_kg = (a == QB_LEFT && s_ns.price_avg > 0.0f)
      ? s_ns.price_avg * 1000.0f / (float)s_ns.label_weight : 0.0f;
  int new_id = 0;
  // The app creates it as its own page does: on the user's Spoolman filament,
  // with the slicer profile for every printer model, the bed temperature and
  // price on the filament, the remembered location, and the code learned.
  // What the app itself refused (BamBuddy said no) is said; the scale tries
  // itself only when the app is not there to ask.
  char why[96] = "";
  int code = f2bCreateSpool(s_ns, cost_per_kg, &new_id, why, sizeof(why));
  ScanPeer peer = PEER_F2B;
  if (code != 200 && (code == -1 || code == -4 || code == -5 || code == -11 || code == 404 ||
                      code == 405 || code == 400)) {
    logSDf("Scan: filament_to_bambuddy did not create (%d %s), creating here", code, why);
    // Made on the user's Spoolman filament, the spool inherits its empty
    // spool weight; a copy on the spool would not follow a correction of it.
    const bool inherit = s_ns.core_from_filament && s_ns.spoolman_filament_id > 0 &&
                         bbInventoryMode() == BB_INV_SPOOLMAN;
    // An unopened spool from the box: all of it is still there.
    code = serverReachNote(backendCreateSpoolFromTag(
        s_ns.material, s_ns.subtype, s_ns.brand, s_ns.rgba, s_ns.color_name,
        s_ns.label_weight, inherit ? 0 : s_ns.core_weight, (float)s_ns.label_weight,
        s_ns.temp_min, s_ns.temp_max, &new_id, 8000, cost_per_kg, s_ns.spoolman_filament_id,
        s_ns.slicer_filament, s_ns.slicer_filament_name), true);
    peer = PEER_BACKEND;
  }
  if ((code != 200 && code != 201) || new_id <= 0) {
    logSDf("Scan: creating from %s failed, HTTP %d %s", s_ns.barcode, code, why);
    // BamBuddy's answer through the app: its own status where it gave one.
    int shown = code;
    if (peer == PEER_F2B && code == 502 && why[0]) {
      const int inner = atoi(why);
      if (inner >= 400 && inner < 600) shown = inner;
    }
    scanFailed(STR_NS_FAIL_WHY, shown, peer == PEER_F2B && code == 502 ? PEER_BACKEND : peer);
    return;
  }
  logSDf("Scan: spool %d created from %s (%s), %.2f per kg", new_id, s_ns.barcode, s_ns.source,
         cost_per_kg);
  spoolCacheForget("spool created from a barcode");
  // The next scan of the same box should find the filament directly.
  const char* base = scanSpoolmanBase();
  if (base && base[0]) {
    const int ac = spoolmanSetFilamentArticleIfEmpty(base, new_id, s_ns.barcode);
    logSDf("Scan: article number %s on spool %d's filament, HTTP %d", s_ns.barcode, new_id, ac);
  }
  scanStatus(UI_COL_GOOD, STR_NS_CREATED, new_id);
  handleSpoolId(new_id);
}

// Codes that are not ours but may be a manufacturer SKU or an Amazon number
// on the box: letters and digits, at least one digit, no spaces or slashes.
static bool looksLikeSku(const char* code) {
  size_t n = 0;
  bool digit = false;
  for (const char* p = code; *p; p++, n++) {
    const char c = *p;
    if (isdigit((unsigned char)c)) digit = true;
    else if (!isalpha((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
  }
  return digit && n >= 5 && n <= 32;
}

static void handleSpoolList(JsonArrayConst spools, const char* what, int filaments,
                            int filament_id = 0) {
  const int n = (int)spools.size();
  if (n == 0) {
    // A product bought again after the last spool of it was used up: a new
    // spool on the same filament, offered as for a box never seen.
    if (filaments > 0 && (!g_f2b_url[0] || !backendCanCreateFromTag()))
      scanStatus(UI_COL_WARN, STR_SCAN_NO_ACTIVE, what);
    else
      offerNewSpool(what, filament_id);
    return;
  }
  if (n == 1) {
    handleSpoolId(spools[0]["id"] | 0);
    return;
  }
  float diff = 0;
  const int best = pickByWeight(spools, &diff);
  if (best <= 0) {
    scanStatus(UI_COL_WARN, STR_SCAN_MANY_PUT_DOWN, n);
    return;
  }
  logSDf("Scan: %d spools for %s, #%d closest by weight (%.0f g off)", n, what, best, diff);
  handleSpoolId(best);
}

static void handleArticle(const ScanCode& sc) {
  const char* base = scanSpoolmanBase();
  if (!base || !base[0]) {
    // Nothing to search existing spools in. A box code can still become a
    // new spool; a filament label cannot be anything without Spoolman.
    if (sc.kind == SCAN_ARTICLE && g_f2b_url[0]) offerNewSpool(sc.article);
    else scanStatus(UI_COL_WARN, STR_SCAN_NEEDS_SPOOLMAN, sc.article);
    return;
  }
  JsonDocument doc;
  int filaments = 0, filament_id = 0;
  int code;
  if (sc.kind == SCAN_ARTICLE) {
    logSDf("Scan: article %s", sc.article);
    code = spoolmanFindSpoolsByArticle(base, sc.article, doc, &filaments, 6000, &filament_id);
  } else {
    logSDf("Scan: filament %d", sc.id);
    code = spoolmanFindSpoolsByFilament(base, sc.id, doc);
    filaments = 1;
  }
  code = serverReachNote(code, true);
  if (code != 200) {
    logSDf("Scan: lookup failed, HTTP %d", code);
    scanFailed(STR_SCAN_FAIL_WHY, code, PEER_SPOOLMAN);
    return;
  }
  char what[20];
  if (sc.kind == SCAN_ARTICLE) snprintf(what, sizeof(what), "%s", sc.article);
  else                         snprintf(what, sizeof(what), "F%d", sc.id);
  handleSpoolList(doc.as<JsonArrayConst>(), what, filaments, filament_id);
}

bool scanQuestionOpen() { return s_ns_scr != nullptr; }

void scanActionTick() {
  scannerApplyPending();
  newSpoolAnswerTick();
  // A question about a new spool is open: further codes wait for it.
  if (s_ns_scr) return;
  char code[SCANNER_CODE_MAX];
  if (!scannerPoll(code, sizeof(code))) return;

  const ScanCode sc = scanClassify(code);
  logSDf("Scan: \"%s\" -> kind %d", code, (int)sc.kind);
  if (!wifi_ok && sc.kind != SCAN_UNKNOWN) {
    scanStatus(UI_COL_BAD_TEXT, STR_SCAN_NO_WIFI, 0);
    return;
  }
  switch (sc.kind) {
    case SCAN_SPOOL_ID:
      handleSpoolId(sc.id);
      break;
    case SCAN_FILAMENT_ID:
    case SCAN_ARTICLE:
      handleArticle(sc);
      break;
    default:
      if (looksLikeSku(code) && g_f2b_url[0]) offerNewSpool(code);
      else scanStatus(UI_COL_WARN, STR_SCAN_UNKNOWN_CODE, 0);
      break;
  }
}
