#include "bridge.h"

#include <Preferences.h>
#include <stdarg.h>

BridgeConfig cfg;

// One namespace for everything. NVS survives an OTA update and a web flash
// without "erase", so a board keeps WLAN, key and printer across versions.
static const char* NVS_NS = "bridge";

void configLoad() {
  Preferences p;
  p.begin(NVS_NS, true);
  cfg.ssid        = p.getString("ssid", "");
  cfg.pass        = p.getString("pass", "");
  cfg.hostname    = p.getString("host", DEFAULT_HOSTNAME);
  cfg.apiKey      = p.getString("key", "");
  cfg.printerAddr = p.getString("bt_addr", "");
  cfg.printerName = p.getString("bt_name", "");
  cfg.printerPin  = p.getString("bt_pin", DEFAULT_PRINTER_PIN);
  p.end();
  if (!hostnameValid(cfg.hostname)) cfg.hostname = DEFAULT_HOSTNAME;
  if (cfg.printerPin.isEmpty()) cfg.printerPin = DEFAULT_PRINTER_PIN;
}

void configSaveWifi(const String& ssid, const String& pass) {
  cfg.ssid = ssid;
  cfg.pass = pass;
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
}

void configSavePrinter(const String& addr, const String& name, const String& pin) {
  cfg.printerAddr = addr;
  cfg.printerName = name;
  cfg.printerPin  = pin.isEmpty() ? String(DEFAULT_PRINTER_PIN) : pin;
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("bt_addr", cfg.printerAddr);
  p.putString("bt_name", cfg.printerName);
  p.putString("bt_pin", cfg.printerPin);
  p.end();
}

void configSaveSettings(const String& apiKey, const String& hostname) {
  cfg.apiKey   = apiKey;
  cfg.hostname = hostname;
  Preferences p;
  p.begin(NVS_NS, false);
  p.putString("key", apiKey);
  p.putString("host", hostname);
  p.end();
}

bool hostnameValid(const String& h) {
  if (h.length() < 1 || h.length() > 32) return false;
  if (h[0] == '-' || h[h.length() - 1] == '-') return false;
  for (size_t i = 0; i < h.length(); i++) {
    const char c = h[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

bool btAddressValid(const String& a) {
  if (a.length() != 17) return false;
  for (size_t i = 0; i < 17; i++) {
    const char c = a[i];
    if (i % 3 == 2) {
      if (c != ':') return false;
    } else if (!isxdigit((unsigned char)c)) {
      return false;
    }
  }
  return true;
}

void logLine(const char* fmt, ...) {
  char buf[256];
  const unsigned long ms = millis();
  int n = snprintf(buf, sizeof(buf), "[%6lu.%03lu] ", ms / 1000, ms % 1000);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
  va_end(ap);
  Serial.println(buf);
}
