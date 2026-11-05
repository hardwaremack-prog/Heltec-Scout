// Heltec Scout - Wi-Fi, Bluetooth LE and GPS
#include "scout.h"
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <TinyGPSPlus.h>
#include <set>

// ======================= Wi-Fi scanner =======================
WifiNet wifiTop[24];
int wifiTopCount = 0, wifiTotalSeen = 0, wifiUniqueBssids = 0;
uint32_t wifiScans = 0;
static std::set<uint64_t> wifiSeen;
static uint32_t lastScanStart = 0;
static bool scanning = false;

static const char *encName(uint8_t e) {
  switch (e) {
    case WIFI_AUTH_OPEN: return "open";       case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";     case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2"; case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-ent";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";   case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    default: return "other";
  }
}

void wifiScanBegin() { lastScanStart = millis() - (cfg.wifiEverySec * 1000UL) + 4000; }

void wifiScanLoop() {
  if (!scanning) {
    if (millis() - lastScanStart < cfg.wifiEverySec * 1000UL) return;
    // A scan hops channels and briefly stalls the board's own hotspot; wait while someone is using it
    if (millis() - lastWebMs < 8000 || serialBusy()) return;
    lastScanStart = millis();
    if (WiFi.scanNetworks(true, true, false, 110) == WIFI_SCAN_FAILED) { WiFi.scanDelete(); return; }
    scanning = true;
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - lastScanStart > 15000) { WiFi.scanDelete(); scanning = false; }
    return;
  }
  scanning = false;
  if (n < 0) { WiFi.scanDelete(); return; }
  wifiScans++;
  wifiTotalSeen = n;
  wifiTopCount = 0;
  char line[160], ssid[40], mac[18];
  for (int i = 0; i < n; i++) {
    const uint8_t *b = WiFi.BSSID(i);
    uint64_t key = 0; for (int k = 0; k < 6; k++) key = (key << 8) | b[k];
    if (wifiSeen.size() < 6000) wifiSeen.insert(key);
    csvClean(WiFi.SSID(i).c_str(), ssid, sizeof ssid);
    macStr(b, mac);
    snprintf(line, sizeof line, "%s,%s,%d,%d,%s", mac, ssid, WiFi.channel(i), WiFi.RSSI(i), encName(WiFi.encryptionType(i)));
    logLine(LOG_WIFI, line);
    // keep the strongest ones for the screen and the web page
    WifiNet w; strlcpy(w.ssid, WiFi.SSID(i).c_str(), sizeof w.ssid); memcpy(w.bssid, b, 6);
    w.rssi = WiFi.RSSI(i); w.ch = WiFi.channel(i); w.enc = WiFi.encryptionType(i);
    int pos = wifiTopCount;
    while (pos > 0 && wifiTop[pos - 1].rssi < w.rssi) pos--;
    if (pos < 24) {
      int last = min(wifiTopCount, 23);
      for (int j = last; j > pos; j--) wifiTop[j] = wifiTop[j - 1];
      wifiTop[pos] = w;
      if (wifiTopCount < 24) wifiTopCount++;
    }
  }
  wifiUniqueBssids = wifiSeen.size();
  WiFi.scanDelete();
}

// ======================= Bluetooth LE scanner =======================
#define BLE_WIN 160
struct BleRec { uint8_t addr[6]; uint8_t type; char name[21]; int8_t rssi; uint16_t hits; uint16_t mfg; uint8_t mfgData[10]; uint8_t mfgLen; char svc[37]; uint32_t lastMs; };
static BleRec win[BLE_WIN];
static int winCount = 0;
static SemaphoreHandle_t bleMux = nullptr;
#define BLE_LOCK() xSemaphoreTake(bleMux, portMAX_DELAY)
#define BLE_UNLOCK() xSemaphoreGive(bleMux)
static std::set<uint64_t> bleSeen;
BleDev bleTop[16];
int bleTopCount = 0, bleWindowCount = 0, bleUnique = 0;
static uint32_t lastBleFlush = 0;
static bool blePaused = false;

static void bleKind(const BleRec &r, char *out, size_t n) {
  const char *k = "";
  if (r.mfg == 0x004C) {
    uint8_t t = r.mfgLen ? r.mfgData[0] : 0;
    k = t == 0x12 ? "FindMy" : t == 0x02 ? "iBeacon" : t == 0x07 ? "AirPods" : t == 0x10 ? "iPhone/Mac" :
        t == 0x0C ? "Handoff" : t == 0x09 ? "AirPlay" : t == 0x0F ? "Apple" : "Apple";
  } else if (r.mfg == 0x0006) k = "Microsoft";
  else if (r.mfg == 0x0075) k = "Samsung";
  else if (r.mfg == 0x00E0) k = "Google";
  else if (r.mfg == 0x0499) k = "Ruuvi";
  else if (r.mfg == 0x0059) k = "Nordic";
  else if (r.mfg == 0x0171) k = "Amazon";
  else if (r.mfg == 0x02E5) k = "Espressif";
  else if (r.mfg == 0x0087) k = "Garmin";
  else if (r.mfg == 0x038F) k = "Xiaomi";
  if (!*k && r.svc[0]) {
    if (strstr(r.svc, "feed") || strstr(r.svc, "feec")) k = "Tile";
    else if (strstr(r.svc, "fd5a")) k = "SmartTag";
    else if (strstr(r.svc, "fe2c")) k = "FastPair";
    else if (strstr(r.svc, "fd6f")) k = "Exposure";
    else if (strstr(r.svc, "180d")) k = "HeartRate";
    else if (strstr(r.svc, "181a")) k = "Sensor";
    else if (strstr(r.svc, "fe95")) k = "Xiaomi";
    else if (strstr(r.svc, "fcd2")) k = "BTHome";
  }
  strlcpy(out, k, n);
}

class ScoutBleCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *d) override {
    BleRec r = {};
    const uint8_t *a = d->getAddress().getNative();   // little-endian
    for (int i = 0; i < 6; i++) r.addr[i] = a[5 - i];
    r.type = d->getAddressType();
    r.rssi = d->getRSSI();
    r.lastMs = millis();
    if (d->haveName()) { std::string nm = d->getName(); strlcpy(r.name, nm.c_str(), sizeof r.name); }
    if (d->haveManufacturerData()) {
      std::string md = d->getManufacturerData();
      if (md.size() >= 2) {
        r.mfg = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
        r.mfgLen = min((int)md.size() - 2, 10);
        memcpy(r.mfgData, md.data() + 2, r.mfgLen);
      }
    }
    if (d->haveServiceUUID()) { std::string s = d->getServiceUUID().toString(); strlcpy(r.svc, s.c_str(), sizeof r.svc); }
    uint64_t key = 0; for (int k = 0; k < 6; k++) key = (key << 8) | r.addr[k];
    BLE_LOCK();
    int i = 0;
    for (; i < winCount; i++) if (!memcmp(win[i].addr, r.addr, 6)) break;
    if (i < winCount) {
      BleRec &w = win[i];
      if (r.rssi > w.rssi) w.rssi = r.rssi;
      w.hits++; w.lastMs = r.lastMs;
      if (r.name[0]) strlcpy(w.name, r.name, sizeof w.name);
      if (r.mfgLen) { w.mfg = r.mfg; w.mfgLen = r.mfgLen; memcpy(w.mfgData, r.mfgData, r.mfgLen); }
      if (r.svc[0]) strlcpy(w.svc, r.svc, sizeof w.svc);
    } else if (winCount < BLE_WIN) {
      r.hits = 1; win[winCount++] = r;
    }
    if (bleSeen.size() < 6000) bleSeen.insert(key);
    BLE_UNLOCK();
  }
};
static ScoutBleCb bleCb;

void bleBegin() {
  bleMux = xSemaphoreCreateMutex();
  NimBLEDevice::init("");
  NimBLEScan *s = NimBLEDevice::getScan();
  s->setAdvertisedDeviceCallbacks(&bleCb, true);
  s->setActiveScan(true);     // asks devices for their names
  s->setInterval(200);
  s->setWindow(60);           // ~30% duty so Wi-Fi keeps working well
  s->setMaxResults(0);        // results go to our own table, not NimBLE's
  s->setDuplicateFilter(false);
  s->start(0, nullptr, false);
}

void blePause(bool p) {
  blePaused = p;
  NimBLEScan *s = NimBLEDevice::getScan();
  if (p && s->isScanning()) s->stop();
}

void bleLoop() {
  NimBLEScan *s = NimBLEDevice::getScan();
  if (!blePaused && !s->isScanning()) s->start(0, nullptr, false);
  // every 15 s: log one line per device heard in the window, refresh the top list
  if (millis() - lastBleFlush < 15000) return;
  lastBleFlush = millis();
  static BleRec snap[BLE_WIN];
  int n;
  BLE_LOCK();
  n = winCount; memcpy(snap, win, sizeof(BleRec) * n); winCount = 0;
  bleUnique = bleSeen.size();
  BLE_UNLOCK();
  bleWindowCount = n;
  // sort strongest first (small n, insertion sort)
  for (int i = 1; i < n; i++) { BleRec t = snap[i]; int j = i - 1; while (j >= 0 && snap[j].rssi < t.rssi) { snap[j + 1] = snap[j]; j--; } snap[j + 1] = t; }
  char line[220], mac[18], nm[24], md[24], kind[12];
  int top = 0;
  for (int i = 0; i < n; i++) {
    BleRec &r = snap[i];
    macStr(r.addr, mac); csvClean(r.name, nm, sizeof nm); bleKind(r, kind, sizeof kind);
    md[0] = 0; for (int k = 0; k < r.mfgLen; k++) sprintf(md + k * 2, "%02x", r.mfgData[k]);
    char mfg[6] = ""; if (r.mfgLen || r.mfg) snprintf(mfg, sizeof mfg, "%04x", r.mfg);
    snprintf(line, sizeof line, "%s,%s,%s,%d,%u,%s,%s,%s,%s", mac,
             r.type == 0 ? "public" : r.type == 1 ? "random" : "other", nm, r.rssi, r.hits, mfg, md, r.svc, kind);
    logLine(LOG_BLE, line);
    if (top < 16) {
      BleDev &b = bleTop[top++];
      memcpy(b.addr, r.addr, 6); strlcpy(b.name, r.name, sizeof b.name); b.rssi = r.rssi; b.mfg = r.mfg;
      b.lastMs = r.lastMs; b.hits = r.hits; b.addrType = r.type; strlcpy(b.kind, kind, sizeof b.kind);
    }
  }
  bleTopCount = top;
}

// ======================= GPS =======================
static TinyGPSPlus gps;
static HardwareSerial gpsSer(1);
bool gpsSeen = false;
uint32_t gpsChars = 0;
static uint32_t lastGpsLog = 0, lastGpsTime = 0;

void gpsBegin() {
#ifdef GPS_EN
  if (!boardIsR8) { pinMode(GPS_EN, OUTPUT); digitalWrite(GPS_EN, cfg.gpsOn ? LOW : HIGH); }
#endif
  if (cfg.gpsOn && cfg.gpsRx >= 0) gpsSer.begin(cfg.gpsBaud, SERIAL_8N1, cfg.gpsRx, cfg.gpsTx);
  if (cfg.fixedLat != 0 || cfg.fixedLon != 0) gLoc = {cfg.fixedLat, cfg.fixedLon, 0, 0, "fixed", millis(), true};
}

void gpsLoop() {
  if (cfg.gpsOn && cfg.gpsRx >= 0) {
    while (gpsSer.available()) { gps.encode(gpsSer.read()); gpsChars++; }
    if (gps.sentencesWithFix() || gps.passedChecksum()) gpsSeen = true;
  }
  bool fix = gps.location.isValid() && gps.location.age() < 5000;
  if (fix) {
    gLoc = {gps.location.lat(), gps.location.lng(), (float)(gps.hdop.isValid() ? gps.hdop.hdop() : 0),
            (uint8_t)(gps.satellites.isValid() ? gps.satellites.value() : 0), "gps", millis(), true};
    if (millis() - lastGpsLog > 10000) {
      lastGpsLog = millis();
      char l[96];
      snprintf(l, sizeof l, "%u,%.1f,%.1f,%.1f,%.0f", gLoc.sats, gLoc.hdop, gps.altitude.meters(), gps.speed.kmph(), gps.course.deg());
      logLine(LOG_GPS, l);
    }
  } else if (gLoc.ok && strcmp(gLoc.src, "gps") == 0 && millis() - gLoc.atMs > 60000) {
    // lost the fix for a minute: keep the last spot but mark it as old
    gLoc.src = "gps-last";
  } else if (!gLoc.ok && (cfg.fixedLat != 0 || cfg.fixedLon != 0)) {
    gLoc = {cfg.fixedLat, cfg.fixedLon, 0, 0, "fixed", millis(), true};
  }
  if (gps.date.isValid() && gps.time.isValid() && gps.date.year() > 2023 && gps.time.age() < 2000 &&
      (millis() - lastGpsTime > 600000 || lastGpsTime == 0)) {
    struct tm t = {};
    t.tm_year = gps.date.year() - 1900; t.tm_mon = gps.date.month() - 1; t.tm_mday = gps.date.day();
    t.tm_hour = gps.time.hour(); t.tm_min = gps.time.minute(); t.tm_sec = gps.time.second();
    setenv("TZ", "UTC0", 1); tzset();
    time_t e = mktime(&t);
    if (e > 1700000000) { timeSet((uint32_t)e, "gps"); lastGpsTime = millis(); }
  }
}
