#pragma once

#include <Arduino.h>

// ============================================================
//  DRUCKBRUECKE (PRINT BRIDGE)
//
//  Takes label jobs over WLAN (HTTP and raw TCP 9100) and hands
//  them to a TYPONOS PM240 over Bluetooth Classic SPP. The PM240
//  prints only over classic Bluetooth; the scale (ESP32-S3) and
//  browsers have BLE at most, so this board sits near the printer
//  and does nothing but forward bytes.
// ============================================================

// Shown in /api/status, the page and to ESP Web Tools. CI compares a
// bridge-v* tag against BRIDGE_VERSION, so the two cannot drift apart.
#define BRIDGE_NAME    "Druckbrücke"
#define BRIDGE_VERSION "0.1.0"

// Defaults for a fresh board.
#define DEFAULT_HOSTNAME    "printbridge"
#define DEFAULT_PRINTER_PIN "0000"

// Everything that survives a restart, mirrored from NVS. Read from several
// tasks, written only by the loop task (web handlers, Improv).
struct BridgeConfig {
  String ssid;
  String pass;
  String hostname;
  String apiKey;
  String printerAddr;   // "80:52:D3:F8:B9:BC", "" when none is chosen
  String printerName;
  String printerPin;
};

extern BridgeConfig cfg;

void configLoad();
void configSaveWifi(const String& ssid, const String& pass);
void configSavePrinter(const String& addr, const String& name, const String& pin);
void configSaveSettings(const String& apiKey, const String& hostname);

// 1..32 of a-z, 0-9 and '-', not starting or ending with '-': what mDNS and
// DHCP accept as a host label without surprises.
bool hostnameValid(const String& h);
// "AA:BB:CC:DD:EE:FF", hex digits in either case.
bool btAddressValid(const String& a);

// One line to Serial with the uptime in front. Serial is shared by the loop,
// the port 9100 task and the Bluetooth callbacks, so a line is written in one
// call and does not tear.
void logLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
