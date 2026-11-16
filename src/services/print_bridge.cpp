#include "services/print_bridge.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "services/mdns_service.h"
#include "services/tspl_printer.h"

// The bridge answers a print when the bytes are out at the printer, within
// its own 30 s; a little more here so its answer, not ours, decides.
#define PB_PRINT_WAIT_MS    35000
// A status is one small JSON answer from the LAN.
#define PB_STATUS_WAIT_MS    4000
#define PB_CONNECT_MS        5000
// lwIP takes about one segment at a time on this board; smaller pieces keep
// the progress moving, larger ones gain nothing.
#define PB_CHUNK            1436
// A send that makes no headway this long is a dead link, not a slow one.
#define PB_STALL_MS         8000
// How long a .local name is asked for over mDNS when lwIP did not find it.
#define PB_MDNS_QUERY_MS    2500
// Enough for the status JSON and any error answer; the rest is dropped.
#define PB_ANSWER_MAX        768

static volatile BleSessionPhase s_phase = BLE_PHASE_IDLE;
static volatile size_t s_sent = 0, s_total = 0;

BleSessionPhase printBridgePhase() { return s_phase; }
void printBridgeBytes(size_t* sent, size_t* total) {
  if (sent) *sent = s_sent;
  if (total) *total = s_total;
}

// "printbridge.local", "192.168.0.50:8080", also "http://host/" as someone
// may paste it: the host and the port, nothing else.
static bool splitHost(const char* in, char* host, size_t n, uint16_t* port) {
  *port = 80;
  host[0] = '\0';
  if (!in) return false;
  while (*in == ' ') in++;
  if (strncmp(in, "http://", 7) == 0) in += 7;
  size_t k = 0;
  for (; *in && *in != '/' && *in != ':' && *in != ' ' && k + 1 < n; in++) host[k++] = *in;
  host[k] = '\0';
  if (*in == ':') {
    const long p = strtol(in + 1, nullptr, 10);
    if (p <= 0 || p > 65535) return false;
    *port = (uint16_t)p;
  }
  return k > 0;
}

// lwIP asks for a ".local" name over mDNS itself in this core
// (CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES), the same lookup the backend's
// client uses. Where that comes back empty, and the scale's own responder is
// up, the responder is asked directly: it retries and listens longer, which
// a bridge that just joined the network sometimes needs.
static bool resolve(const char* host, IPAddress* ip) {
  if (ip->fromString(host)) return true;
  if (Network.hostByName(host, *ip) == 1 && *ip != IPAddress((uint32_t)0)) return true;
  const size_t n = strlen(host);
  if (n > 6 && strcasecmp(host + n - 6, ".local") == 0 && mdnsRunning()) {
    const String label = String(host).substring(0, n - 6);
    *ip = MDNS.queryHost(label, PB_MDNS_QUERY_MS);
    if (*ip != IPAddress((uint32_t)0)) {
      logSDf("Bridge: %s found over mDNS at %s", host, ip->toString().c_str());
      return true;
    }
  }
  logSDf("Bridge: %s does not resolve", host);
  return false;
}

// A request outcome below the HTTP layer.
#define PB_ERR_RESOLVE  -1
#define PB_ERR_CONNECT  -2
#define PB_ERR_SEND     -3
#define PB_ERR_TIMEOUT  -4

// One request, Connection: close. Returns the HTTP status code or a PB_ERR_.
// The answer's body, up to out_n - 1 bytes, lands in out. progress is called
// between pieces and while waiting, so a card keeps turning. Only a request
// with progress reports its phase: a status check on the web worker must not
// move the print card of a print running at the same time.
static int request(const char* host_in, const char* api_key, const char* method,
                   const char* path, const uint8_t* body, size_t len, uint32_t wait_ms,
                   char* out, size_t out_n, BleProgressFn progress) {
  if (out && out_n) out[0] = '\0';
  char host[72];   // LP_BRIDGE_HOST_LEN and then some
  uint16_t port = 80;
  if (!splitHost(host_in, host, sizeof(host), &port)) return PB_ERR_RESOLVE;
  const bool track = progress != nullptr;
  if (track) s_phase = BLE_PHASE_CONNECT;
  if (progress) progress();
  IPAddress ip;
  if (!resolve(host, &ip)) return PB_ERR_RESOLVE;
  if (progress) progress();

  WiFiClient client;
  if (!client.connect(ip, port, PB_CONNECT_MS)) {
    logSDf("Bridge: no connection to %s (%s:%u)", host, ip.toString().c_str(), (unsigned)port);
    return PB_ERR_CONNECT;
  }
  client.setNoDelay(true);

  char head[256];
  int n = snprintf(head, sizeof(head),
                   "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
                   "User-Agent: SpoolmanScale\r\n",
                   method, path, host);
  if (body) n += snprintf(head + n, sizeof(head) - n,
                          "Content-Type: application/octet-stream\r\nContent-Length: %u\r\n",
                          (unsigned)len);
  if (api_key && api_key[0]) n += snprintf(head + n, sizeof(head) - n, "X-Api-Key: %s\r\n", api_key);
  n += snprintf(head + n, sizeof(head) - n, "\r\n");
  if (n <= 0 || n >= (int)sizeof(head) || client.write((const uint8_t*)head, n) != (size_t)n) {
    client.stop();
    return PB_ERR_SEND;
  }

  // The body straight out of its buffer, PSRAM included: the socket copies.
  size_t sent = 0;
  if (track) { s_total = len; s_sent = 0; }
  if (body && len) {
    if (track) s_phase = BLE_PHASE_SEND;
    unsigned long moved = millis();
    while (sent < len) {
      const size_t piece = len - sent < PB_CHUNK ? len - sent : PB_CHUNK;
      const size_t w = client.write(body + sent, piece);
      if (w > 0) { sent += w; moved = millis(); if (track) s_sent = sent; }
      else if (!client.connected() || millis() - moved > PB_STALL_MS) {
        logSDf("Bridge: send broke off at %u of %u bytes", (unsigned)sent, (unsigned)len);
        client.stop();
        return PB_ERR_SEND;
      } else delay(5);
      if (progress) progress();
    }
  }

  // The answer: status line, headers, body; the bridge closes after it.
  if (track) s_phase = BLE_PHASE_AWAIT;
  const unsigned long t0 = millis();
  char line[128];
  size_t ll = 0;
  int code = 0;
  bool in_body = false;
  size_t got = 0;
  while (millis() - t0 < wait_ms) {
    if (progress) progress();
    const int avail = client.available();
    if (avail <= 0) {
      if (!client.connected()) break;
      delay(10);
      continue;
    }
    const int c = client.read();
    if (c < 0) continue;
    if (in_body) {
      if (out && got + 1 < out_n) out[got++] = (char)c;
      continue;
    }
    if (c == '\n') {
      line[ll] = '\0';
      if (ll && line[ll - 1] == '\r') line[--ll] = '\0';
      if (code == 0) {
        // "HTTP/1.1 200 OK"
        const char* sp = strchr(line, ' ');
        code = sp ? atoi(sp + 1) : -1;
      } else if (ll == 0) {
        in_body = true;
      }
      ll = 0;
    } else if (ll + 1 < sizeof(line)) {
      line[ll++] = (char)c;
    }
  }
  client.stop();
  if (out && out_n) out[got < out_n ? got : out_n - 1] = '\0';
  if (code <= 0) {
    logSDf("Bridge: %s %s sent, no answer in %u ms", method, path, (unsigned)(millis() - t0));
    return PB_ERR_TIMEOUT;
  }
  return code;
}

PrintBridgeResult printBridgePrint(const char* host, const char* api_key,
                                   const LabelRaster& image, uint16_t media_w_mm,
                                   uint16_t media_l_mm, BleProgressFn progress) {
  if (!host || !host[0]) return PB_NO_HOST;
  TsplJob job{};
  if (!tsplBuildJob(image, media_w_mm, media_l_mm, &job)) return PB_NO_MEMORY;
  const unsigned long t0 = millis();
  char answer[PB_ANSWER_MAX];
  const int code = request(host, api_key, "POST", "/api/print", job.data, job.len,
                           PB_PRINT_WAIT_MS, answer, sizeof(answer), progress);
  const size_t len = job.len;
  tsplFreeJob(&job);
  s_phase = BLE_PHASE_IDLE;
  logSDf("Bridge: print %u bytes to %s -> %d in %lu ms %s", (unsigned)len, host, code,
         millis() - t0, answer);
  switch (code) {
    case 200:            return PB_OK;
    case 401:            return PB_AUTH;
    case 409:            return PB_BUSY;
    case 412:            return PB_NO_PRINTER;
    case 502:            return PB_PRINTER_FAILED;
    case PB_ERR_RESOLVE:
    case PB_ERR_CONNECT: return PB_UNREACHABLE;
    case PB_ERR_SEND:    return PB_SEND_FAILED;
    case PB_ERR_TIMEOUT: return PB_NO_ANSWER;
    default:             return PB_REFUSED;
  }
}

void printBridgeStatus(const char* host, const char* api_key, PrintBridgeStatus* out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  out->battery = -1;
  if (!host || !host[0]) return;
  char answer[PB_ANSWER_MAX];
  out->http = request(host, api_key, "GET", "/api/status", nullptr, 0, PB_STATUS_WAIT_MS,
                      answer, sizeof(answer), nullptr);
  if (out->http != 200) {
    logSDf("Bridge: status from %s -> %d", host, out->http);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, answer)) {
    logSDf("Bridge: status from %s is not JSON", host);
    return;
  }
  out->reachable = doc["ok"] | false;
  out->busy = doc["busy"] | false;
  snprintf(out->version, sizeof(out->version), "%s", (const char*)(doc["version"] | ""));
  JsonVariantConst p = doc["printer"];
  snprintf(out->printer_name, sizeof(out->printer_name), "%s", (const char*)(p["name"] | ""));
  snprintf(out->printer_address, sizeof(out->printer_address), "%s",
           (const char*)(p["address"] | ""));
  out->printer_set = out->printer_address[0] != '\0';
  out->printer_connected = p["connected"] | false;
  out->battery = p["battery"].is<int>() ? p["battery"].as<int>() : -1;
}
