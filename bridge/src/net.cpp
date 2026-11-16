#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <atomic>

#include "bridge.h"

// Credentials that have not connected for this long bring the setup access
// point up next to the station.
#define PORTAL_AFTER_MS      30000
// While the access point has nobody on it, the station tries again this
// often. Not while someone is on it: a connect attempt hops channels and the
// setup page would stall for the phone using it.
#define PORTAL_RETRY_MS      60000
// Once the station is up, the access point stays this long, so the setup
// page can still show the new address to the phone that sent the password.
#define PORTAL_LINGER_MS     120000
#define SCAN_MAX             20

static DNSServer s_dns;
static bool      s_portal          = false;
static uint32_t  s_portalSince     = 0;
static uint32_t  s_lastTry         = 0;
static uint32_t  s_downSince       = 0;
static uint32_t  s_upSince         = 0;
static bool      s_wasUp           = false;
static bool      s_mdnsUp          = false;
static bool      s_pendingBegin    = false;
static uint32_t  s_pendingAt       = 0;
// Counted on the WiFi event task, read on the loop task.
static std::atomic<uint32_t> s_gotIpCount{0};

// ---------------------------------------------------------------- helpers

String netApName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "Druckbruecke-%02X%02X", mac[4], mac[5]);
  return String(name);
}

bool netConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String netIp() {
  return netConnected() ? WiFi.localIP().toString() : String("");
}

String netSsid() {
  return netConnected() ? WiFi.SSID() : String("");
}

int netRssi() {
  return netConnected() ? WiFi.RSSI() : 0;
}

bool netPortalActive() {
  return s_portal;
}

static void startMdns() {
  if (s_mdnsUp) MDNS.end();
  s_mdnsUp = MDNS.begin(cfg.hostname.c_str());
  if (!s_mdnsUp) {
    logLine("mDNS: start failed");
    return;
  }
  MDNS.addService("http", "tcp", 80);
  // What CUPS and other print clients look for when they browse for raw
  // JetDirect printers.
  MDNS.addService("pdl-datastream", "tcp", 9100);
  logLine("mDNS: %s.local", cfg.hostname.c_str());
}

void netHostnameChanged() {
  WiFi.setHostname(cfg.hostname.c_str());
  if (netConnected()) startMdns();
}

static void startPortal() {
  if (s_portal) return;
  const String name = netApName();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(name.c_str());
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());
  s_portal = true;
  s_portalSince = millis();
  logLine("WiFi: setup access point \"%s\" at %s", name.c_str(), WiFi.softAPIP().toString().c_str());
}

static void stopPortal() {
  if (!s_portal) return;
  s_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  s_portal = false;
  logLine("WiFi: setup access point closed");
}

static void beginStation() {
  if (cfg.ssid.isEmpty()) return;
  logLine("WiFi: connecting to \"%s\"", cfg.ssid.c_str());
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  s_lastTry = millis();
}

// ---------------------------------------------------------------- scan

void netScanJson(JsonArray out) {
  int n = WiFi.scanNetworks(false, false);
  if (n < 0) {
    // A station stuck in a failed connect refuses to scan; stopping the
    // attempt frees the radio. The reconnect logic starts it again.
    WiFi.disconnect(false, false);
    delay(100);
    n = WiFi.scanNetworks(false, false);
  }
  if (n < 0) {
    logLine("WiFi: scan failed (%d)", n);
    return;
  }
  // Strongest first, each SSID once, hidden ones left out.
  int order[64];
  int count = 0;
  for (int i = 0; i < n && count < 64; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (int k = 0; k < count; k++) {
      if (WiFi.SSID(order[k]) == ssid) {
        if (WiFi.RSSI(i) > WiFi.RSSI(order[k])) order[k] = i;
        dup = true;
        break;
      }
    }
    if (!dup) order[count++] = i;
  }
  for (int a = 1; a < count; a++) {
    const int v = order[a];
    int b = a - 1;
    while (b >= 0 && WiFi.RSSI(order[b]) < WiFi.RSSI(v)) {
      order[b + 1] = order[b];
      b--;
    }
    order[b + 1] = v;
  }
  for (int k = 0; k < count && k < SCAN_MAX; k++) {
    JsonObject o = out.add<JsonObject>();
    o["ssid"] = WiFi.SSID(order[k]);
    o["rssi"] = WiFi.RSSI(order[k]);
    o["open"] = WiFi.encryptionType(order[k]) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
}

void netSetCredentials(const String& ssid, const String& pass) {
  configSaveWifi(ssid, pass);
  s_pendingBegin = true;
  s_pendingAt = millis() + 500;
}

// ---------------------------------------------------------------- Improv

// Improv Serial, the protocol ESP Web Tools speaks after an install: frame
// "IMPROV", version, type, length, data, checksum (sum of all bytes before
// it). Ported from the scale's src/services/improv_serial.cpp, without its
// setup screen. Spec: https://www.improv-wifi.com/serial/
#define IMPROV_VERSION            0x01
#define IMPROV_TYPE_STATE         0x01
#define IMPROV_TYPE_ERROR         0x02
#define IMPROV_TYPE_RPC           0x03
#define IMPROV_TYPE_RESULT        0x04
#define IMPROV_STATE_READY        0x02
#define IMPROV_STATE_PROVISIONING 0x03
#define IMPROV_STATE_PROVISIONED  0x04
#define IMPROV_ERR_INVALID_RPC    0x01
#define IMPROV_ERR_UNKNOWN_RPC    0x02
#define IMPROV_ERR_UNABLE_CONNECT 0x03
#define IMPROV_CMD_WIFI_SETTINGS  0x01
#define IMPROV_CMD_GET_STATE      0x02
#define IMPROV_CMD_GET_INFO       0x03
#define IMPROV_CMD_GET_NETWORKS   0x04
#define IMPROV_BYTE_GAP_MS        500
#define IMPROV_CONNECT_TIMEOUT_MS 20000

static const char IMPROV_HEADER[] = "IMPROV";
static uint8_t  s_rx[9 + 255 + 1];
static size_t   s_rxLen = 0;
static uint32_t s_rxLast = 0;
static bool     s_improvConnecting = false;
static uint32_t s_improvStart = 0;
static uint32_t s_improvIpCount = 0;
static String   s_improvSsid;
static String   s_improvPass;

// One Serial.write() per frame so a log line from another task cannot land
// inside it. The leading newline ends any half log line: the browser skips
// to a newline before it looks for the header.
static void improvSend(uint8_t type, const uint8_t* data, size_t len) {
  if (len > 255) return;
  uint8_t out[1 + 9 + 255 + 2];
  size_t n = 0;
  out[n++] = '\n';
  memcpy(out + n, IMPROV_HEADER, 6);
  n += 6;
  out[n++] = IMPROV_VERSION;
  out[n++] = type;
  out[n++] = (uint8_t)len;
  if (len) memcpy(out + n, data, len);
  n += len;
  uint8_t sum = 0;
  for (size_t i = 1; i < n; i++) sum += out[i];
  out[n++] = sum;
  out[n++] = '\n';
  Serial.write(out, n);
}

static void improvState(uint8_t s) { improvSend(IMPROV_TYPE_STATE, &s, 1); }
static void improvError(uint8_t e) { improvSend(IMPROV_TYPE_ERROR, &e, 1); }

struct ImprovResult {
  uint8_t buf[255];
  size_t  len;
  explicit ImprovResult(uint8_t cmd) : len(2) { buf[0] = cmd; buf[1] = 0; }
  // A string that does not fit is left out rather than cut.
  void add(const String& s) {
    if (len + 1 + s.length() > sizeof(buf)) return;
    buf[len++] = (uint8_t)s.length();
    memcpy(buf + len, s.c_str(), s.length());
    len += s.length();
    buf[1] = (uint8_t)(len - 2);
  }
  void send() const { improvSend(IMPROV_TYPE_RESULT, buf, len); }
};

// The address rather than the name: the computer that flashed may not
// resolve .local names.
static void improvUrls(uint8_t cmd) {
  ImprovResult r(cmd);
  if (netConnected()) r.add("http://" + WiFi.localIP().toString() + "/");
  r.send();
}

static void improvRpc(const uint8_t* d, size_t len) {
  if (len < 2 || (size_t)d[1] + 2 > len) {
    improvError(IMPROV_ERR_INVALID_RPC);
    return;
  }
  const uint8_t* p = d + 2;
  const size_t plen = d[1];
  switch (d[0]) {
    case IMPROV_CMD_WIFI_SETTINGS: {
      if (plen < 2) { improvError(IMPROV_ERR_INVALID_RPC); return; }
      const size_t sl = p[0];
      if (sl == 0 || sl > 32 || 1 + sl + 1 > plen) { improvError(IMPROV_ERR_INVALID_RPC); return; }
      const size_t pl = p[1 + sl];
      if (pl > 64 || 1 + sl + 1 + pl > plen) { improvError(IMPROV_ERR_INVALID_RPC); return; }
      s_improvSsid = String((const char*)p + 1, sl);
      s_improvPass = String((const char*)p + 2 + sl, pl);
      logLine("Improv: credentials for \"%s\"", s_improvSsid.c_str());
      improvState(IMPROV_STATE_PROVISIONING);
      s_improvIpCount = s_gotIpCount;
      WiFi.begin(s_improvSsid.c_str(), s_improvPass.c_str());
      s_improvConnecting = true;
      s_improvStart = millis();
      break;
    }
    case IMPROV_CMD_GET_STATE:
      if (s_improvConnecting) {
        improvState(IMPROV_STATE_PROVISIONING);
      } else if (netConnected()) {
        improvState(IMPROV_STATE_PROVISIONED);
        improvUrls(IMPROV_CMD_GET_STATE);
      } else {
        improvState(IMPROV_STATE_READY);
      }
      break;
    case IMPROV_CMD_GET_INFO: {
      ImprovResult r(IMPROV_CMD_GET_INFO);
      r.add(BRIDGE_NAME);
      r.add(BRIDGE_VERSION);
      r.add("ESP32");
      r.add(cfg.hostname);
      r.send();
      break;
    }
    case IMPROV_CMD_GET_NETWORKS: {
      if (!s_improvConnecting) {
        JsonDocument doc;
        JsonArray nets = doc.to<JsonArray>();
        netScanJson(nets);
        for (JsonObject n : nets) {
          ImprovResult r(IMPROV_CMD_GET_NETWORKS);
          r.add(n["ssid"].as<String>());
          r.add(String(n["rssi"].as<int>()));
          r.add(n["open"].as<bool>() ? "NO" : "YES");
          r.send();
        }
      }
      ImprovResult end(IMPROV_CMD_GET_NETWORKS);   // an empty result ends the list
      end.send();
      break;
    }
    default:
      improvError(IMPROV_ERR_UNKNOWN_RPC);
      break;
  }
}

// Anything that is not a frame (a monitor typing, noise) falls out at the
// header check.
static void improvByte(uint8_t b) {
  const uint32_t now = millis();
  if (s_rxLen > 0 && now - s_rxLast > IMPROV_BYTE_GAP_MS) s_rxLen = 0;
  s_rxLast = now;
  if (s_rxLen < 6) {
    if (b == (uint8_t)IMPROV_HEADER[s_rxLen]) {
      s_rx[s_rxLen++] = b;
    } else {
      s_rxLen = (b == (uint8_t)IMPROV_HEADER[0]) ? 1 : 0;
      if (s_rxLen) s_rx[0] = b;
    }
    return;
  }
  s_rx[s_rxLen++] = b;
  if (s_rxLen == 7 && b != IMPROV_VERSION) { s_rxLen = 0; return; }
  if (s_rxLen < 9) return;
  const size_t frame = 9 + s_rx[8] + 1;
  if (s_rxLen < frame) return;
  s_rxLen = 0;
  uint8_t sum = 0;
  for (size_t i = 0; i < frame - 1; i++) sum += s_rx[i];
  if (sum != s_rx[frame - 1]) { improvError(IMPROV_ERR_INVALID_RPC); return; }
  if (s_rx[7] != IMPROV_TYPE_RPC) return;
  improvRpc(s_rx + 9, frame - 10);
}

static void improvTick() {
  for (int budget = 512; budget > 0 && Serial.available() > 0; budget--) {
    const int b = Serial.read();
    if (b < 0) break;
    improvByte((uint8_t)b);
  }
  if (!s_improvConnecting) return;
  // A new address since the attempt began, not just "connected": right
  // after the switch the old link may still be reported.
  if (s_gotIpCount != s_improvIpCount && netConnected()) {
    s_improvConnecting = false;
    configSaveWifi(s_improvSsid, s_improvPass);
    s_improvPass = "";
    logLine("Improv: connected, %s", WiFi.localIP().toString().c_str());
    improvState(IMPROV_STATE_PROVISIONED);
    improvUrls(IMPROV_CMD_WIFI_SETTINGS);
    return;
  }
  if (millis() - s_improvStart < IMPROV_CONNECT_TIMEOUT_MS) return;
  s_improvConnecting = false;
  s_improvPass = "";
  logLine("Improv: could not connect to \"%s\"", s_improvSsid.c_str());
  // Back to the stored network, if there is one; the credentials that did
  // not work are not kept.
  WiFi.disconnect(false, false);
  beginStation();
  improvError(IMPROV_ERR_UNABLE_CONNECT);
  improvState(IMPROV_STATE_READY);
}

// ---------------------------------------------------------------- main

void netBegin() {
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { s_gotIpCount++; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    logLine("WiFi: disconnected (reason %u)", (unsigned)info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.persistent(false);   // NVS of our own; the driver's copy only goes stale
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.mode(WIFI_STA);
  // Modem sleep makes the bridge miss and delay packets (DNS, mDNS, the first
  // bytes of a job). It is powered by USB, so the radio stays on.
  WiFi.setSleep(false);
  s_downSince = millis();
  if (cfg.ssid.isEmpty()) {
    logLine("WiFi: no network stored");
    startPortal();
  } else {
    beginStation();
  }
}

void netLoop() {
  improvTick();
  if (s_portal) s_dns.processNextRequest();

  if (s_pendingBegin && (int32_t)(millis() - s_pendingAt) >= 0) {
    s_pendingBegin = false;
    if (!s_portal) startPortal();   // keeps the page reachable whatever happens
    WiFi.disconnect(false, false);
    beginStation();
  }

  const bool up = netConnected();
  if (up && !s_wasUp) {
    logLine("WiFi: connected to \"%s\", IP %s, RSSI %d", WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
    s_upSince = millis();
    startMdns();
  } else if (!up && s_wasUp) {
    s_downSince = millis();
  }
  s_wasUp = up;

  if (up) {
    if (s_portal && millis() - s_upSince > PORTAL_LINGER_MS && millis() - s_portalSince > PORTAL_LINGER_MS) {
      stopPortal();
    }
    return;
  }
  if (s_improvConnecting) return;
  if (!s_portal && millis() - s_downSince > PORTAL_AFTER_MS) startPortal();
  if (s_portal && !cfg.ssid.isEmpty() && WiFi.softAPgetStationNum() == 0 &&
      millis() - s_lastTry > PORTAL_RETRY_MS) {
    beginStation();
  }
}
