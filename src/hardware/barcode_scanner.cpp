#include "barcode_scanner.h"

#include <Arduino.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/prefs_store.h"

bool     g_scanner_enabled = false;
int8_t   g_scanner_rx_pin  = SCANNER_DEFAULT_RX;
int8_t   g_scanner_tx_pin  = SCANNER_DEFAULT_TX;
uint32_t g_scanner_baud    = SCANNER_DEFAULT_BAUD;

// A scanner set to send no terminator still finishes a code within a few
// milliseconds at 9600 baud: one byte is about 1 ms. A pause this long means
// the code is complete.
static constexpr uint32_t SCANNER_IDLE_END_MS = 60;

// The same code read twice in a row this quickly is the scanner seeing the
// label again while it is still held up, not a second request.
static constexpr uint32_t SCANNER_REPEAT_MS = 1500;

static HardwareSerial& s_uart = Serial1;
static bool     s_open = false;
static volatile bool s_reopen_pending = false;

static char     s_buf[SCANNER_CODE_MAX];
static size_t   s_len = 0;
static uint32_t s_last_byte_ms = 0;
static bool     s_overflow = false;

static char     s_last_code[SCANNER_CODE_MAX];
static uint32_t s_last_code_ms = 0;
static uint32_t s_code_count = 0;

bool scannerPinAllowed(int pin, bool is_tx) {
  if (is_tx && pin == -1) return true;
  switch (pin) {
    case 12: case 13: case 14: case 21: return true;
    default: return false;
  }
}

static uint32_t sanitizeBaud(uint32_t baud) {
  switch (baud) {
    case 9600: case 19200: case 38400: case 57600: case 115200: return baud;
    default: return SCANNER_DEFAULT_BAUD;
  }
}

static void scannerClose() {
  if (!s_open) return;
  s_uart.end();
  s_open = false;
}

void scannerBegin() {
  g_scanner_enabled = prefsGetBool("scan_on", false);
  int rx = prefsGetInt("scan_rx", SCANNER_DEFAULT_RX);
  int tx = prefsGetInt("scan_tx", SCANNER_DEFAULT_TX);
  if (!scannerPinAllowed(rx, false)) rx = SCANNER_DEFAULT_RX;
  if (!scannerPinAllowed(tx, true) || tx == rx) tx = -1;
  g_scanner_rx_pin = (int8_t)rx;
  g_scanner_tx_pin = (int8_t)tx;
  g_scanner_baud   = sanitizeBaud(prefsGetUInt("scan_baud", SCANNER_DEFAULT_BAUD));

  scannerClose();
  s_len = 0;
  s_overflow = false;
  if (!g_scanner_enabled) {
    logSD("Scanner: switched off");
    return;
  }
  s_uart.setRxBufferSize(512);
  s_uart.begin(g_scanner_baud, SERIAL_8N1, g_scanner_rx_pin, g_scanner_tx_pin);
  s_open = true;
  logSDf("Scanner: UART open, RX=GPIO%d TX=%s%d %lu baud", (int)g_scanner_rx_pin,
         g_scanner_tx_pin < 0 ? "none/" : "GPIO", (int)g_scanner_tx_pin,
         (unsigned long)g_scanner_baud);
}

void scannerConfigure(bool enabled, int rx, int tx, uint32_t baud) {
  prefsPutBool("scan_on", enabled);
  if (scannerPinAllowed(rx, false)) prefsPutInt("scan_rx", rx);
  if (scannerPinAllowed(tx, true) && tx != rx) prefsPutInt("scan_tx", tx);
  prefsPutUInt("scan_baud", sanitizeBaud(baud));
  s_reopen_pending = true;
}

void scannerApplyPending() {
  if (!s_reopen_pending) return;
  s_reopen_pending = false;
  scannerBegin();
}

bool scannerActive() { return s_open; }

// Strips control characters at both ends: some scanners prefix an STX or
// append a tab, and none of that belongs to the code.
static size_t trimCode(char* s) {
  size_t n = strlen(s);
  while (n > 0 && (uint8_t)s[n - 1] <= ' ') s[--n] = '\0';
  size_t lead = 0;
  while (s[lead] && (uint8_t)s[lead] <= ' ') lead++;
  if (lead) memmove(s, s + lead, n - lead + 1);
  return n - lead;
}

static bool finishCode(char* out, size_t out_size) {
  s_buf[s_len] = '\0';
  const bool overflow = s_overflow;
  s_len = 0;
  s_overflow = false;
  if (overflow) {
    logSD("Scanner: code longer than the buffer, dropped");
    return false;
  }
  if (trimCode(s_buf) == 0) return false;

  const uint32_t now = millis();
  if (strcmp(s_buf, s_last_code) == 0 && now - s_last_code_ms < SCANNER_REPEAT_MS) {
    s_last_code_ms = now;   // still held up: keep swallowing
    return false;
  }
  snprintf(s_last_code, sizeof(s_last_code), "%s", s_buf);
  s_last_code_ms = now;
  s_code_count++;
  snprintf(out, out_size, "%s", s_buf);
  return true;
}

bool scannerPoll(char* out, size_t out_size) {
  if (!s_open || !out || out_size == 0) return false;
  while (s_uart.available() > 0) {
    const int c = s_uart.read();
    if (c < 0) break;
    s_last_byte_ms = millis();
    if (c == '\r' || c == '\n') {
      if (s_len > 0 || s_overflow) {
        if (finishCode(out, out_size)) return true;
      }
      continue;
    }
    if (s_len < sizeof(s_buf) - 1) s_buf[s_len++] = (char)c;
    else s_overflow = true;
  }
  if ((s_len > 0 || s_overflow) && millis() - s_last_byte_ms >= SCANNER_IDLE_END_MS) {
    return finishCode(out, out_size);
  }
  return false;
}

const char* scannerLastCode()   { return s_last_code; }
uint32_t    scannerLastCodeMs() { return s_last_code_ms; }
uint32_t    scannerCodeCount()  { return s_code_count; }
