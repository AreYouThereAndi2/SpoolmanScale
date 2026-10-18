#include "f2b_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/backend_http.h"
#include "services/prefs_store.h"

char g_f2b_url[96] = "";

void f2bLoadSettings() {
  String u = prefsGetString("f2b_url", "");
  u.trim();
  while (u.endsWith("/")) u.remove(u.length() - 1);
  snprintf(g_f2b_url, sizeof(g_f2b_url), "%s", u.c_str());
}

void f2bSetUrl(const char* url) {
  prefsPutString("f2b_url", url ? url : "");
  f2bLoadSettings();
}

static String urlEncode(const char* s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (; *s; s++) {
    const unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

static void copyStr(char* dst, size_t n, const char* src) {
  snprintf(dst, n, "%s", src ? src : "");
}

int f2bLookup(const char* code, F2bProduct& out, uint32_t timeout_ms) {
  out = F2bProduct{};
  if (!g_f2b_url[0] || !code || !code[0]) return -1;

  String url = String(g_f2b_url) + "/api/lookup?barcode=" + urlEncode(code);
  BackendHttp http;
  if (!http.begin(url)) return -1;
  http.setTimeout(timeout_ms);
  const int code_http = http.GET();
  if (code_http != 200) {
    http.end();
    logSDf("f2b: lookup %s answered %d", code, code_http);
    return code_http;
  }
  JsonDocument filter;
  filter["barcode"] = true;
  filter["source"]  = true;
  filter["fields"]  = true;
  filter["price"]   = true;
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    logSDf("f2b: answer did not parse: %s", err.c_str());
    return -2;
  }

  JsonObjectConst f = doc["fields"];
  copyStr(out.source,     sizeof(out.source),     doc["source"] | "");
  copyStr(out.barcode,    sizeof(out.barcode),    doc["barcode"] | code);
  copyStr(out.brand,      sizeof(out.brand),      f["brand"] | "");
  copyStr(out.material,   sizeof(out.material),   f["material"] | "");
  copyStr(out.subtype,    sizeof(out.subtype),    f["subtype"] | "");
  copyStr(out.color_name, sizeof(out.color_name), f["color_name"] | "");
  copyStr(out.rgba,       sizeof(out.rgba),       f["rgba"] | "");
  out.label_weight = f["label_weight"] | 0;
  out.core_weight  = f["core_weight"] | 0;
  out.temp_min     = f["nozzle_temp_min"] | 0;
  out.temp_max     = f["nozzle_temp_max"] | 0;
  // In Spoolman mode the app looks in the user's own filaments first; the
  // spool is then made on the one it found.
  out.spoolman_filament_id = f["spoolman_filament_id"] | 0;
  // Only a UPCitemdb answer carries one: the mean of the shop offers it found
  // for the code, in the currency most of them use.
  JsonObjectConst pr = doc["price"];
  out.price_avg    = pr["avg"] | 0.0f;
  out.price_offers = pr["count"] | 0;
  copyStr(out.price_cur, sizeof(out.price_cur), pr["currency"] | "");
  if (out.price_avg < 0.0f) out.price_avg = 0.0f;
  out.found = out.material[0] != '\0' && strcmp(out.source, "none") != 0 &&
              strcmp(out.source, "amazon") != 0;
  logSDf("f2b: %s -> %s, %s %s %s %s, %d g, %.2f %s", code, out.source, out.brand,
         out.material, out.subtype, out.color_name, out.label_weight, out.price_avg,
         out.price_cur);
  return 200;
}
