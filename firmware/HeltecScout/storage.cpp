// Heltec Scout - settings, clock, storage and CSV logging
#include "scout.h"
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <FFat.h>

Settings cfg;
static Preferences prefs;

void settingsLoad() {
  prefs.begin("scout", true);
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char defAp[33]; snprintf(defAp, sizeof defAp, "Scout-%02X%02X", mac[4], mac[5]);
  strlcpy(cfg.apName,  prefs.getString("apName", defAp).c_str(), sizeof cfg.apName);
  strlcpy(cfg.apPass,  prefs.getString("apPass", "heltecscout").c_str(), sizeof cfg.apPass);
  strlcpy(cfg.staSsid, prefs.getString("staSsid", "").c_str(), sizeof cfg.staSsid);
  strlcpy(cfg.staPass, prefs.getString("staPass", "").c_str(), sizeof cfg.staPass);
  cfg.scrollSec   = prefs.getUShort("scroll", 6);
  cfg.sleepMin    = prefs.getUShort("sleepMin", 0);
  cfg.loraFreq    = prefs.getFloat("lFreq", 906.875f);   // Meshtastic US LongFast default slot
  cfg.loraBw      = prefs.getFloat("lBw", 250.0f);
  cfg.loraSf      = prefs.getUChar("lSf", 11);
  cfg.loraCr      = prefs.getUChar("lCr", 5);
  cfg.loraSync    = prefs.getUChar("lSync", 0x2B);
  cfg.sweepStart  = prefs.getFloat("swA", 902.0f);
  cfg.sweepStop   = prefs.getFloat("swB", 928.0f);
  cfg.sweepStep   = prefs.getFloat("swS", 0.25f);
  cfg.sweepThresh = prefs.getUChar("swT", 12);
  cfg.wifiEverySec= prefs.getUShort("wEvery", 20);
  cfg.gpsOn       = prefs.getBool("gpsOn", true);
  cfg.gpsRx       = prefs.getChar("gpsRx", GPS_RX_DEF);
  cfg.gpsTx       = prefs.getChar("gpsTx", GPS_TX_DEF);
  cfg.gpsBaud     = prefs.getUInt("gpsBaud", 9600);
  cfg.fixedLat    = prefs.getDouble("fixLat", 0);
  cfg.fixedLon    = prefs.getDouble("fixLon", 0);
  prefs.end();
  if (cfg.sweepStep < 0.05f) cfg.sweepStep = 0.05f;
  if (cfg.sweepStop <= cfg.sweepStart) cfg.sweepStop = cfg.sweepStart + 1;
}

void settingsSave() {
  prefs.begin("scout", false);
  prefs.putString("apName", cfg.apName);   prefs.putString("apPass", cfg.apPass);
  prefs.putString("staSsid", cfg.staSsid); prefs.putString("staPass", cfg.staPass);
  prefs.putUShort("scroll", cfg.scrollSec); prefs.putUShort("sleepMin", cfg.sleepMin);
  prefs.putFloat("lFreq", cfg.loraFreq);   prefs.putFloat("lBw", cfg.loraBw);
  prefs.putUChar("lSf", cfg.loraSf);       prefs.putUChar("lCr", cfg.loraCr);
  prefs.putUChar("lSync", cfg.loraSync);
  prefs.putFloat("swA", cfg.sweepStart);   prefs.putFloat("swB", cfg.sweepStop);
  prefs.putFloat("swS", cfg.sweepStep);    prefs.putUChar("swT", cfg.sweepThresh);
  prefs.putUShort("wEvery", cfg.wifiEverySec);
  prefs.putBool("gpsOn", cfg.gpsOn);
  prefs.putChar("gpsRx", cfg.gpsRx);       prefs.putChar("gpsTx", cfg.gpsTx);
  prefs.putUInt("gpsBaud", cfg.gpsBaud);
  prefs.putDouble("fixLat", cfg.fixedLat); prefs.putDouble("fixLon", cfg.fixedLon);
  prefs.end();
}

// ---------- clock ----------
static int64_t epochAtBoot = 0;   // utc seconds when millis() was 0
static const char *tSrc = "none";
bool timeValid() { return epochAtBoot != 0; }
uint32_t utcNow() { return epochAtBoot ? (uint32_t)(epochAtBoot + millis() / 1000) : 0; }
const char *timeSource() { return tSrc; }
void timeSet(uint32_t epoch, const char *source) {
  if (epoch < 1700000000UL) return;                       // obviously bogus
  bool first = !timeValid();
  int64_t nb = (int64_t)epoch - millis() / 1000;
  // GPS beats a phone/PC clock; don't let a later phone sync override GPS time
  if (!first && strcmp(tSrc, "gps") == 0 && strcmp(source, "gps") != 0) return;
  if (!first && llabs(nb - epochAtBoot) < 2) { tSrc = source; return; }
  epochAtBoot = nb; tSrc = source;
  char d[48]; snprintf(d, sizeof d, "%lu %s", (unsigned long)epoch, source);
  logEvent("time_set", d);   // lets the analyzer back-date rows logged before the clock was known
}

Loc gLoc = {0, 0, 0, 0, "", 0, false};

// ---------- storage ----------
fs::FS *store = nullptr;
bool storeIsSD = false, storeOk = false;
char sessionDir[24] = "";
SemaphoreHandle_t storeLock;
static SPIClass sdSPI(HSPI);
static uint32_t sessNo = 0;
uint32_t sessionNumber() { return sessNo; }

struct LogF { const char *name; const char *head; File f; uint32_t lines; bool dirty; };
#define PFX "utc,uptime_s,lat,lon,loc_src,"
static LogF logs[LOG_COUNT] = {
  {"wifi.csv",   PFX "bssid,ssid,channel,rssi,enc", File(), 0, false},
  {"ble.csv",    PFX "addr,addr_type,name,rssi_max,hits,mfg_id,mfg_data,svc_uuid,kind", File(), 0, false},
  {"lora.csv",   PFX "freq_mhz,rssi,snr,len,dest,src,pkt_id,hop_limit,hop_start,want_ack,via_mqtt,chan_hash,next_hop,relay,payload_hex", File(), 0, false},
  {"rf900.csv",  PFX "kind,freq_mhz,rssi,floor,detail", File(), 0, false},
  {"gps.csv",    PFX "sats,hdop,alt_m,speed_kmh,course", File(), 0, false},
  {"events.csv", PFX "event,detail", File(), 0, false},
};

bool storageBegin() {
  storeLock = xSemaphoreCreateRecursiveMutex();
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS, sdSPI, 20000000) || SD.begin(SD_CS, sdSPI, 4000000)) {
    store = &SD; storeIsSD = true; storeOk = true;
  } else if (FFat.begin(true)) {
    store = &FFat; storeIsSD = false; storeOk = true;
  } else {
    storeOk = false; return false;
  }
  prefs.begin("scout", false);
  sessNo = prefs.getUInt("session", 0) + 1;
  prefs.putUInt("session", sessNo);
  prefs.end();
  if (!store->exists("/scout")) store->mkdir("/scout");
  snprintf(sessionDir, sizeof sessionDir, "/scout/s%04lu", (unsigned long)sessNo);
  store->mkdir(sessionDir);
  for (int i = 0; i < LOG_COUNT; i++) {
    char p[48]; snprintf(p, sizeof p, "%s/%s", sessionDir, logs[i].name);
    logs[i].f = store->open(p, FILE_APPEND);
    if (logs[i].f && logs[i].f.size() == 0) { logs[i].f.println(logs[i].head); }
  }
  char p[48]; snprintf(p, sizeof p, "%s/session.txt", sessionDir);
  File s = store->open(p, FILE_WRITE);
  if (s) {
    uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA); char m[18]; macStr(mac, m);
    s.printf("session=%lu\nboard=%s\nfirmware=%s\nmac=%s\nstorage=%s\n", (unsigned long)sessNo, BOARD_NAME, FW_VERSION, m, storeIsSD ? "microSD" : "internal");
    s.close();
  }
  return true;
}

void storageInfo(uint64_t &total, uint64_t &used) {
  total = used = 0;
  if (!storeOk) return;
  if (storeIsSD) { total = SD.totalBytes(); used = SD.usedBytes(); }
  else { total = FFat.totalBytes(); used = FFat.usedBytes(); }
}

static uint32_t lastFlush = 0;
static bool lowSpace = false;

void logLine(LogId id, const char *body) {
  if (!storeOk || id >= LOG_COUNT || !logs[id].f || lowSpace) return;
  char pre[96];
  uint32_t u = utcNow();
  char ut[12] = ""; if (u) snprintf(ut, sizeof ut, "%lu", (unsigned long)u);
  if (gLoc.ok) snprintf(pre, sizeof pre, "%s,%lu,%.6f,%.6f,%s,", ut, (unsigned long)(millis() / 1000), gLoc.lat, gLoc.lon, gLoc.src);
  else         snprintf(pre, sizeof pre, "%s,%lu,,,,", ut, (unsigned long)(millis() / 1000));
  xSemaphoreTakeRecursive(storeLock, portMAX_DELAY);
  logs[id].f.print(pre);
  logs[id].f.println(body);
  logs[id].lines++; logs[id].dirty = true;
  xSemaphoreGiveRecursive(storeLock);
}

void logEvent(const char *what, const char *detail) {
  char b[160], d[120]; csvClean(detail, d, sizeof d);
  snprintf(b, sizeof b, "%s,%s", what, d);
  logLine(LOG_EVENT, b);
}

void logFlush(bool force) {
  if (!storeOk) return;
  if (!force && millis() - lastFlush < 4000) return;
  lastFlush = millis();
  xSemaphoreTakeRecursive(storeLock, portMAX_DELAY);
  for (int i = 0; i < LOG_COUNT; i++) if (logs[i].dirty && logs[i].f) { logs[i].f.flush(); logs[i].dirty = false; }
  xSemaphoreGiveRecursive(storeLock);
  static uint32_t lastSpace = 0;
  if (millis() - lastSpace > 60000 || lastSpace == 0) {   // stop writing with <8 MB left so the card never fills solid
    lastSpace = millis();
    uint64_t t, u; storageInfo(t, u);
    bool low = t && (t - u) < 8ULL * 1024 * 1024;
    if (low && !lowSpace) logEvent("storage_full", "logging paused");
    lowSpace = low;
  }
}

uint32_t logLines(LogId id) { return id < LOG_COUNT ? logs[id].lines : 0; }

void macStr(const uint8_t *m, char *out) {
  sprintf(out, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}
void csvClean(const char *in, char *out, size_t n) {
  size_t j = 0;
  for (size_t i = 0; in && in[i] && j + 1 < n; i++) {
    char c = in[i];
    if (c == ',' ) c = ';';
    else if (c == '"' || c == '\r' || c == '\n' || (uint8_t)c < 32) c = ' ';
    out[j++] = c;
  }
  out[j] = 0;
}
