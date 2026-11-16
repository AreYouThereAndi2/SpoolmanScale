#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ============================================================
//  WLAN, SETUP ACCESS POINT, mDNS, IMPROV SERIAL
//
//  No credentials: an open access point "Druckbruecke-XXXX" with a
//  DNS server that answers every name with the bridge, so a phone
//  opens the setup page by itself. Credentials that do not work
//  for 30 s bring the same access point up next to the station,
//  so a moved router does not need a USB cable. ESP Web Tools can
//  hand over the credentials right after flashing (Improv Serial).
// ============================================================

void netBegin();
// From loop(), every pass: Improv, DNS, reconnect, portal on/off, mDNS.
void netLoop();

bool   netConnected();
String netIp();          // station address, "" when not connected
String netSsid();
int    netRssi();
bool   netPortalActive();
String netApName();

// Nearby networks, strongest first, each SSID once.
void netScanJson(JsonArray out);

// Stores new credentials and connects with them shortly after, so the HTTP
// answer that brought them still goes out on the old link.
void netSetCredentials(const String& ssid, const String& pass);

// Re-announces mDNS under cfg.hostname.
void netHostnameChanged();
