#include "nfc_reset.h"

#include <Arduino.h>

#include "hardware/barcode_scanner.h"
#include "hardware/nfc.h"
#include "hardware/pins.h"
#include "hardware/scale.h"
#include "hardware/sd_logger.h"
#include "services/prefs_store.h"

// NVS keys. Preferences keys are limited to 15 characters.
#define KEY_VERIFIED   "rst_ok"
#define KEY_VERIFIED_HX "rst_ok_hx"
#define KEY_RECOVERY   "rst_recov"
#define KEY_NEVER      "rst_never"
#define KEY_BOOTS      "rst_boots"
#define KEY_DUE_AT     "rst_due_at"

// How long "later" lasts. Boots rather than days: the clock needs NTP and a
// scale that sits unplugged between prints would otherwise be nagged on the
// strength of wall time it never experienced.
#define HINT_DEFER_BOOTS  20

// Held low for long enough to cover one I2C transaction and no longer. The
// PN532 needs 20 ns on RSTPD_N, the measurement needs a round trip.
#define PROBE_HOLD_MS   5
#define PROBE_SETTLE_MS 100

static bool     s_verified = false;
static bool     s_verified_hx = false;
static bool     s_recovery = false;
static bool     s_never    = false;
static uint32_t s_boots    = 0;
static uint32_t s_due_at   = 0;

void nfcResetLoad() {
  s_verified = prefsGetBool(KEY_VERIFIED, false);
  s_verified_hx = prefsGetBool(KEY_VERIFIED_HX, false);
  s_recovery = prefsGetBool(KEY_RECOVERY, false);
  s_never    = prefsGetBool(KEY_NEVER,    false);
  s_boots    = prefsGetUInt(KEY_BOOTS,   0) + 1;
  s_due_at   = prefsGetUInt(KEY_DUE_AT,  0);
  prefsPutUInt(KEY_BOOTS, s_boots);
}

int8_t nfcResetPinForBoot() {
  // With an HX711 the usual pins are its clock and data line: the reader goes
  // without a reset line unless one was confirmed on pin 8.
  if (scaleHx711Fitted()) return s_verified_hx ? hw_pins::PN532_RESET_HX : -1;
  return s_verified ? hw_pins::PN532_RESET_WIRE : hw_pins::PN532_RESET_SAFE;
}

bool nfcResetVerified() { return scaleHx711Fitted() ? s_verified_hx : s_verified; }

bool nfcResetOnScannerPin() { return scaleHx711Fitted() && s_verified_hx; }

bool nfcResetSelfTest() {
  // With an HX711 the old wire's pin is its data line, and holding that low
  // would fight the chip. The candidate there is pin 8.
  const bool hx = scaleHx711Fitted();
  const int8_t wire = hx ? hw_pins::PN532_RESET_HX : hw_pins::PN532_RESET_WIRE;

  // Open drain: the pin sinks and never sources, so on a device that still has
  // the wire on the module's output there is no contention to speak of.
  pinMode(wire, OUTPUT_OPEN_DRAIN);
  digitalWrite(wire, LOW);
  delay(PROBE_HOLD_MS);
  const bool answered_while_held = nfcHardwarePing();
  digitalWrite(wire, HIGH);
  pinMode(wire, INPUT);
  delay(PROBE_SETTLE_MS);

  // Whatever the verdict, the reader has to be usable again afterwards. A
  // probe that leaves the scale worse off than it found it is not a probe.
  uint32_t version = 0;
  const bool back = nfcHardwareReinit(&version);

  const bool works = (!answered_while_held && back);
  bool &verified = hx ? s_verified_hx : s_verified;
  if (works != verified) {
    verified = works;
    prefsPutBool(hx ? KEY_VERIFIED_HX : KEY_VERIFIED, works);
  }

  // Pin 8 is the scanner's TX until the wire is confirmed: the probe took it
  // over, so the UART gets it back.
  if (hx) scannerBegin();

  logSDf("NFC reset probe on GPIO%d: held=%s after=%s fw=0x%08lX -> %s", (int)wire,
         answered_while_held ? "answers" : "silent",
         back ? "answers" : "silent", (unsigned long)version,
         works ? "line works" : "no effect");
  return works;
}

void nfcReaderNoteRecovery() {
  if (s_recovery) return;   // one write, not one per stumble
  s_recovery = true;
  prefsPutBool(KEY_RECOVERY, true);
}

bool nfcResetHintDue() {
  if (scaleHx711Fitted()) return false;   // its pin is taken
  if (s_verified) return false;   // nothing left to tell them
  if (s_never)    return false;
  if (!s_recovery) return false;  // the modification would buy them nothing yet
  return s_boots >= s_due_at;
}

void nfcResetHintLater() {
  s_due_at = s_boots + HINT_DEFER_BOOTS;
  prefsPutUInt(KEY_DUE_AT, s_due_at);
}

void nfcResetHintNever() {
  s_never = true;
  prefsPutBool(KEY_NEVER, true);
}
