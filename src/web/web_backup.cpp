#include "web/web_backup.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <nvs.h>
#include <string.h>

#include "hardware/sd_logger.h"
#include "web/web_access.h"
#include "web/web_shell.h"
// Last on purpose: T() is a macro and ArduinoJson uses T as a template
// parameter, so lang.h has to come after anything that pulls it in.
#include "lang.h"

static const char *NS = "spoolscale";
#define BACKUP_FORMAT 1

static const char *const SECRET_KEYS[] = {
  "wifi_pass", "web_pass", "filaman_key", "filaman_dev", "bb_key", "sm_secret",
};
static const char *const DEVICE_KEYS[] = {
  "fw_ver", "upd_sha", "upd_ver", "hx711",
  "rst_ok", "rst_ok_hx", "rst_recov", "rst_never", "rst_boots", "rst_due_at",
};

static bool inList(const char *key, const char *const *list, size_t n) {
  for (size_t i = 0; i < n; i++) if (!strcmp(key, list[i])) return true;
  return false;
}
static bool isSecret(const char *k) {
  return inList(k, SECRET_KEYS, sizeof(SECRET_KEYS) / sizeof(SECRET_KEYS[0]));
}
static bool isDevice(const char *k) {
  return inList(k, DEVICE_KEYS, sizeof(DEVICE_KEYS) / sizeof(DEVICE_KEYS[0]));
}

static const char *typeName(nvs_type_t t) {
  switch (t) {
    case NVS_TYPE_U8:  return "u8";
    case NVS_TYPE_I8:  return "i8";
    case NVS_TYPE_U16: return "u16";
    case NVS_TYPE_I16: return "i16";
    case NVS_TYPE_U32: return "u32";
    case NVS_TYPE_I32: return "i32";
    case NVS_TYPE_U64: return "u64";
    case NVS_TYPE_I64: return "i64";
    case NVS_TYPE_STR: return "str";
    case NVS_TYPE_BLOB: return "blob";
    default:           return nullptr;
  }
}

static void sendBackup(WebServer &srv) {
  const bool secrets = srv.arg("secrets") == "1";
  JsonDocument doc;
  doc["format"] = BACKUP_FORMAT;
  doc["secrets"] = secrets;
  JsonArray items = doc["settings"].to<JsonArray>();

  Preferences p;
  if (!p.begin(NS, true)) {
    srv.send(500, "application/json", "{\"error\":\"nvs\"}");
    return;
  }
  nvs_iterator_t it = nullptr;
  esp_err_t e = nvs_entry_find("nvs", NS, NVS_TYPE_ANY, &it);
  while (e == ESP_OK && it) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    const char *t = typeName(info.type);
    if (t && !isDevice(info.key) && (secrets || !isSecret(info.key))) {
      JsonObject o = items.add<JsonObject>();
      o["k"] = info.key;
      o["t"] = t;
      switch (info.type) {
        case NVS_TYPE_U8:  o["v"] = p.getUChar(info.key); break;
        case NVS_TYPE_I8:  o["v"] = p.getChar(info.key); break;
        case NVS_TYPE_U16: o["v"] = p.getUShort(info.key); break;
        case NVS_TYPE_I16: o["v"] = p.getShort(info.key); break;
        case NVS_TYPE_U32: o["v"] = p.getUInt(info.key); break;
        case NVS_TYPE_I32: o["v"] = p.getInt(info.key); break;
        case NVS_TYPE_U64: o["v"] = p.getULong64(info.key); break;
        case NVS_TYPE_I64: o["v"] = p.getLong64(info.key); break;
        case NVS_TYPE_STR: o["v"] = p.getString(info.key); break;
        case NVS_TYPE_BLOB: {
          // Floats are stored as four byte blobs; any blob travels as hex.
          uint8_t buf[256];
          const size_t n = p.getBytes(info.key, buf, sizeof(buf));
          String hex;
          hex.reserve(n * 2);
          static const char D[] = "0123456789abcdef";
          for (size_t i = 0; i < n; i++) { hex += D[buf[i] >> 4]; hex += D[buf[i] & 15]; }
          o["v"] = hex;
          break;
        }
        default: break;
      }
    }
    e = nvs_entry_next(&it);
  }
  if (it) nvs_release_iterator(it);
  p.end();

  String out;
  serializeJsonPretty(doc, out);
  srv.sendHeader("Content-Disposition", "attachment; filename=\"spoolmanscale-settings.json\"");
  srv.send(200, "application/json", out);
  logSDf("Web: settings backup downloaded (%u settings%s)", (unsigned)items.size(),
         secrets ? ", with secrets" : "");
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void restoreBackup(WebServer &srv) {
  JsonDocument doc;
  if (!srv.hasArg("plain") || deserializeJson(doc, srv.arg("plain")) ||
      (doc["format"] | 0) != BACKUP_FORMAT || !doc["settings"].is<JsonArray>()) {
    srv.send(400, "application/json", "{\"ok\":false,\"error\":\"not a settings file\"}");
    return;
  }
  Preferences p;
  if (!p.begin(NS, false)) {
    srv.send(500, "application/json", "{\"ok\":false,\"error\":\"nvs\"}");
    return;
  }
  int written = 0, skipped = 0;
  for (JsonObject o : doc["settings"].as<JsonArray>()) {
    const char *k = o["k"] | "";
    const char *t = o["t"] | "";
    // Keys are capped at 15 characters by NVS; anything else is not ours.
    if (!*k || strlen(k) > 15 || isDevice(k) || o["v"].isNull()) { skipped++; continue; }
    size_t ok = 0;
    if (!strcmp(t, "u8"))       ok = p.putUChar(k, o["v"].as<uint8_t>());
    else if (!strcmp(t, "i8"))  ok = p.putChar(k, o["v"].as<int8_t>());
    else if (!strcmp(t, "u16")) ok = p.putUShort(k, o["v"].as<uint16_t>());
    else if (!strcmp(t, "i16")) ok = p.putShort(k, o["v"].as<int16_t>());
    else if (!strcmp(t, "u32")) ok = p.putUInt(k, o["v"].as<uint32_t>());
    else if (!strcmp(t, "i32")) ok = p.putInt(k, o["v"].as<int32_t>());
    else if (!strcmp(t, "u64")) ok = p.putULong64(k, o["v"].as<uint64_t>());
    else if (!strcmp(t, "i64")) ok = p.putLong64(k, o["v"].as<int64_t>());
    else if (!strcmp(t, "str")) ok = p.putString(k, o["v"].as<const char *>());
    else if (!strcmp(t, "blob")) {
      const char *hex = o["v"] | "";
      const size_t n = strlen(hex) / 2;
      uint8_t buf[256];
      bool good = n <= sizeof(buf) && strlen(hex) % 2 == 0;
      for (size_t i = 0; good && i < n; i++) {
        const int hi = hexNibble(hex[2 * i]), lo = hexNibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) good = false; else buf[i] = (uint8_t)(hi << 4 | lo);
      }
      if (good) ok = p.putBytes(k, buf, n);
    }
    if (ok) written++; else skipped++;
  }
  p.end();
  logSDf("Web: settings restored (%d written, %d skipped)", written, skipped);
  String r = "{\"ok\":true,\"written\":";
  r += written;
  r += ",\"skipped\":";
  r += skipped;
  r += "}";
  srv.send(200, "application/json", r);
}

void webBackupRoutes(WebServer &srv) {
  srv.on("/api/backup", HTTP_GET, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_C_BACKUP))) return;
    sendBackup(srv);
  });
  srv.on("/api/restore", HTTP_POST, [&srv]() {
    if (!webRequire(srv, GATE_CONFIG, T(STR_W_C_BACKUP))) return;
    restoreBackup(srv);
  });
}

String webBackupCard() {
  String h;
  h.reserve(2400);
  h += F("<div class='card wide'><h2>");
  h += T(STR_W_C_BACKUP);
  h += F("</h2><p class='hint'>");
  h += T(STR_W_BACKUP_HINT);
  h += F("</p><div class='field'><label class='check'><span class='switch'>"
         "<input id='bk-sec' type='checkbox'><i></i></span>");
  h += T(STR_W_BACKUP_SECRETS);
  h += F("</label></div><div class='inrow'><button id='bk-dl'>");
  h += T(STR_W_BACKUP_SAVE);
  h += F("</button></div><div class='field' style='margin-top:18px'><label>");
  h += T(STR_W_BACKUP_LOAD);
  h += F("</label><div class='inrow'><input id='bk-f' type='file' accept='.json,application/json'>"
         "<button class='quiet' id='bk-up'>");
  h += T(STR_W_BACKUP_RESTORE);
  h += F("</button><span class='msg' id='bk-s'></span></div></div></div>");
  h += F("<script>");
  h += webShellJsStrings();
  h += F("const BK={done:");
  h += jsStr(T(STR_W_BACKUP_DONE));
  h += F(",bad:");
  h += jsStr(T(STR_W_BACKUP_BAD));
  h += F("};"
         "$('bk-dl').addEventListener('click',()=>{"
         "location.href='/api/backup'+($('bk-sec').checked?'?secrets=1':'');});"
         "$('bk-up').addEventListener('click',()=>{"
         "const f=$('bk-f').files[0];if(!f)return;"
         "f.text().then(t=>fetch('/api/restore',{method:'POST',"
         "headers:{'Content-Type':'application/json'},body:t}))"
         ".then(r=>r.json()).then(d=>{"
         "if(!d.ok){flash('bk-s',BK.bad,true,6000);return;}"
         // The values are in flash now and are read at boot: the page's own
         // restart button (webShellRestartUi) asks before it restarts.
         "flash('bk-s',BK.done.replace('%d',d.written),false,0);"
         "if(typeof doRestart==='function')doRestart();})"
         ".catch(()=>flash('bk-s',BK.bad,true,6000));});"
         "</script>");
  return h;
}
