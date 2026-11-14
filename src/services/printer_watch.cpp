#include "printer_watch.h"

#include <Arduino.h>
#include <string.h>

#include "app/app_state.h"
#include "hardware/sd_logger.h"
#include "services/backend.h"
#include "services/backend_api.h"
#include "services/prefs_store.h"

bool g_print_watch = true;

// Shared with the AMS view, which is where the printer is picked.
#define PW_NVS_PRINTER  "ams_printer"
#define PW_NVS_JOB      "pw_job"

static int           s_printer_id   = 0;
static unsigned long s_next_poll_ms = 0;
static bool          s_job_pending  = false;
static BbJob         s_job;
static BbPlug        s_plug;
static int           s_plug_printer = 0;   // which printer s_plug belongs to

static bool          s_cooling       = false;
static unsigned long s_cool_since_ms = 0;
static unsigned long s_cool_next_ms  = 0;
static int           s_cool_temp     = -1;
static int           s_cool_target   = PW_COOL_DEFAULT_C;
// The printer and the plug a running cooldown is for, taken when the wait
// starts. The AMS view may step to another printer meanwhile, and its chip
// reads that printer's plug into s_plug: the wait used to follow, and switched
// the other printer off as soon as that one read cold, a paused print on it
// included, while the printer that had been asked about stayed on.
static int           s_cool_printer  = 0;
static int           s_cool_plug     = 0;

static bool watchActive() {
  return backendIsBamBuddy() && wifi_ok;
}

// The stored choice when there is one; otherwise the first printer BamBuddy
// lists, which is also what the AMS view falls back to.
static int resolvePrinter() {
  const int stored = prefsGetInt(PW_NVS_PRINTER, 0);
  if (stored > 0) return stored;
  if (s_printer_id > 0) return s_printer_id;   // the fallback, already looked up
  AmsPrinterList list;
  if (backendListPrinters(list, 6000) == 200 && list.count > 0) return list.p[0].id;
  return 0;
}

const BbJob* printerWatchPendingJob() { return s_job_pending ? &s_job : nullptr; }

void printerWatchTakeJob() {
  if (!s_job_pending) return;
  s_job_pending = false;
  prefsPutInt(PW_NVS_JOB, s_job.id);
}

const BbPlug& printerWatchPlug() { return s_plug; }

int printerWatchRefreshPlug() {
  if (!watchActive()) return -1;
  // The AMS view may have switched printers a moment ago.
  const int want = resolvePrinter();
  if (want != s_printer_id) {
    s_printer_id = want;
    s_job_pending = false;
  }
  if (s_printer_id <= 0) return -1;
  BbPlug p;
  const int code = bbGetPrinterPlug(backendBaseUrl(), bambuddyApiKey(), s_printer_id, p);
  if (code == 200) {
    s_plug = p;
    s_plug_printer = s_printer_id;
  }
  return code;
}

static int switchPlug(int plug_id, bool on) {
  const int code = bbSwitchPlug(backendBaseUrl(), bambuddyApiKey(), plug_id, on);
  logSDf("PrinterWatch: plug %d -> %s, HTTP %d", plug_id, on ? "ON" : "OFF", code);
  if (code == 200) {
    if (s_plug.id == plug_id) {
      s_plug.state_known = true;
      s_plug.on = on;
    }
    // A switch of the plug a cooldown waits for ends that wait, either way.
    if (s_cooling && plug_id == s_cool_plug) s_cooling = false;
  }
  return code;
}

int printerWatchSwitch(bool on) {
  if (!s_plug.found || !s_plug.controllable) return -1;
  return switchPlug(s_plug.id, on);
}

// A print on the printer, running, being prepared or paused. A paused one (a
// runout, a jam) cools down like a finished one, and power cut then loses it.
// The spellings the AMS view reads as well.
static bool printActive(const char* st) {
  static const char* const ACTIVE[] = { "RUNNING", "PRINTING", "PREPARE", "PREPARING",
                                        "SLICING", "PAUSE", "PAUSED" };
  for (const char* a : ACTIVE)
    if (strcasecmp(st, a) == 0) return true;
  return false;
}

static bool jobAskable(const BbJob& j) {
  if (j.id <= 0 || !j.finished || j.has_verdict) return false;
  // Ended, one way or the other. A job still printing has no completed_at,
  // but the status is checked as well in case a server sets both.
  return strcmp(j.status, "printing") != 0 && strcmp(j.status, "pending") != 0;
}

static void pollJob() {
  BbJob j;
  const int code = bbGetLastJob(backendBaseUrl(), bambuddyApiKey(), s_printer_id, j);
  if (code != 200 || j.id <= 0) return;
  // First look since the switch went on: take note, ask nothing.
  if (!prefsHasKey(PW_NVS_JOB)) {
    prefsPutInt(PW_NVS_JOB, j.id);
    logSDf("PrinterWatch: printer %d, newest job %d noted", s_printer_id, j.id);
    return;
  }
  if (j.id == prefsGetInt(PW_NVS_JOB, 0) || !jobAskable(j)) return;
  if (s_job_pending && s_job.id == j.id) return;
  s_job = j;
  s_job_pending = true;
  logSDf("PrinterWatch: job %d \"%s\" ended (%s), asking", j.id, j.name, j.status);
}

PwOffPlan printerWatchPowerOffWhenCool() {
  if (s_plug_printer != s_printer_id) printerWatchRefreshPlug();
  if (!s_plug.found || !s_plug.controllable) return PW_OFF_NONE;
  if (s_plug.auto_off) {
    logSD("PrinterWatch: BamBuddy's Auto Power Off is set, leaving it to BamBuddy");
    return PW_OFF_BAMBUDDY;
  }
  s_cool_target = (s_plug.off_by_temp && s_plug.off_temp_c > 0) ? s_plug.off_temp_c
                                                                 : PW_COOL_DEFAULT_C;
  s_cooling       = true;
  s_cool_printer  = s_printer_id;
  s_cool_plug     = s_plug.id;
  s_cool_since_ms = millis();
  s_cool_next_ms  = 0;
  s_cool_temp     = -1;
  logSDf("PrinterWatch: switching off below %d C", s_cool_target);
  printerWatchTick();
  return s_cooling ? PW_OFF_WAITING : PW_OFF_NOW;
}

bool printerWatchCooling() { return s_cooling; }
int  printerWatchCoolingTemp()   { return s_cool_temp; }
int  printerWatchCoolingTarget() { return s_cool_target; }

static void coolTick() {
  const unsigned long now = millis();
  if (s_cool_next_ms && (long)(now - s_cool_next_ms) < 0) return;
  s_cool_next_ms = now + PW_COOL_POLL_MS;
  if (now - s_cool_since_ms > PW_COOL_TIMEOUT_MS) {
    logSD("PrinterWatch: printer did not cool down within an hour, plug left on");
    s_cooling = false;
    return;
  }
  BbPrinterLive live;
  if (bbGetPrinterLive(backendBaseUrl(), bambuddyApiKey(), s_cool_printer, live) != 200) return;
  // A new print took over, or one is paused: leave the power alone.
  if (printActive(live.state)) {
    logSDf("PrinterWatch: printer %d is %s, cooldown switch-off dropped", s_cool_printer,
           live.state);
    s_cooling = false;
    return;
  }
  // Only numbers BamBuddy has from the printer itself, the nozzle among them.
  // A bed reading alone switched a printer off whose nozzle nobody had read,
  // and a disconnected printer's are not current. The hour above still ends
  // the wait, with the plug left on.
  if (!live.connected || live.nozzle_c < 0) return;
  const float hot = live.nozzle_c > live.bed_c ? live.nozzle_c : live.bed_c;
  s_cool_temp = (int)(hot + 0.5f);
  if (hot < (float)s_cool_target) {
    logSDf("PrinterWatch: printer %d at %d C, below %d C, switching off", s_cool_printer,
           s_cool_temp, s_cool_target);
    switchPlug(s_cool_plug, false);
    s_cooling = false;
  }
}

void printerWatchTick() {
  if (!watchActive()) return;
  if (s_cooling) {
    coolTick();
    return;
  }
  if (!g_print_watch) return;
  const unsigned long now = millis();
  // The first look waits for the network and the backend to settle, like
  // the AMS presence check.
  if (!s_next_poll_ms) s_next_poll_ms = now + PW_FIRST_POLL_MS;
  if ((long)(now - s_next_poll_ms) < 0) return;
  s_next_poll_ms = now + PW_POLL_MS;

  // The AMS view may have switched printers since the last look.
  const int want = resolvePrinter();
  if (want != s_printer_id) {
    s_printer_id = want;
    s_job_pending = false;
    s_plug = BbPlug{};
    s_plug_printer = 0;
  }
  if (s_printer_id <= 0) return;
  pollJob();
}
