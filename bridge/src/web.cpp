#include "web.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "bridge.h"
#include "net.h"
#include "printer.h"
#include "web_page.h"

// The largest job /api/print takes. The body is streamed to the printer as it
// arrives, so this is a sanity limit, not a buffer: a 50 x 30 mm label is
// about 12 kB, a 100 x 150 mm one about 70 kB.
#define MAX_JOB_BYTES (256 * 1024)

static WebServer server(80);
static uint32_t  s_restartAt = 0;

// ---------------------------------------------------------------- helpers

// Every answer carries this: the scale and the filament_to_bambuddy page call
// from other origins, and a browser would otherwise hide the answer from them.
static void cors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
}

static void sendJson(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  cors();
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void sendError(int code, const char* error) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = error;
  sendJson(code, doc);
}

static void sendOk() {
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

// Compares in constant time, so the answer time does not tell how many
// leading characters of a guess were right.
static bool keyOk() {
  if (cfg.apiKey.isEmpty()) return true;
  const String got = server.header("X-Api-Key");
  if (got.length() != cfg.apiKey.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < got.length(); i++) diff |= (uint8_t)(got[i] ^ cfg.apiKey[i]);
  return diff == 0;
}

static bool requireKey() {
  if (keyOk()) return true;
  sendError(401, "api key");
  return false;
}

static bool parseBody(JsonDocument& doc) {
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok || !doc.is<JsonObject>()) {
    sendError(400, "bad json");
    return false;
  }
  return true;
}

static void sendJobResult(const JobResult& r) {
  if (r.status != JOB_OK) {
    sendError(jobHttpStatus(r.status), jobErrorText(r.status));
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["bytes"] = r.bytes;
  doc["ms"] = r.ms;
  sendJson(200, doc);
}

// ---------------------------------------------------------------- status

static void handleStatus() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["name"] = BRIDGE_NAME;
  doc["version"] = BRIDGE_VERSION;
  printerStatusJson(doc["printer"].to<JsonObject>());
  doc["busy"] = printerBusy();
  doc["jobs"] = printerJobCount();
  if (!printerLastJobJson(doc["last"].to<JsonObject>())) doc["last"] = nullptr;
  // Beyond the spec, for the page: what it shows and fills in.
  doc["hostname"] = cfg.hostname;
  doc["auth"] = !cfg.apiKey.isEmpty();
  doc["uptime"] = millis() / 1000;
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = netSsid();
  w["ip"] = netIp();
  w["rssi"] = netRssi();
  w["ap"] = netPortalActive();
  w["ap_name"] = netPortalActive() ? netApName() : String("");
  doc["heap"] = ESP.getFreeHeap();
  sendJson(200, doc);
}

// ---------------------------------------------------------------- print

// The body of /api/print is not collected: WebServer hands it over in pieces
// of up to 1436 bytes (its "raw" mode for bodies that are not forms), and each
// piece goes to the printer before the next is read. TCP holds the sender
// back while the printer is slower than the network.
struct RawJob {
  bool        started;
  bool        active;
  int         code;
  const char* error;
  JobResult   result;
};
static RawJob s_raw;

static void printBody() {
  // Also called for a multipart upload; that is not a job.
  if (!server.hasRaw()) return;
  HTTPRaw& raw = server.raw();
  switch (raw.status) {
    case RAW_START: {
      s_raw = RawJob();
      s_raw.started = true;
      const int len = server.clientContentLength();
      logLine("HTTP: print job, %d bytes from %s", len, server.client().remoteIP().toString().c_str());
      if (!keyOk()) {
        s_raw.code = 401;
        s_raw.error = "api key";
      } else if (len <= 0) {
        s_raw.code = 400;
        s_raw.error = "empty";
      } else if (len > MAX_JOB_BYTES) {
        s_raw.code = 413;
        s_raw.error = "too large";
      } else {
        const JobStatus st = jobBegin();
        if (st == JOB_OK) {
          s_raw.active = true;
        } else {
          s_raw.code = jobHttpStatus(st);
          s_raw.error = jobErrorText(st);
        }
      }
      break;
    }
    case RAW_WRITE:
      // A failed write marks the job; the rest of the body is read and
      // dropped so the answer can still be sent.
      if (s_raw.active) jobWrite(raw.buf, raw.currentSize);
      break;
    case RAW_END:
      if (s_raw.active) {
        s_raw.active = false;
        s_raw.result = jobEnd(false);
        s_raw.code = jobHttpStatus(s_raw.result.status);
        s_raw.error = jobErrorText(s_raw.result.status);
      }
      break;
    case RAW_ABORTED:
      if (s_raw.active) {
        s_raw.active = false;
        s_raw.result = jobEnd(true);
      }
      // WebServer sends no answer after an abort, so nothing reads this.
      s_raw.started = false;
      break;
  }
}

static void handlePrint() {
  if (!s_raw.started) {
    // No Content-Length (chunked) or a form: nothing came through raw mode.
    if (requireKey()) sendError(400, "empty");
    return;
  }
  s_raw.started = false;
  if (s_raw.code == 200) {
    sendJobResult(s_raw.result);
  } else {
    sendError(s_raw.code, s_raw.error);
  }
}

static void handleTest() {
  if (!requireKey()) return;
  const String ip = netConnected() ? netIp() : WiFi.softAPIP().toString();
  const String job = buildTestLabel(ip, cfg.hostname);
  logLine("HTTP: test label");
  sendJobResult(printBuffer((const uint8_t*)job.c_str(), job.length()));
}

// ---------------------------------------------------------------- printer

static void handleScan() {
  if (!requireKey()) return;
  JsonDocument doc;
  doc["ok"] = true;
  if (!printerScan(doc["devices"].to<JsonArray>())) {
    sendError(409, "busy");
    return;
  }
  sendJson(200, doc);
}

static void handlePrinter() {
  if (!requireKey()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  if (!doc["address"].is<const char*>()) {
    sendError(400, "address");
    return;
  }
  String addr = doc["address"].as<String>();
  addr.trim();
  addr.toUpperCase();
  if (!addr.isEmpty() && !btAddressValid(addr)) {
    sendError(400, "address");
    return;
  }
  String name = addr.isEmpty() ? String("") : String(doc["name"] | "");
  String pin = addr.isEmpty() ? String(DEFAULT_PRINTER_PIN) : String(doc["pin"] | DEFAULT_PRINTER_PIN);
  name.trim();
  pin.trim();
  if (name.length() > 48) name = name.substring(0, 48);
  if (pin.isEmpty()) pin = DEFAULT_PRINTER_PIN;
  if (pin.length() > 16) {
    sendError(400, "pin");
    return;
  }
  if (!printerSetConfig(addr, name, pin)) {
    sendError(409, "busy");
    return;
  }
  logLine("Printer: %s", addr.isEmpty() ? "forgotten" : (addr + " \"" + name + "\"").c_str());
  sendOk();
}

// ---------------------------------------------------------------- settings

static void handleSettings() {
  if (!requireKey()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  String key = cfg.apiKey;
  String host = cfg.hostname;
  if (doc["api_key"].is<const char*>()) {
    key = doc["api_key"].as<String>();
    key.trim();
    if (key.length() > 64) {
      sendError(400, "api_key");
      return;
    }
  }
  if (doc["hostname"].is<const char*>()) {
    host = doc["hostname"].as<String>();
    host.trim();
    host.toLowerCase();
    if (!hostnameValid(host)) {
      sendError(400, "hostname");
      return;
    }
  }
  const bool hostChanged = host != cfg.hostname;
  const bool keyChanged = key != cfg.apiKey;
  configSaveSettings(key, host);
  if (keyChanged) logLine("Settings: API key %s", key.isEmpty() ? "removed" : "set");
  // Answered first: the new mDNS name takes a moment, the page is waiting.
  sendOk();
  if (hostChanged) {
    logLine("Settings: hostname %s", host.c_str());
    netHostnameChanged();
  }
}

static void handleWifiScan() {
  if (!requireKey()) return;
  JsonDocument doc;
  doc["ok"] = true;
  netScanJson(doc["networks"].to<JsonArray>());
  sendJson(200, doc);
}

static void handleWifi() {
  if (!requireKey()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  const String ssid = doc["ssid"] | "";
  const String pass = doc["pass"] | "";
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 64) {
    sendError(400, "ssid");
    return;
  }
  logLine("Settings: WLAN \"%s\"", ssid.c_str());
  sendOk();
  netSetCredentials(ssid, pass);
}

// ---------------------------------------------------------------- update

static bool   s_updAllowed = false;
static bool   s_updSeen = false;
static String s_updError;

static void updateBody() {
  // Also called for a raw body; only a multipart file is firmware.
  if (!server.hasUpload()) return;
  HTTPUpload& up = server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      s_updSeen = true;
      s_updError = "";
      s_updAllowed = keyOk();
      if (!s_updAllowed) return;
      logLine("Update: %s", up.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) s_updError = Update.errorString();
      break;
    case UPLOAD_FILE_WRITE:
      if (s_updAllowed && s_updError.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize) {
        s_updError = Update.errorString();
      }
      break;
    case UPLOAD_FILE_END:
      if (s_updAllowed && s_updError.isEmpty()) {
        if (Update.end(true)) {
          logLine("Update: %u bytes written", (unsigned)up.totalSize);
        } else {
          s_updError = Update.errorString();
        }
      }
      break;
    case UPLOAD_FILE_ABORTED:
      // No answer follows an abort; the next upload starts clean.
      if (s_updAllowed) Update.abort();
      s_updSeen = false;
      break;
  }
}

static void handleUpdate() {
  const bool seen = s_updSeen;
  s_updSeen = false;
  if (!seen) {
    if (requireKey()) sendError(400, "no file");
    return;
  }
  if (!s_updAllowed) {
    sendError(401, "api key");
    return;
  }
  if (!s_updError.isEmpty()) {
    logLine("Update: failed, %s", s_updError.c_str());
    sendError(500, s_updError.c_str());
    return;
  }
  sendOk();
  // A moment for the answer to leave before the restart cuts the link.
  s_restartAt = millis() + 1000;
}

// ---------------------------------------------------------------- rest

static void handleNotFound() {
  if (server.method() == HTTP_OPTIONS) {
    // CORS preflight: a page elsewhere asking whether it may POST with
    // X-Api-Key. Chrome also asks before a public page reaches into the LAN
    // (Private Network Access); the extra header says yes to that.
    cors();
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Api-Key");
    server.sendHeader("Access-Control-Max-Age", "600");
    if (server.header("Access-Control-Request-Private-Network") == "true") {
      server.sendHeader("Access-Control-Allow-Private-Network", "true");
    }
    server.send(204);
    return;
  }
  // Captive portal: a phone on the setup access point probes some well-known
  // address; sending it here makes it open the setup page by itself.
  if (netPortalActive()) {
    const String host = server.hostHeader();
    const String ap = WiFi.softAPIP().toString();
    if (host != ap && host != netIp() && host != cfg.hostname + ".local") {
      server.sendHeader("Location", "http://" + ap + "/");
      server.send(302, "text/plain", "");
      return;
    }
  }
  if (server.uri().startsWith("/api/")) {
    sendError(404, "not found");
    return;
  }
  server.send(404, "text/plain", "Nicht gefunden");
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", WEB_PAGE, sizeof(WEB_PAGE) - 1);
}

void webBegin() {
  static const char* headers[] = { "X-Api-Key", "Access-Control-Request-Private-Network" };
  server.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
  server.on("/", HTTP_GET, handleRoot);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/print", HTTP_POST, handlePrint, printBody);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/printer", HTTP_POST, handlePrinter);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/update", HTTP_POST, handleUpdate, updateBody);
  server.onNotFound(handleNotFound);
  server.begin();
  logLine("HTTP: port 80");
}

void webLoop() {
  server.handleClient();
  if (s_restartAt && (int32_t)(millis() - s_restartAt) >= 0) {
    logLine("Restarting");
    Serial.flush();
    ESP.restart();
  }
}
