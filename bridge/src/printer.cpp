#include "printer.h"

#include "bridge.h"

// BluetoothSerial is marked [[deprecated]] in Arduino core 3.x ("won't be
// supported in version 4.0.0 by default"). It is still the only Arduino API
// for an SPP master and works on 3.3.12, which is what this bridge is for.
// The warning is silenced for this file only, the one place that uses it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <BluetoothSerial.h>

// How long the link stays up after a job. Long enough for a batch of labels
// sent one by one, short enough that the printer can go back to sleep.
#define LINK_IDLE_MS    60000
// From the end of the connect to the last byte out, per job.
#define JOB_TIMEOUT_MS  30000
// Bytes per write() call. BluetoothSerial copies every call into a heap
// packet and queues up to 32 of them: 512 keeps that under 16 kB of a heap
// that has no PSRAM behind it, and still fills its 330 byte SPP frames.
#define WRITE_CHUNK     512
// Inquiry length for /api/scan, a multiple of the 1.28 s inquiry unit.
#define SCAN_MS         (8 * 1280)
// The name other devices see when they scan for us.
#define BT_DEVICE_NAME  "Druckbruecke"
// What the vendor app asks before a job (HCI snoop, 16.11.2026).
static const char BATTERY_QUERY[] = "BATTERY?\r\n";
static const char BATTERY_REPLY[] = "BATTERY ";

static BluetoothSerial bt;
static SemaphoreHandle_t s_lock = nullptr;

static volatile bool     s_busy     = false;
static volatile bool     s_scanning = false;
static volatile uint32_t s_lastUse  = 0;
static volatile int      s_battery  = -1;   // percent, -1 unknown
static bool              s_wasUp    = false;

// The RFCOMM channel found by SDP for s_channelAddr. SDP costs a second or
// two per connect, and the printer's channel does not move.
static int    s_channel = 0;
static String s_channelAddr;
// Whether the last connect that worked asked for no security. Tried first
// next time, so a printer that only works one way pays for the other once.
static bool   s_secNoneFirst = false;

// The running job. Only the task holding s_lock touches these.
static uint32_t s_jobStart  = 0;
static uint32_t s_deadline  = 0;
static size_t   s_jobBytes  = 0;
static bool     s_jobFailed = false;

// Read by the loop task for /api/status while another task may write it.
struct LastJob {
  bool     ok;
  size_t   bytes;
  uint32_t ms;
  uint32_t at;    // uptime in s
  char     error[24];
};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static LastJob  s_last;
static bool     s_haveLast = false;
static uint32_t s_jobs     = 0;

// ---------------------------------------------------------------- receive

// Runs on the Bluetooth task for every packet the printer sends. Nothing it
// says is needed for printing; the battery is picked out and the rest logged
// in hex, so the printer's other answers can be read off the serial log.
// The reply format is only half known ("BATTERY " then binary), so the byte
// after the word counts only when it is a plausible percentage.
static uint8_t s_batMatch = 0;

static void onBtData(const uint8_t* data, size_t len) {
  char hex[3 * 24 + 1];
  size_t h = 0;
  for (size_t i = 0; i < len && i < 24; i++) h += snprintf(hex + h, sizeof(hex) - h, "%02X ", data[i]);
  hex[h] = '\0';
  logLine("BT: %u bytes from printer: %s%s", (unsigned)len, hex, len > 24 ? "..." : "");

  const size_t word = sizeof(BATTERY_REPLY) - 1;
  for (size_t i = 0; i < len; i++) {
    const uint8_t b = data[i];
    if (s_batMatch == word) {
      s_batMatch = 0;
      if (b <= 100) {
        s_battery = b;
        logLine("BT: battery %u %%", (unsigned)b);
      } else {
        s_battery = -1;
        logLine("BT: battery reply not understood (0x%02X)", b);
      }
      continue;
    }
    if (b == (uint8_t)BATTERY_REPLY[s_batMatch]) {
      s_batMatch++;
    } else {
      s_batMatch = (b == (uint8_t)BATTERY_REPLY[0]) ? 1 : 0;
    }
  }
}

// ---------------------------------------------------------------- link

static bool linkUp() {
  return bt.connected(0);
}

static esp_spp_sec_t secMask(bool none) {
  return none ? (esp_spp_sec_t)ESP_SPP_SEC_NONE
              : (esp_spp_sec_t)(ESP_SPP_SEC_ENCRYPT | ESP_SPP_SEC_AUTHENTICATE);
}

// Finds the SPP channel by SDP. The PM240 names its service "JL_SPP"; a
// service with "SPP" in its name wins, else the first one listed.
static int findChannel(const BTAddress& addr) {
  std::map<int, std::string> channels = bt.getChannels(addr);
  int pick = 0;
  for (const auto& c : channels) {
    logLine("BT: SDP channel %d \"%s\"", c.first, c.second.c_str());
    const bool spp = c.second.find("SPP") != std::string::npos;
    if (pick == 0 || spp) pick = c.first;
    if (spp) break;
  }
  return pick;
}

// Called with s_lock held.
static bool connectPrinter() {
  if (linkUp()) return true;
  const BTAddress addr(cfg.printerAddr);
  bt.setPin(cfg.printerPin.c_str(), (uint8_t)cfg.printerPin.length());

  if (s_channelAddr != cfg.printerAddr) s_channel = 0;
  if (s_channel == 0) {
    const uint32_t t0 = millis();
    s_channel = findChannel(addr);
    if (s_channel == 0) {
      logLine("BT: no SPP service found on %s (%lu ms) - printer off or out of range?",
              cfg.printerAddr.c_str(), (unsigned long)(millis() - t0));
      return false;
    }
    s_channelAddr = cfg.printerAddr;
  }

  // Security as Android does it first (authenticated, encrypted, PIN for
  // legacy pairing); then without, for a printer that refuses to pair.
  for (int attempt = 0; attempt < 2; attempt++) {
    const bool none = (attempt == 0) ? s_secNoneFirst : !s_secNoneFirst;
    const uint32_t t0 = millis();
    logLine("BT: connecting to %s channel %d (%s)", cfg.printerAddr.c_str(), s_channel,
            none ? "no security" : "authenticated");
    if (bt.connect(addr, s_channel, secMask(none))) {
      s_secNoneFirst = none;
      s_wasUp = true;
      s_lastUse = millis();
      logLine("BT: connected in %lu ms", (unsigned long)(millis() - t0));
      // Asked, not awaited: the answer comes in on the Bluetooth task while
      // the job is already on its way.
      bt.write((const uint8_t*)BATTERY_QUERY, sizeof(BATTERY_QUERY) - 1);
      return true;
    }
    logLine("BT: connect failed after %lu ms", (unsigned long)(millis() - t0));
  }
  // The channel may be stale (another printer at the address, firmware
  // update); SDP again next time.
  s_channel = 0;
  return false;
}

static bool isBonded(const String& address) {
  if (!btAddressValid(address)) return false;
  int n = bt.getNumberOfBondedDevices();
  if (n <= 0) return false;
  if (n > 16) n = 16;
  esp_bd_addr_t list[16];
  n = bt.getBondedDevices((uint)n, list);
  const BTAddress want(address);
  for (int i = 0; i < n; i++) {
    if (BTAddress(list[i]).equals(want)) return true;
  }
  return false;
}

// ---------------------------------------------------------------- public

void printerBegin() {
  s_lock = xSemaphoreCreateMutex();
  // No display, no keyboard: Secure Simple Pairing then runs as "just works".
  // A printer that still asks for a numeric comparison gets a yes; one that
  // uses legacy pairing gets the PIN set before each connect.
  bt.enableSSP(false, false);
  bt.onConfirmRequest([](uint32_t num) {
    logLine("BT: pairing confirmation %06lu accepted", (unsigned long)num);
    bt.confirmReply(true);
  });
  bt.onAuthComplete([](boolean ok) { logLine("BT: pairing %s", ok ? "done" : "failed"); });
  bt.onData(onBtData);
  // Master, and BLE released: the bridge does not use it, and its controller
  // memory is worth more to WiFi and the web server on a board without PSRAM.
  if (!bt.begin(BT_DEVICE_NAME, true, true)) {
    logLine("BT: start failed");
    return;
  }
  bt.setPin(cfg.printerPin.c_str(), (uint8_t)cfg.printerPin.length());
  logLine("BT: ready as \"%s\" %s", BT_DEVICE_NAME, bt.getBtAddressString().c_str());
}

void printerLoop() {
  const bool up = linkUp();
  if (s_wasUp && !up && !s_busy) {
    logLine("BT: link closed");
    s_wasUp = false;
  }
  if (!up || s_busy || millis() - s_lastUse < LINK_IDLE_MS) return;
  if (xSemaphoreTake(s_lock, 0) != pdTRUE) return;
  if (linkUp()) {
    logLine("BT: idle for %d s, disconnecting", LINK_IDLE_MS / 1000);
    bt.disconnect();
    s_wasUp = false;
  }
  xSemaphoreGive(s_lock);
}

static void recordLast(JobStatus st, size_t bytes, uint32_t ms) {
  portENTER_CRITICAL(&s_mux);
  s_last.ok    = st == JOB_OK;
  s_last.bytes = bytes;
  s_last.ms    = ms;
  s_last.at    = millis() / 1000;
  strlcpy(s_last.error, st == JOB_OK ? "" : jobErrorText(st), sizeof(s_last.error));
  s_haveLast = true;
  if (st == JOB_OK) s_jobs++;
  portEXIT_CRITICAL(&s_mux);
}

JobStatus jobBegin() {
  if (!s_lock) return JOB_CONNECT_FAILED;
  if (xSemaphoreTake(s_lock, 0) != pdTRUE) return JOB_BUSY;
  // Read under the lock: the printer settings change only while it is held.
  if (cfg.printerAddr.isEmpty()) {
    xSemaphoreGive(s_lock);
    return JOB_NO_PRINTER;
  }
  s_busy      = true;
  s_jobStart  = millis();
  s_jobBytes  = 0;
  s_jobFailed = false;
  if (!connectPrinter()) {
    const uint32_t ms = millis() - s_jobStart;
    recordLast(JOB_CONNECT_FAILED, 0, ms);
    logLine("Job: connect failed (%lu ms)", (unsigned long)ms);
    s_busy = false;
    xSemaphoreGive(s_lock);
    return JOB_CONNECT_FAILED;
  }
  s_deadline = millis() + JOB_TIMEOUT_MS;
  return JOB_OK;
}

bool jobWrite(const uint8_t* data, size_t len) {
  if (s_jobFailed) return false;
  while (len > 0) {
    const size_t n = len < WRITE_CHUNK ? len : WRITE_CHUNK;
    // All or nothing: 0 means the queue stayed full for a second or the link
    // is gone. The same chunk is offered again until the deadline.
    if (bt.write(data, n) == n) {
      data += n;
      len -= n;
      s_jobBytes += n;
    } else if (!linkUp()) {
      logLine("Job: link lost after %u bytes", (unsigned)s_jobBytes);
      s_jobFailed = true;
      return false;
    } else {
      delay(10);
    }
    if ((int32_t)(millis() - s_deadline) >= 0) {
      logLine("Job: timeout after %u bytes", (unsigned)s_jobBytes);
      s_jobFailed = true;
      return false;
    }
  }
  return true;
}

JobResult jobEnd(bool aborted) {
  JobStatus st = aborted ? JOB_ABORTED : (s_jobFailed ? JOB_WRITE_FAILED : JOB_OK);
  if (st == JOB_OK) {
    // Waits until BluetoothSerial's queue is empty, i.e. everything was
    // handed to the stack. A link that dropped meanwhile means the tail was
    // thrown away.
    bt.flush();
    if (!linkUp()) st = JOB_WRITE_FAILED;
  } else if (linkUp()) {
    // Half a job leaves the printer in the middle of a BITMAP. A fresh link
    // makes sure the next job starts from a clean parser.
    bt.disconnect();
    s_wasUp = false;
  }
  const JobResult r = { st, s_jobBytes, millis() - s_jobStart };
  recordLast(st, r.bytes, r.ms);
  logLine("Job: %s, %u bytes in %lu ms", st == JOB_OK ? "ok" : jobErrorText(st), (unsigned)r.bytes,
          (unsigned long)r.ms);
  s_lastUse = millis();
  s_busy = false;
  xSemaphoreGive(s_lock);
  return r;
}

JobResult printBuffer(const uint8_t* data, size_t len) {
  const JobStatus st = jobBegin();
  if (st != JOB_OK) return { st, 0, 0 };
  jobWrite(data, len);
  return jobEnd(false);
}

String buildTestLabel(const String& ip, const String& host) {
  // 50 x 30 mm at 203 dpi: 400 x 240 dots, of which the 384 dot head sees
  // the middle. Built-in fonts only, so no bitmap is needed; they are plain
  // ASCII, which is why the label says "Druckbruecke" without the umlaut.
  // Font "4" is 24 x 32 dots: 15 characters fill 360 of the 384.
  String j;
  j.reserve(400);
  j += "SIZE 50 mm,30 mm\r\n";
  j += "GAP 6 mm,0 mm\r\n";
  j += "DIRECTION 0,0\r\n";
  j += "DENSITY 10\r\n";
  j += "CLS\r\n";
  j += "TEXT 16,16,\"4\",0,1,1,\"Druckbruecke OK\"\r\n";
  j += "TEXT 16,72,\"3\",0,1,1,\"IP " + ip + "\"\r\n";
  j += "TEXT 16,108,\"3\",0,1,1,\"" + host + ".local\"\r\n";
  j += "TEXT 16,150,\"2\",0,1,1,\"Firmware " BRIDGE_VERSION "\"\r\n";
  j += "TEXT 16,180,\"2\",0,1,1,\"Drucker " + cfg.printerAddr + "\"\r\n";
  j += "PRINT 1\r\n";
  return j;
}

int jobHttpStatus(JobStatus s) {
  switch (s) {
    case JOB_OK:             return 200;
    case JOB_BUSY:           return 409;
    case JOB_NO_PRINTER:     return 412;
    case JOB_CONNECT_FAILED: return 502;
    case JOB_WRITE_FAILED:   return 502;
    case JOB_ABORTED:        return 400;
  }
  return 500;
}

const char* jobErrorText(JobStatus s) {
  switch (s) {
    case JOB_OK:             return "";
    case JOB_BUSY:           return "busy";
    case JOB_NO_PRINTER:     return "no printer";
    case JOB_CONNECT_FAILED: return "connect failed";
    case JOB_WRITE_FAILED:   return "write failed";
    case JOB_ABORTED:        return "aborted";
  }
  return "error";
}

bool printerScan(JsonArray devices) {
  if (!s_lock || xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
  s_scanning = true;
  // An inquiry with the link open would starve it; discover() closes it
  // anyway, this only makes it visible in the log.
  if (linkUp()) {
    logLine("BT: closing the link for a scan");
    bt.disconnect();
    s_wasUp = false;
  }
  logLine("BT: scanning %d ms", SCAN_MS);
  BTScanResults* res = bt.discover(SCAN_MS);
  const int n = res ? res->getCount() : 0;
  for (int i = 0; i < n; i++) {
    BTAdvertisedDevice* d = res->getDevice(i);
    if (!d) continue;
    JsonObject o = devices.add<JsonObject>();
    o["address"] = d->getAddress().toString(true);
    o["name"] = d->haveName() ? String(d->getName().c_str()) : String("");
    if (d->haveRSSI()) o["rssi"] = d->getRSSI();
    else               o["rssi"] = nullptr;
  }
  logLine("BT: scan found %d device(s)", n);
  s_scanning = false;
  xSemaphoreGive(s_lock);
  return true;
}

bool printerSetConfig(const String& addr, const String& name, const String& pin) {
  if (!s_lock || xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
  const String old = cfg.printerAddr;
  configSavePrinter(addr, name, pin);
  if (linkUp()) {
    bt.disconnect();
    s_wasUp = false;
  }
  // Forgetting the printer also drops its pairing, so choosing it again
  // pairs afresh with whatever PIN is entered then.
  if (addr.isEmpty() && btAddressValid(old)) {
    const BTAddress oldAddr(old);
    bt.deleteBondedDevice(*oldAddr.getNative());
    logLine("BT: pairing with %s removed", old.c_str());
  }
  s_channel = 0;
  s_battery = -1;
  xSemaphoreGive(s_lock);
  return true;
}

void printerStatusJson(JsonObject printer) {
  printer["address"]   = cfg.printerAddr;
  printer["name"]      = cfg.printerName;
  printer["paired"]    = isBonded(cfg.printerAddr);
  printer["connected"] = linkUp();
  const int bat = s_battery;
  if (bat >= 0) printer["battery"] = bat;
  else          printer["battery"] = nullptr;
}

bool printerBusy() {
  return s_busy || s_scanning;
}

uint32_t printerJobCount() {
  portENTER_CRITICAL(&s_mux);
  const uint32_t n = s_jobs;
  portEXIT_CRITICAL(&s_mux);
  return n;
}

bool printerLastJobJson(JsonObject last) {
  LastJob l;
  bool have;
  portENTER_CRITICAL(&s_mux);
  l = s_last;
  have = s_haveLast;
  portEXIT_CRITICAL(&s_mux);
  if (!have) return false;
  last["ok"]    = l.ok;
  last["bytes"] = l.bytes;
  last["ms"]    = l.ms;
  last["error"] = l.error;
  last["at"]    = l.at;
  return true;
}

#pragma GCC diagnostic pop
