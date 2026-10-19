#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  BARCODE SCANNER (GM861 and anything else that types a line)
//
//  A UART scanner on the extension header. It sends every code it reads as
//  plain text, terminated by CR, LF or both. A GM861 left in its default
//  sense mode reads on its own when something is held in front of it, and
//  most settings are made by scanning the setup codes in its manual, so the
//  TX line is optional. With it, the scale sets the scanner's lights.
//
//  Pins: the I/O connector brings out 5 V, GND and GPIO 10, 11, 12, 13, 14
//  and 21 on pins 1 to 8. 10 and 11 (pins 3, 4) are the I2C bus of the
//  reader and the ADC, 12 (pin 5) is the PN532 reset this firmware hands the
//  library on every device whose wire was not moved, and 14 (pin 7) is that
//  wire when it was. That leaves 13 on pin 6 - white, unused on the 7-pin
//  cable - for the scanner's TX (our RX). 13 is only the PN532 IRQ
//  placeholder, set to an input once in the constructor and never read in
//  I2C mode; the scanner is started after the reader so the UART takes the
//  pin over from it. Our TX goes to 21 on pin 8, which the 7-pin cable does
//  not reach: it is optional, a GM861 reads without ever being spoken to.
//
//  Supply: the connector has 5 V and no 3.3 V. Feed the scanner from 5 V
//  only if its manual allows it, and measure its TX line idling before it
//  goes to GPIO 13: it must sit at 3.3 V, the ESP32-S3 is not 5 V tolerant.
//
//  Reading runs on the loop: the UART driver buffers in its own interrupt,
//  a code is 8 to 100 bytes, and the loop comes round every few ms. A task
//  would buy nothing but a lock around the hand over.
// ============================================================

#define SCANNER_CODE_MAX      128
#define SCANNER_DEFAULT_RX    13     // EXT_IO4, pin 6: the scanner's TX goes here
#define SCANNER_DEFAULT_TX    21     // EXT_IO6, pin 8: the scanner's RX, optional
#define SCANNER_DEFAULT_BAUD  9600

extern bool     g_scanner_enabled;   // NVS "scan_on",   default off
extern int8_t   g_scanner_rx_pin;    // NVS "scan_rx",   default 13
extern int8_t   g_scanner_tx_pin;    // NVS "scan_tx",   default 21, -1 = none
extern uint32_t g_scanner_baud;      // NVS "scan_baud", default 9600

// The GM861's lights, sent to it over our TX each time the UART opens. Not
// saved in the scanner: it forgets them at power off and gets them again at
// the next boot, which spares its flash and keeps NVS the one place they
// live. "Keep" sends nothing, so setup codes scanned from the manual stay.
enum ScannerLamp : uint8_t {          // illumination and aimer together
  SCAN_LAMP_KEEP = 0, SCAN_LAMP_STANDARD, SCAN_LAMP_ALWAYS, SCAN_LAMP_OFF
};
enum ScannerLed : uint8_t {           // the green LED on a good read
  SCAN_LED_KEEP = 0, SCAN_LED_ON, SCAN_LED_OFF
};
enum ScannerLightResult : uint8_t {
  SCAN_LIGHT_NOT_SENT = 0,            // both on keep, or the scanner is off
  SCAN_LIGHT_OK,                      // the scanner confirmed the write
  SCAN_LIGHT_NO_ANSWER,               // no reply: TX not wired, or not a GM861
  SCAN_LIGHT_NO_TX,                   // no TX pin set, nothing can be sent
};
extern uint8_t g_scanner_lamp;       // NVS "scan_lamp", ScannerLamp
extern uint8_t g_scanner_led;        // NVS "scan_led",  ScannerLed
ScannerLightResult scannerLightResult();

// Reads the settings and opens the UART when the scanner is switched on.
// Called once at boot after the NFC reader, and again by scannerApplyPending()
// after a change from the browser.
void scannerBegin();

// Whether a pin may carry the scanner. The header pins that nothing else on
// this board drives, plus -1 for "not connected" on TX.
bool scannerPinAllowed(int pin, bool is_tx);

// Stores new settings and asks the loop to reopen the UART with them. Safe
// from a web handler: nothing but NVS and a flag.
void scannerConfigure(bool enabled, int rx, int tx, uint32_t baud,
                       uint8_t lamp = SCAN_LAMP_KEEP, uint8_t led = SCAN_LED_KEEP);
void scannerApplyPending();

// Collects bytes and hands back one finished code, trimmed and terminated.
// A code ends at CR or LF, or after a short pause for a scanner that is set
// to send no terminator. Non-blocking, call every loop pass.
bool scannerPoll(char* out, size_t out_size);

// The last code read, for the status page.
const char* scannerLastCode();
