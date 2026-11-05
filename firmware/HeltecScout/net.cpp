// Heltec Scout - Wi-Fi hotspot, thumb-drive web server, and USB serial data link
#include "scout.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <memory>
#include <vector>
#include "web_page.h"   // generated from web/device.html by tools/build_web.py

volatile uint32_t lastWebMs = 0;
IPAddress apIP(192, 168, 4, 1), staIP(0, 0, 0, 0);
int clientsOnAp = 0;
static AsyncWebServer server(80);
static DNSServer dns;

#define LOCK()   xSemaphoreTakeRecursive(storeLock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGiveRecursive(storeLock)

static bool safePath(const String &p) { return p.startsWith("/") && p.indexOf("..") < 0 && p.length() < 200; }
static void touch() { lastWebMs = millis(); }

static String jsonEsc(const char *s) {
  String o; o.reserve(strlen(s) + 4);
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 32) o += ' ';
    else o += c;
  }
  return o;
}

static bool rmTree(const String &path) {
  File f = store->open(path);
  if (!f) return false;
  if (!f.isDirectory()) { f.close(); return store->remove(path); }
  std::vector<String> kids;
  File k;
  while ((k = f.openNextFile())) { kids.push_back(String(k.path())); k.close(); }
  f.close();
  for (auto &c : kids) rmTree(c);
  return store->rmdir(path);
}

static String statusJson() {
  uint64_t t, u; storageInfo(t, u);
  char b[512];
  String j; j.reserve(4096);
  snprintf(b, sizeof b,
    "{\"board\":\"%s\",\"fw\":\"%s\",\"session\":%lu,\"dir\":\"%s\",\"store\":\"%s\",\"total\":%llu,\"used\":%llu,"
    "\"uptime\":%lu,\"utc\":%lu,\"timeSrc\":\"%s\",\"batt\":%.2f,\"ap\":\"%s\",\"clients\":%d,\"sta\":\"%s\",",
    BOARD_NAME, FW_VERSION, (unsigned long)sessionNumber(), sessionDir, !storeOk ? "none" : storeIsSD ? "microSD" : "internal",
    (unsigned long long)t, (unsigned long long)u, (unsigned long)(millis() / 1000), (unsigned long)utcNow(), timeSource(),
    batteryVolts(), jsonEsc(cfg.apName).c_str(), clientsOnAp, staIP.toString().c_str());
  j += b;
  snprintf(b, sizeof b, "\"loc\":{\"ok\":%s,\"lat\":%.6f,\"lon\":%.6f,\"src\":\"%s\",\"sats\":%u,\"hdop\":%.1f,\"gpsSeen\":%s},",
    gLoc.ok ? "true" : "false", gLoc.lat, gLoc.lon, gLoc.src, gLoc.sats, gLoc.hdop, gpsSeen ? "true" : "false");
  j += b;
  snprintf(b, sizeof b, "\"counts\":{\"wifi\":%lu,\"ble\":%lu,\"lora\":%lu,\"rf\":%lu,\"gps\":%lu,\"wifiNow\":%d,\"wifiUnique\":%d,\"bleNow\":%d,\"bleUnique\":%d,"
    "\"loraPkts\":%lu,\"loraNodes\":%lu,\"loraBad\":%lu,\"rfHits\":%lu,\"sweeps\":%lu},",
    (unsigned long)logLines(LOG_WIFI), (unsigned long)logLines(LOG_BLE), (unsigned long)logLines(LOG_LORA), (unsigned long)logLines(LOG_RF),
    (unsigned long)logLines(LOG_GPS), wifiTotalSeen, wifiUniqueBssids, bleWindowCount, bleUnique,
    (unsigned long)loraPackets, (unsigned long)loraNodes, (unsigned long)loraCrcErr, (unsigned long)rfHits, (unsigned long)sweepCount);
  j += b;
  j += "\"wifi\":[";
  char mac[18];
  for (int i = 0; i < wifiTopCount; i++) {
    macStr(wifiTop[i].bssid, mac);
    snprintf(b, sizeof b, "%s{\"ssid\":\"%s\",\"bssid\":\"%s\",\"rssi\":%d,\"ch\":%u,\"open\":%s}", i ? "," : "",
             jsonEsc(wifiTop[i].ssid).c_str(), mac, wifiTop[i].rssi, wifiTop[i].ch, wifiTop[i].enc == 0 ? "true" : "false");
    j += b;
  }
  j += "],\"ble\":[";
  for (int i = 0; i < bleTopCount; i++) {
    macStr(bleTop[i].addr, mac);
    snprintf(b, sizeof b, "%s{\"addr\":\"%s\",\"name\":\"%s\",\"kind\":\"%s\",\"rssi\":%d,\"hits\":%u}", i ? "," : "",
             mac, jsonEsc(bleTop[i].name).c_str(), bleTop[i].kind, bleTop[i].rssi, bleTop[i].hits);
    j += b;
  }
  j += "],\"lora\":[";
  for (int i = 0; i < loraLastCount; i++) {
    LoraPkt &p = loraLast[i];
    snprintf(b, sizeof b, "%s{\"src\":\"%08lx\",\"dst\":\"%08lx\",\"rssi\":%.1f,\"snr\":%.1f,\"len\":%u,\"ago\":%lu}", i ? "," : "",
             (unsigned long)p.src, (unsigned long)p.dst, p.rssi, p.snr, p.len, (unsigned long)((millis() - p.atMs) / 1000));
    j += b;
  }
  snprintf(b, sizeof b, "],\"rf\":{\"start\":%.3f,\"step\":%.3f,\"floor\":%.1f,\"busiest\":%.3f,\"now\":[", cfg.sweepStart, cfg.sweepStep, sweepFloor, rfBusiestMHz);
  j += b;
  for (int i = 0; i < sweepBins; i++) { if (i) j += ','; j += String((int)lroundf(sweepRssi[i])); }
  j += "],\"peak\":[";
  for (int i = 0; i < sweepBins; i++) { if (i) j += ','; j += String((int)lroundf(sweepPeak[i])); }
  j += "]},";
  snprintf(b, sizeof b,
    "\"cfg\":{\"apName\":\"%s\",\"staSsid\":\"%s\",\"scrollSec\":%u,\"sleepMin\":%u,\"loraFreq\":%.3f,\"loraBw\":%.1f,\"loraSf\":%u,"
    "\"loraCr\":%u,\"loraSync\":%u,\"sweepStart\":%.3f,\"sweepStop\":%.3f,\"sweepStep\":%.3f,\"sweepThresh\":%u,\"wifiEvery\":%u,"
    "\"gpsOn\":%s,\"gpsRx\":%d,\"gpsTx\":%d,\"gpsBaud\":%lu,\"fixedLat\":%.6f,\"fixedLon\":%.6f}}",
    jsonEsc(cfg.apName).c_str(), jsonEsc(cfg.staSsid).c_str(), cfg.scrollSec, cfg.sleepMin, cfg.loraFreq, cfg.loraBw, cfg.loraSf,
    cfg.loraCr, cfg.loraSync, cfg.sweepStart, cfg.sweepStop, cfg.sweepStep, cfg.sweepThresh, cfg.wifiEverySec,
    cfg.gpsOn ? "true" : "false", cfg.gpsRx, cfg.gpsTx, (unsigned long)cfg.gpsBaud, cfg.fixedLat, cfg.fixedLon);
  j += b;
  return j;
}

// streams one or more files, optionally each preceded by a "##FILE name size" line (session bundle)
struct Streamer {
  std::vector<String> paths; size_t idx = 0; File f; String pending; bool headers = false;
};
static size_t streamFill(std::shared_ptr<Streamer> s, uint8_t *buf, size_t max) {
  touch();
  size_t out = 0;
  while (out < max) {
    if (s->pending.length()) {
      size_t n = min(max - out, (size_t)s->pending.length());
      memcpy(buf + out, s->pending.c_str(), n); out += n; s->pending.remove(0, n); continue;
    }
    if (!s->f) {
      if (s->idx >= s->paths.size()) break;
      LOCK(); s->f = store->open(s->paths[s->idx++]); UNLOCK();
      if (!s->f) continue;
      if (s->headers) { String nm = s->f.name(); s->pending = "##FILE " + nm + " " + String((unsigned long)s->f.size()) + "\n"; }
      continue;
    }
    LOCK(); int n = s->f.read(buf + out, max - out); UNLOCK();
    if (n <= 0) { s->f.close(); s->f = File(); if (s->headers) s->pending = "\n"; continue; }
    out += n;
  }
  return out;
}

static const char *mimeFor(const String &p) {
  String l = p; l.toLowerCase();
  if (l.endsWith(".csv")) return "text/csv";
  if (l.endsWith(".txt") || l.endsWith(".log")) return "text/plain";
  if (l.endsWith(".html") || l.endsWith(".htm")) return "text/html";
  if (l.endsWith(".jpg") || l.endsWith(".jpeg")) return "image/jpeg";
  if (l.endsWith(".png")) return "image/png";
  if (l.endsWith(".gif")) return "image/gif";
  if (l.endsWith(".mp3")) return "audio/mpeg";
  if (l.endsWith(".mp4")) return "video/mp4";
  if (l.endsWith(".pdf")) return "application/pdf";
  if (l.endsWith(".json")) return "application/json";
  return "application/octet-stream";
}

static void routes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    touch();
    AsyncWebServerResponse *res = r->beginResponse(200, "text/html", WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
    res->addHeader("Content-Encoding", "gzip");
    r->send(res);
  });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *r) { touch(); r->send(200, "application/json", statusJson()); });
  server.on("/api/time", HTTP_POST, [](AsyncWebServerRequest *r) {
    touch();
    if (r->hasParam("t", true)) timeSet(strtoul(r->getParam("t", true)->value().c_str(), nullptr, 10), "phone");
    r->send(200, "text/plain", "ok");
  });
  server.on("/api/ls", HTTP_GET, [](AsyncWebServerRequest *r) {
    touch();
    if (!storeOk) { r->send(503, "text/plain", "no storage"); return; }
    String dir = r->hasParam("dir") ? r->getParam("dir")->value() : "/";
    if (!safePath(dir)) { r->send(400); return; }
    String j = "[";
    LOCK();
    File d = store->open(dir);
    if (d && d.isDirectory()) {
      File f; bool first = true;
      while ((f = d.openNextFile())) {
        j += first ? "" : ","; first = false;
        j += "{\"n\":\""; j += jsonEsc(f.name()); j += "\",\"s\":"; j += String((unsigned long)f.size());
        j += ",\"d\":"; j += f.isDirectory() ? "true" : "false"; j += "}";
        f.close();
        if (j.length() > 60000) break;
      }
    }
    if (d) d.close();
    UNLOCK();
    j += "]";
    r->send(200, "application/json", j);
  });
  server.on("/api/dl", HTTP_GET, [](AsyncWebServerRequest *r) {
    touch();
    if (!storeOk || !r->hasParam("path")) { r->send(400); return; }
    String p = r->getParam("path")->value();
    if (!safePath(p)) { r->send(400); return; }
    LOCK(); File f = store->open(p); UNLOCK();
    if (!f || f.isDirectory()) { r->send(404); return; }
    size_t len = f.size(); f.close();
    auto s = std::make_shared<Streamer>(); s->paths.push_back(p);
    AsyncWebServerResponse *res = r->beginResponse(mimeFor(p), len, [s](uint8_t *b, size_t m, size_t) { return streamFill(s, b, m); });
    if (r->hasParam("save")) { String nm = p.substring(p.lastIndexOf('/') + 1); res->addHeader("Content-Disposition", "attachment; filename=\"" + nm + "\""); }
    r->send(res);
  });
  // a whole logging session as one text file the analyzer understands
  server.on("/api/bundle", HTTP_GET, [](AsyncWebServerRequest *r) {
    touch();
    if (!storeOk) { r->send(503); return; }
    String sess = r->hasParam("s") ? r->getParam("s")->value() : String(sessionDir).substring(7);
    String dir = "/scout/" + sess;
    if (!safePath(dir)) { r->send(400); return; }
    auto s = std::make_shared<Streamer>(); s->headers = true;
    LOCK();
    logFlush(true);
    File d = store->open(dir); File f;
    if (d && d.isDirectory()) while ((f = d.openNextFile())) { if (!f.isDirectory()) s->paths.push_back(String(f.path())); f.close(); }
    if (d) d.close();
    UNLOCK();
    if (s->paths.empty()) { r->send(404, "text/plain", "no such session"); return; }
    s->pending = "##SCOUT 1 " + sess + "\n";
    AsyncWebServerResponse *res = r->beginChunkedResponse("text/plain", [s](uint8_t *b, size_t m, size_t) { return streamFill(s, b, m); });
    res->addHeader("Content-Disposition", "attachment; filename=\"scout-" + sess + ".scout\"");
    r->send(res);
  });
  server.on("/api/rm", HTTP_POST, [](AsyncWebServerRequest *r) {
    touch();
    String p = r->hasParam("path", true) ? r->getParam("path", true)->value() : "";
    if (!storeOk || !safePath(p) || p == "/" || p == sessionDir) { r->send(400, "text/plain", "can't delete that"); return; }
    LOCK(); bool ok = rmTree(p); UNLOCK();
    r->send(ok ? 200 : 500, "text/plain", ok ? "ok" : "failed");
  });
  server.on("/api/mkdir", HTTP_POST, [](AsyncWebServerRequest *r) {
    touch();
    String p = r->hasParam("path", true) ? r->getParam("path", true)->value() : "";
    if (!storeOk || !safePath(p)) { r->send(400); return; }
    LOCK(); bool ok = store->mkdir(p); UNLOCK();
    r->send(ok ? 200 : 500);
  });
  server.on("/api/mv", HTTP_POST, [](AsyncWebServerRequest *r) {
    touch();
    String a = r->hasParam("from", true) ? r->getParam("from", true)->value() : "";
    String b = r->hasParam("to", true) ? r->getParam("to", true)->value() : "";
    if (!storeOk || !safePath(a) || !safePath(b)) { r->send(400); return; }
    LOCK(); bool ok = store->rename(a, b); UNLOCK();
    r->send(ok ? 200 : 500);
  });
  server.on("/api/up", HTTP_POST,
    [](AsyncWebServerRequest *r) { touch(); r->send(200, "text/plain", "ok"); },
    [](AsyncWebServerRequest *r, const String &fn, size_t index, uint8_t *data, size_t len, bool final) {
      touch();
      if (!storeOk) return;
      if (index == 0) {
        String dir = r->hasParam("dir") ? r->getParam("dir")->value() : "/";
        if (!safePath(dir)) return;
        String name = fn; name.replace("/", "_"); name.replace("\\", "_");
        String p = dir.endsWith("/") ? dir + name : dir + "/" + name;
        LOCK(); r->_tempFile = store->open(p, FILE_WRITE); UNLOCK();
      }
      if (r->_tempFile) { LOCK(); r->_tempFile.write(data, len); UNLOCK(); }
      if (final && r->_tempFile) { LOCK(); r->_tempFile.close(); UNLOCK(); }
    });
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *r) {
    touch();
    auto has = [&](const char *k) { return r->hasParam(k, true); };
    auto val = [&](const char *k) { return r->getParam(k, true)->value(); };
    bool apChanged = false;
    if (has("apName") && val("apName").length()) { apChanged |= val("apName") != cfg.apName; strlcpy(cfg.apName, val("apName").c_str(), sizeof cfg.apName); }
    if (has("apPass") && val("apPass").length() >= 8) { apChanged = true; strlcpy(cfg.apPass, val("apPass").c_str(), sizeof cfg.apPass); }
    if (has("staSsid")) { apChanged |= val("staSsid") != cfg.staSsid; strlcpy(cfg.staSsid, val("staSsid").c_str(), sizeof cfg.staSsid); }
    if (has("staPass") && val("staPass").length()) { apChanged = true; strlcpy(cfg.staPass, val("staPass").c_str(), sizeof cfg.staPass); }
    if (has("scrollSec")) cfg.scrollSec = val("scrollSec").toInt();
    if (has("sleepMin")) cfg.sleepMin = val("sleepMin").toInt();
    if (has("loraFreq")) cfg.loraFreq = constrain(val("loraFreq").toFloat(), 860.0f, 930.0f);
    if (has("loraBw")) cfg.loraBw = val("loraBw").toFloat();
    if (has("loraSf")) cfg.loraSf = constrain(val("loraSf").toInt(), 6, 12);
    if (has("loraCr")) cfg.loraCr = constrain(val("loraCr").toInt(), 5, 8);
    if (has("loraSync")) cfg.loraSync = val("loraSync").toInt();
    if (has("sweepStart")) cfg.sweepStart = val("sweepStart").toFloat();
    if (has("sweepStop")) cfg.sweepStop = val("sweepStop").toFloat();
    if (has("sweepStep")) cfg.sweepStep = max(0.05f, val("sweepStep").toFloat());
    if (cfg.sweepStop <= cfg.sweepStart) cfg.sweepStop = cfg.sweepStart + 1;
    if (has("sweepThresh")) cfg.sweepThresh = constrain(val("sweepThresh").toInt(), 3, 40);
    if (has("wifiEvery")) cfg.wifiEverySec = max(5L, val("wifiEvery").toInt());
    if (has("gpsOn")) cfg.gpsOn = val("gpsOn") == "1";
    if (has("gpsRx")) cfg.gpsRx = val("gpsRx").toInt();
    if (has("gpsTx")) cfg.gpsTx = val("gpsTx").toInt();
    if (has("gpsBaud")) cfg.gpsBaud = val("gpsBaud").toInt();
    if (has("fixedLat")) cfg.fixedLat = val("fixedLat").toDouble();
    if (has("fixedLon")) cfg.fixedLon = val("fixedLon").toDouble();
    settingsSave();
    logEvent("settings", "changed from web page");
    radioApplySettings();
    r->send(200, "text/plain", apChanged ? "restart" : "ok");
    if (apChanged) { logFlush(true); delay(300); ESP.restart(); }
  });
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *r) {
    r->send(200, "text/plain", "ok"); logEvent("reboot", "web"); logFlush(true); delay(300); ESP.restart();
  });
  server.onNotFound([](AsyncWebServerRequest *r) {          // captive portal: send everything to the main page
    touch();
    if (r->url().startsWith("/api/")) { r->send(404); return; }
    r->redirect("http://192.168.4.1/");
  });
}

static uint32_t lastSta = 0;
void netBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(cfg.apName, cfg.apPass, 6, 0, 4);
  if (cfg.staSsid[0]) WiFi.begin(cfg.staSsid, cfg.staPass);
  dns.start(53, "*", apIP);
  MDNS.begin("scout");
  routes();
  server.begin();
  MDNS.addService("http", "tcp", 80);
}

void netLoop() {
  dns.processNextRequest();
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    clientsOnAp = WiFi.softAPgetStationNum();
    staIP = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
    if (cfg.staSsid[0] && WiFi.status() != WL_CONNECTED && millis() - lastSta > 60000) { lastSta = millis(); WiFi.begin(cfg.staSsid, cfg.staPass); }
  }
}

// ======================= USB serial link (used by the Scout Studio app) =======================
// Lines starting with '?' are commands. Replies start with '@'. Anything else from the board starts with '#'.
//   ?INFO            -> @INFO {json}
//   ?LS /scout       -> @F <size> <path> / @D <path> ... then @END
//   ?GET <path>      -> @DATA <size>\n<raw bytes>\n@END
//   ?TIME <epoch>    -> @OK
//   ?RM <path>       -> @OK / @ERR
static String cmdBuf[2];
static Stream *ports[2] = {&Serial, nullptr};
static Stream *xferPort = nullptr;
static File xferFile;
static uint32_t xferLeft = 0;

bool serialBusy() { return xferPort != nullptr; }

static void reply(Stream *s, const String &cmd) {
  String c = cmd; c.trim();
  if (c.startsWith("?INFO")) { s->print("@INFO "); s->println(statusJson()); return; }
  if (c.startsWith("?TIME ")) { timeSet(strtoul(c.c_str() + 6, nullptr, 10), "usb"); s->println("@OK"); return; }
  if (!storeOk) { s->println("@ERR no storage"); return; }
  if (c.startsWith("?LS")) {
    String dir = c.length() > 4 ? c.substring(4) : "/scout";
    if (!safePath(dir)) { s->println("@ERR path"); return; }
    logFlush(true);
    LOCK();
    File d = store->open(dir), f;
    if (d && d.isDirectory()) while ((f = d.openNextFile())) {
      if (f.isDirectory()) { s->print("@D "); s->println(f.path()); }
      else { s->printf("@F %lu %s\n", (unsigned long)f.size(), f.path()); }
      f.close();
    }
    if (d) d.close();
    UNLOCK();
    s->println("@END");
    return;
  }
  if (c.startsWith("?GET ")) {
    String p = c.substring(5);
    if (!safePath(p)) { s->println("@ERR path"); return; }
    logFlush(true);
    LOCK(); xferFile = store->open(p); UNLOCK();
    if (!xferFile || xferFile.isDirectory()) { s->println("@ERR not found"); return; }
    xferLeft = xferFile.size();
    s->printf("@DATA %lu\n", (unsigned long)xferLeft);
    xferPort = s;
    return;
  }
  if (c.startsWith("?RM ")) {
    String p = c.substring(4);
    if (!safePath(p) || p == sessionDir || p == "/") { s->println("@ERR path"); return; }
    LOCK(); bool ok = rmTree(p); UNLOCK();
    s->println(ok ? "@OK" : "@ERR failed");
    return;
  }
  s->println("@ERR unknown");
}

void serialLoop() {
#if ARDUINO_USB_CDC_ON_BOOT
  ports[1] = &Serial0;     // V4: also answer on the UART pins
#endif
  if (xferPort) {           // send a file a chunk at a time so the radios keep running
    uint8_t buf[1024];
    for (int k = 0; k < 8 && xferLeft; k++) {
      LOCK(); int n = xferFile.read(buf, min((uint32_t)sizeof buf, xferLeft)); UNLOCK();
      if (n <= 0) { xferLeft = 0; break; }
      xferPort->write(buf, n); xferLeft -= n;
    }
    if (!xferLeft) { LOCK(); xferFile.close(); UNLOCK(); xferPort->print("\n@END\n"); xferPort = nullptr; }
    return;
  }
  for (int i = 0; i < 2; i++) {
    Stream *s = ports[i]; if (!s) continue;
    while (s->available()) {
      char ch = s->read();
      if (ch == '\n' || ch == '\r') {
        if (cmdBuf[i].startsWith("?")) reply(s, cmdBuf[i]);
        cmdBuf[i] = "";
        if (xferPort) return;
      } else if (cmdBuf[i].length() < 240) cmdBuf[i] += ch;
    }
  }
}
