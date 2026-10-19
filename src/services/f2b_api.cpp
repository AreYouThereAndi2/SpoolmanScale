#include "f2b_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>
#include <esp_heap_caps.h>

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
  serializeJson(f, out.fields_json);
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
  // The empty spool's weight is the filament's: the spool inherits it, so it
  // is not copied onto the spool, where a correction would not reach it.
  out.core_from_filament = f["core_weight_from_filament"] | false;
  // The profile imported into BamBuddy that fits (any printer model's; the
  // phone app adds the other models' when it links profiles).
  copyStr(out.slicer_filament,      sizeof(out.slicer_filament),      f["slicer_filament"] | "");
  copyStr(out.slicer_filament_name, sizeof(out.slicer_filament_name), f["slicer_filament_name"] | "");
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

int f2bCreateSpool(const F2bProduct& p, float cost_per_kg, int* out_spool_id,
                   char* why, size_t why_n, uint32_t timeout_ms) {
  if (out_spool_id) *out_spool_id = 0;
  if (why && why_n) why[0] = '\0';
  if (!g_f2b_url[0]) return -1;

  JsonDocument body;
  body["barcode"] = p.barcode;
  body["quantity"] = 1;
  JsonObject f = body["fields"].to<JsonObject>();
  JsonDocument fields;
  if (p.fields_json.length() && !deserializeJson(fields, p.fields_json)) f.set(fields.as<JsonObjectConst>());
  // The fields the scale knows better than the lookup did, or only knows.
  if (!f["material"].is<const char*>() || !(f["material"] | "")[0]) f["material"] = p.material;
  if (cost_per_kg > 0.0f) f["cost_per_kg"] = roundf(cost_per_kg * 100.0f) / 100.0f;
  if (p.spoolman_filament_id > 0) f["spoolman_filament_id"] = p.spoolman_filament_id;
  String payload;
  serializeJson(body, payload);

  BackendHttp http;
  if (!http.begin(String(g_f2b_url) + "/api/spool")) return -1;
  http.setTimeout(timeout_ms);
  http.addHeader("Content-Type", "application/json");
  const int code = http.sendRequest("POST", (uint8_t*)payload.c_str(), payload.length());
  if (code <= 0) {
    http.end();
    logSDf("f2b: create %s -> transport error %d", p.barcode, code);
    return code;
  }
  JsonDocument doc;
  JsonDocument filter;
  filter["ok"] = true;
  filter["spool_ids"] = true;
  filter["errors"] = true;
  filter["error"] = true;
  filter["profile"] = true;
  const DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  const int id = doc["spool_ids"][0] | 0;
  if (code == 200 && id > 0) {
    if (out_spool_id) *out_spool_id = id;
    logSDf("f2b: spool %d created from %s, profile %s", id, p.barcode, doc["profile"] | "-");
    return 200;
  }
  // The app's reason, or BamBuddy's as the app passed it on.
  const char* reason = doc["errors"][0] | (doc["error"] | (err ? "unreadable answer" : ""));
  if (why && why_n) snprintf(why, why_n, "%s", reason);
  logSDf("f2b: create %s -> %d %s", p.barcode, code, reason);
  return code == 200 ? -2 : code;
}

int f2bFetchLabel(int spool_id, uint16_t w_mm, uint16_t l_mm, uint8_t** out_bits,
                  size_t* out_len, uint16_t* out_w, uint16_t* out_h, uint32_t timeout_ms) {
  *out_bits = nullptr;
  *out_len = 0;
  if (!g_f2b_url[0] || spool_id <= 0) return -1;
  char url[192];
  snprintf(url, sizeof(url), "%s/api/label/%d.bits?w=%u&l=%u", g_f2b_url, spool_id,
           (unsigned)w_mm, (unsigned)l_mm);
  BackendHttp http;
  if (!http.begin(url)) return -1;
  http.setTimeout(timeout_ms);
  const char* keys[] = {"X-Width", "X-Height"};
  http.collectHeaders(keys, 2);
  const int code = http.GET();
  if (code != 200) {
    http.end();
    logSDf("f2b: label %d -> %d", spool_id, code);
    return code;
  }
  const int w = http.header("X-Width").toInt(), h = http.header("X-Height").toInt();
  const size_t want = size_t((w + 7) / 8) * (size_t)h;
  if (w <= 0 || h <= 0 || want == 0 || want > 256u * 1024u) {
    http.end();
    logSDf("f2b: label %d has no usable size (%dx%d)", spool_id, w, h);
    return -2;
  }
  uint8_t* bits = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM);
  if (!bits) { http.end(); return -3; }
  Stream* s = http.getStreamPtr();
  size_t got = 0;
  const uint32_t until = millis() + timeout_ms;
  while (got < want && millis() < until && (http.connected() || s->available())) {
    const int n = s->readBytes(bits + got, want - got);
    if (n > 0) got += (size_t)n;
    else delay(5);
  }
  http.end();
  if (got != want) {
    heap_caps_free(bits);
    logSDf("f2b: label %d cut short, %u of %u bytes", spool_id, (unsigned)got, (unsigned)want);
    return -4;
  }
  *out_bits = bits;
  *out_len = want;
  *out_w = (uint16_t)w;
  *out_h = (uint16_t)h;
  return 200;
}
