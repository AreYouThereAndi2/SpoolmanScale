#include "barcode_scanner.h"

#include <Arduino.h>
#include <string.h>

#include "hardware/pins.h"
#include "hardware/scale.h"
#include "hardware/sd_logger.h"
#include "services/prefs_store.h"

bool     g_scanner_enabled = false;
int8_t   g_scanner_rx_pin  = SCANNER_DEFAULT_RX;
int8_t   g_scanner_tx_pin  = SCANNER_DEFAULT_TX;
uint32_t g_scanner_baud    = SCANNER_DEFAULT_BAUD;
uint8_t  g_scanner_lamp    = SCAN_LAMP_KEEP;
uint8_t  g_scanner_led     = SCAN_LED_KEEP;
static ScannerLightResult s_light_result = SCAN_LIGHT_NOT_SENT;

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

bool scannerPinAllowed(int pin, bool is_tx) {
  if (is_tx && pin == -1) return true;
  if (scaleHx711Fitted() && (pin == hw_pins::HX711_SCK || pin == hw_pins::HX711_DOUT)) return false;
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

// ---- GM861 zone bits ------------------------------------------------------
// Grow GM861 manual V1.2.4, 10.2 to 10.6. A command is 7E 00, a type (07
// read, 08 write), a length, a two-byte address and the data, then a
// CRC-CCITT the scanner also accepts as AB CD. It answers 02 00 00, a length
// and the data. Zone bit 0x0000:
//   bit 7    green LED on a good read
//   bit 6    1 = sound on
//   bit 5-4, bit 3-2  the two lights, each 00 off, 01 while reading, 1x
//            always on (the manual lists both alike, without saying which)
//   bit 1-0  read mode (10 continuous, the default; 11 sense)
// Its default is D6. The two light fields are set together: the manual names
// them only by position, and one switch for "the light" is what a person
// at the bench means anyway.
static constexpr uint32_t GM_REPLY_MS = 300;
static constexpr uint8_t  GM_DEFAULT_0000 = 0xD6;

// Sends one command and waits for the 02 00 00 reply; true with its first
// data byte. Whatever else the line carries meanwhile is dropped, a code read
// in these few hundred milliseconds included.
static bool gmTransact(const uint8_t* cmd, size_t n, uint8_t* value) {
  while (s_uart.available() > 0) s_uart.read();
  s_uart.write(cmd, n);
  s_uart.flush();
  uint8_t win[5] = {0};
  size_t got = 0;
  const uint32_t from = millis();
  while (millis() - from < GM_REPLY_MS) {
    if (s_uart.available() <= 0) { delay(2); continue; }
    memmove(win, win + 1, sizeof(win) - 1);
    win[sizeof(win) - 1] = (uint8_t)s_uart.read();
    if (++got >= sizeof(win) && win[0] == 0x02 && win[1] == 0x00 && win[2] == 0x00 &&
        win[3] >= 1) {
      if (value) *value = win[4];
      return true;
    }
  }
  return false;
}

static uint8_t lampBits(uint8_t lamp) {
  switch (lamp) {
    case SCAN_LAMP_STANDARD: return 0x01;
    case SCAN_LAMP_ALWAYS:   return 0x02;
    default:                 return 0x00;   // off
  }
}

// Reads zone bit 0x0000, changes the light fields and writes it back, so the
// read mode and the sound stay as the setup codes left them.
static void applyLights() {
  s_light_result = SCAN_LIGHT_NOT_SENT;
  if (g_scanner_lamp == SCAN_LAMP_KEEP && g_scanner_led == SCAN_LED_KEEP) return;
  if (g_scanner_tx_pin < 0) {
    s_light_result = SCAN_LIGHT_NO_TX;
    logSD("Scanner: lights not sent, no TX pin");
    return;
  }
  static const uint8_t READ_0000[] = {0x7E, 0x00, 0x07, 0x01, 0x00, 0x00, 0x01, 0xAB, 0xCD};
  uint8_t cur = 0;
  const bool read_ok = gmTransact(READ_0000, sizeof(READ_0000), &cur);
  if (!read_ok) cur = GM_DEFAULT_0000;
  uint8_t want = cur;
  if (g_scanner_lamp != SCAN_LAMP_KEEP) {
    const uint8_t b = lampBits(g_scanner_lamp);
    want = (want & 0xC3) | (b << 4) | (b << 2);
  }
  if (g_scanner_led == SCAN_LED_ON)  want |= 0x80;
  if (g_scanner_led == SCAN_LED_OFF) want &= 0x7F;
  const uint8_t write_0000[] = {0x7E, 0x00, 0x08, 0x01, 0x00, 0x00, want, 0xAB, 0xCD};
  const bool write_ok = gmTransact(write_0000, sizeof(write_0000), nullptr);
  s_light_result = write_ok ? SCAN_LIGHT_OK : SCAN_LIGHT_NO_ANSWER;
  logSDf("Scanner: zone 0x0000 %02X%s -> %02X, %s", cur, read_ok ? "" : " (assumed)", want,
         write_ok ? "confirmed" : "no answer");
}

ScannerLightResult scannerLightResult() { return s_light_result; }

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
  g_scanner_lamp   = (uint8_t)prefsGetInt("scan_lamp", SCAN_LAMP_KEEP);
  g_scanner_led    = (uint8_t)prefsGetInt("scan_led", SCAN_LED_KEEP);
  if (g_scanner_lamp > SCAN_LAMP_OFF) g_scanner_lamp = SCAN_LAMP_KEEP;
  if (g_scanner_led > SCAN_LED_OFF)   g_scanner_led  = SCAN_LED_KEEP;

  scannerClose();
  s_len = 0;
  s_overflow = false;
  if (!g_scanner_enabled) {
    s_light_result = SCAN_LIGHT_NOT_SENT;
    logSD("Scanner: switched off");
    return;
  }
  s_uart.setRxBufferSize(512);
  s_uart.begin(g_scanner_baud, SERIAL_8N1, g_scanner_rx_pin, g_scanner_tx_pin);
  s_open = true;
  logSDf("Scanner: UART open, RX=GPIO%d TX=%s%d %lu baud", (int)g_scanner_rx_pin,
         g_scanner_tx_pin < 0 ? "none/" : "GPIO", (int)g_scanner_tx_pin,
         (unsigned long)g_scanner_baud);
  applyLights();
}

void scannerConfigure(bool enabled, int rx, int tx, uint32_t baud, uint8_t lamp, uint8_t led) {
  prefsPutBool("scan_on", enabled);
  if (lamp <= SCAN_LAMP_OFF) prefsPutInt("scan_lamp", lamp);
  if (led <= SCAN_LED_OFF)   prefsPutInt("scan_led", led);
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
