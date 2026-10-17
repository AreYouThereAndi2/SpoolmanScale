#include "scan_action.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>
#include <stdarg.h>

#include "app/app_loop.h"
#include "app/app_state.h"
#include "hardware/barcode_scanner.h"
#include "hardware/sd_logger.h"
#include "lang.h"
#include "services/backend.h"
#include "services/bambuddy_api.h"
#include "services/scan_code.h"
#include "services/server_reach.h"
#include "services/spoolman_api.h"
#include "ui/main_screen_helpers.h"
#include "ui/spool_flow.h"
#include "ui/spoolman_lookup.h"
#include "ui/theme.h"

// Below this the pad counts as empty: the same floor the location question
// uses, and well under the lightest spool core anyone weighs.
static constexpr float SCAN_LOAD_MIN_G = 50.0f;

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
    const float net = scale_weight_g - candidateTare(s);
    const float diff = fabsf(net - (float)(s["remaining_weight"] | 0.0f));
    if (diff < best_diff) { best_diff = diff; best = id; }
  }
  if (out_diff) *out_diff = best_diff;
  return best;
}

static void handleSpoolList(JsonArrayConst spools, const char* what, int filaments) {
  const int n = (int)spools.size();
  if (n == 0) {
    if (filaments > 0) scanStatus(UI_COL_WARN, STR_SCAN_NO_ACTIVE, what);
    else               scanStatus(UI_COL_WARN, STR_SCAN_UNKNOWN_ARTICLE, what);
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
    scanStatus(UI_COL_WARN, STR_SCAN_NEEDS_SPOOLMAN, sc.article);
    return;
  }
  JsonDocument doc;
  int filaments = 0;
  int code;
  if (sc.kind == SCAN_ARTICLE) {
    logSDf("Scan: article %s", sc.article);
    code = spoolmanFindSpoolsByArticle(base, sc.article, doc, &filaments);
  } else {
    logSDf("Scan: filament %d", sc.id);
    code = spoolmanFindSpoolsByFilament(base, sc.id, doc);
    filaments = 1;
  }
  code = serverReachNote(code, true);
  if (code != 200) {
    logSDf("Scan: lookup failed, HTTP %d", code);
    scanStatus(UI_COL_BAD_TEXT, STR_SCAN_LOOKUP_FAILED, code);
    return;
  }
  char what[20];
  if (sc.kind == SCAN_ARTICLE) snprintf(what, sizeof(what), "%s", sc.article);
  else                         snprintf(what, sizeof(what), "F%d", sc.id);
  handleSpoolList(doc.as<JsonArrayConst>(), what, filaments);
}

void scanActionTick() {
  scannerApplyPending();
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
      scanStatus(UI_COL_WARN, STR_SCAN_UNKNOWN_CODE, 0);
      break;
  }
}
