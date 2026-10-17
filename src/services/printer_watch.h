#pragma once

#include <stdint.h>

#include "services/bambuddy_api.h"

// ============================================================
//  PRINTER WATCH (BamBuddy only)
//
//  The printer is the one picked in the AMS view, kept in NVS as
//  "ams_printer", so switching there switches everything: the power chip,
//  the question after a print and the cooldown switch-off.
//
//  A finished print is read off BamBuddy's archive, not off the printer's
//  live state: the archive keeps the job, its outcome and whether anyone has
//  judged it yet, so a print that ended while the scale was asleep or
//  restarting is still asked about once it is back. The last job asked
//  about is remembered in NVS ("pw_job"), and the first look after the
//  switch is turned on only takes note of what is there, so nobody is asked
//  about a print from last month.
//
//  Switching off after a print: BamBuddy has no cooldown on a manual off.
//  When its own Auto Power Off is set on the plug, BamBuddy does it and the
//  scale only says so. Otherwise the scale waits here until nozzle and bed
//  are below the plug's threshold (50 C when none is set), then switches.
//  A new print starting, or an hour passing, ends the wait.
//
//  Everything here blocks for one small request at a time and runs from the
//  loop on a slow timer. No LVGL.
// ============================================================

#define PW_FIRST_POLL_MS     25000UL     // after boot
#define PW_POLL_MS           60000UL     // archive check
#define PW_COOL_POLL_MS      15000UL     // temperatures while cooling down
#define PW_COOL_TIMEOUT_MS   (60UL * 60UL * 1000UL)
#define PW_COOL_DEFAULT_C    50

extern bool g_print_watch;        // NVS "bb_printdone", default on

// The printer everything here is about. 0 until one is known: the stored AMS
// view choice, or the first printer BamBuddy lists.
int  printerWatchPrinterId();

// From appLoop(). Does nothing on most passes.
void printerWatchTick();

// A finished job waiting for its question, or nullptr. The popup takes it
// with printerWatchTakeJob() so it is asked once.
const BbJob* printerWatchPendingJob();
void printerWatchTakeJob();

// The plug of the watched printer, as last read. found false when there is
// none or it has not been read yet.
const BbPlug& printerWatchPlug();
// Reads the plug again now. Blocks for up to two requests.
int  printerWatchRefreshPlug();
// Switches now. Returns the HTTP code; refreshes the cached state on success.
int  printerWatchSwitch(bool on);

// The power-off answer after a print. Returns what will happen:
enum PwOffPlan : uint8_t {
  PW_OFF_NONE = 0,       // no plug, or one that cannot be switched
  PW_OFF_BAMBUDDY,       // BamBuddy's own Auto Power Off will do it
  PW_OFF_WAITING,        // the scale waits for the cooldown, then switches
  PW_OFF_NOW             // already cool: switched right away
};
PwOffPlan printerWatchPowerOffWhenCool();
bool printerWatchCooling();
void printerWatchCancelCooling();
// Highest of nozzle and bed at the last look while cooling, -1 unknown.
int  printerWatchCoolingTemp();
int  printerWatchCoolingTarget();
