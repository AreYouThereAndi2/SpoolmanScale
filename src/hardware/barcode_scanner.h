#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
//  BARCODE SCANNER (GM861 and anything else that types a line)
//
//  A UART scanner on the extension header. It sends every code it reads as
//  plain text, terminated by CR, LF or both, and that is all this module
//  knows about it: no command set, no trigger. A GM861 left in its default
//  sense mode reads on its own when something is held in front of it, and
//  the few settings worth changing are made by scanning the setup codes in
//  its manual, so the TX line is optional.
//
//  Pins: the extension header brings out GPIO 10 to 14 and 21. 10 and 11
//  are the I2C bus of the reader and the ADC, 12 is the PN532 reset this
//  firmware hands the library on every device whose wire was not moved, and
//  14 is that wire when it was. That leaves 21 for the scanner's TX (our RX)
//  and 13 for our TX: 13 is only the PN532 IRQ placeholder, set to an input
//  once in the constructor and never read in I2C mode. The scanner is
//  started after the reader for exactly that reason - the UART takes the
//  pin over from whatever the constructor left on it.
//
//  Supply: the GM861 is a 3.3 V part with 3.3 V TTL levels. Feed it from
//  the 3.3 V pin; the ESP32-S3 is not 5 V tolerant.
//
//  Reading runs on the loop: the UART driver buffers in its own interrupt,
//  a code is 8 to 100 bytes, and the loop comes round every few ms. A task
//  would buy nothing but a lock around the hand over.
// ============================================================

#define SCANNER_CODE_MAX      128
#define SCANNER_DEFAULT_RX    21     // EXT_IO6, the scanner's TX goes here
#define SCANNER_DEFAULT_TX    13     // EXT_IO4, the scanner's RX, optional
#define SCANNER_DEFAULT_BAUD  9600

extern bool     g_scanner_enabled;   // NVS "scan_on",   default off
extern int8_t   g_scanner_rx_pin;    // NVS "scan_rx",   default 21
extern int8_t   g_scanner_tx_pin;    // NVS "scan_tx",   default 13, -1 = none
extern uint32_t g_scanner_baud;      // NVS "scan_baud", default 9600

// Reads the settings and opens the UART when the scanner is switched on.
// Called once at boot after the NFC reader, and again by scannerApplyPending()
// after a change from the browser.
void scannerBegin();

// Whether a pin may carry the scanner. The header pins that nothing else on
// this board drives, plus -1 for "not connected" on TX.
bool scannerPinAllowed(int pin, bool is_tx);

// Stores new settings and asks the loop to reopen the UART with them. Safe
// from a web handler: nothing but NVS and a flag.
void scannerConfigure(bool enabled, int rx, int tx, uint32_t baud);
void scannerApplyPending();

// True while the UART is open.
bool scannerActive();

// Collects bytes and hands back one finished code, trimmed and terminated.
// A code ends at CR or LF, or after a short pause for a scanner that is set
// to send no terminator. Non-blocking, call every loop pass.
bool scannerPoll(char* out, size_t out_size);

// The last code read and when, for the status page.
const char* scannerLastCode();
uint32_t    scannerLastCodeMs();
uint32_t    scannerCodeCount();
