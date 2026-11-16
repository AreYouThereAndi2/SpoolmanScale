#pragma once

#include <stddef.h>
#include <stdint.h>

#include "services/ble_service.h"
#include "services/label_raster.h"

// ============================================================
//  THE PRINT BRIDGE
//
//  An ESP32 with classic Bluetooth next to a printer that only
//  prints over SPP (the TYPONOS PM240): it takes a job over the
//  WLAN and hands it on. Its firmware and setup live in bridge/
//  of this repository; this is the scale's side of its HTTP API.
//
//    GET  /api/status   reachable? which printer, busy?
//    POST /api/print    body = the raw TSPL job, answered when the
//                       bytes are out (up to 30 s)
//
//  The job is the same TSPL the BLE path builds
//  (services/tspl_printer.h). An API key, when one is set on the
//  bridge, goes in X-Api-Key.
//
//  Plain sockets rather than HTTPClient: the print blocks the loop
//  for seconds, and the print card has to keep turning while the
//  body goes out and while the bridge talks to the printer, which
//  HTTPClient's blocking send and header wait would not allow.
// ============================================================

enum PrintBridgeResult : uint8_t {
  PB_OK = 0,
  PB_NO_HOST,         // no address set
  PB_NO_MEMORY,       // the job could not be built
  PB_UNREACHABLE,     // the name did not resolve, or no connection
  PB_SEND_FAILED,     // the connection dropped while the job went out
  PB_NO_ANSWER,       // all sent, no answer in time
  PB_NO_PRINTER,      // 412: no printer chosen on the bridge
  PB_PRINTER_FAILED,  // 502: the bridge could not reach or write the printer
  PB_AUTH,            // 401: API key wrong or missing
  PB_BUSY,            // 409: another job is running
  PB_REFUSED,         // any other answer
};

// Blocking, from appLoop(): builds the TSPL job for the raster and posts it.
// progress keeps the print card turning; printBridgePhase() says where it is.
PrintBridgeResult printBridgePrint(const char* host, const char* api_key,
                                   const LabelRaster& image, uint16_t media_w_mm,
                                   uint16_t media_l_mm, BleProgressFn progress);

// Where a print stands, in the BLE session's terms so the print card needs
// no second table: CONNECT, SEND (with printBridgeBytes), AWAIT. IDLE outside.
BleSessionPhase printBridgePhase();
void printBridgeBytes(size_t* sent, size_t* total);

// What GET /api/status said. Blocking for up to a few seconds; safe on the
// web job task (it touches no LVGL, no NVS).
struct PrintBridgeStatus {
  int  http;               // the HTTP code, or <= 0 when there was no answer
  bool reachable;          // answered with ok:true
  bool printer_set;        // the bridge has a printer chosen
  bool printer_connected;  // its SPP link is up right now
  bool busy;
  int  battery;            // percent, -1 when unknown
  char version[16];
  char printer_name[32];
  char printer_address[18];
};
void printBridgeStatus(const char* host, const char* api_key, PrintBridgeStatus* out);
