// Heltec Scout - shared state between modules
#pragma once
#include <Arduino.h>
#include <FS.h>
#include "board.h"

// ---------- settings (saved in NVS) ----------
struct Settings {
  char apName[33];
  char apPass[65];
  char staSsid[33];      // optional home Wi-Fi to also join
  char staPass[65];
  uint16_t scrollSec;    // seconds per screen when auto-scrolling (0 = off)
  uint16_t sleepMin;     // screen auto-sleep after N minutes with no button (0 = never)
  float loraFreq;        // MHz
  float loraBw;          // kHz
  uint8_t loraSf;
  uint8_t loraCr;
  uint8_t loraSync;
  float sweepStart, sweepStop, sweepStep; // MHz
  uint8_t sweepThresh;   // dB above noise floor that counts as activity
  uint16_t wifiEverySec;
  bool gpsOn;
  int8_t gpsRx, gpsTx;
  uint32_t gpsBaud;
  double fixedLat, fixedLon; // optional fixed location when there is no GPS (0,0 = none)
};
extern Settings cfg;
void settingsLoad();
void settingsSave();

// ---------- clock & location ----------
bool     timeValid();
uint32_t utcNow();                 // 0 if unknown
void     timeSet(uint32_t epoch, const char *source);
const char *timeSource();

struct Loc { double lat, lon; float hdop; uint8_t sats; const char *src; uint32_t atMs; bool ok; };
extern Loc gLoc;

// ---------- storage & logging ----------
extern fs::FS *store;       // SD or internal flash
extern bool    storeIsSD;
extern bool    storeOk;
extern char    sessionDir[24];
extern SemaphoreHandle_t storeLock;
bool  storageBegin();
void  storageInfo(uint64_t &total, uint64_t &used);
enum LogId { LOG_WIFI, LOG_BLE, LOG_LORA, LOG_RF, LOG_GPS, LOG_EVENT, LOG_COUNT };
void  logLine(LogId id, const char *body);     // prefix (utc,uptime,lat,lon,src) is added
void  logEvent(const char *what, const char *detail);
void  logFlush(bool force = false);
uint32_t logLines(LogId id);
uint32_t sessionNumber();

// ---------- Wi-Fi scanner ----------
struct WifiNet { char ssid[33]; uint8_t bssid[6]; int8_t rssi; uint8_t ch; uint8_t enc; };
extern WifiNet wifiTop[24];
extern int     wifiTopCount, wifiTotalSeen, wifiUniqueBssids;
extern uint32_t wifiScans;
void wifiScanBegin();
void wifiScanLoop();

// ---------- BLE scanner ----------
struct BleDev { uint8_t addr[6]; char name[21]; int8_t rssi; uint16_t mfg; uint32_t lastMs; uint16_t hits; uint8_t addrType; char kind[12]; };
extern BleDev bleTop[16];
extern int    bleTopCount, bleWindowCount, bleUnique;
void bleBegin();
void bleLoop();
void blePause(bool p);

// ---------- LoRa radio ----------
struct LoraPkt { uint32_t src, dst, id; int8_t hops; float rssi, snr; uint8_t len, chHash; uint32_t atMs; };
extern LoraPkt loraLast[8];
extern int     loraLastCount;
extern uint32_t loraPackets, loraNodes, loraCrcErr;
extern bool    radioOk;
#define SWEEP_MAX 200
extern int     sweepBins;
extern float   sweepRssi[SWEEP_MAX];   // latest sweep
extern float   sweepPeak[SWEEP_MAX];   // decaying peak hold
extern float   sweepFloor;
extern uint32_t sweepCount, rfHits;
extern float   rfBusiestMHz;
void radioBegin();
void radioLoop();
void radioApplySettings();

// ---------- GPS ----------
extern bool gpsSeen;      // any NMEA received
extern uint32_t gpsChars;
void gpsBegin();
void gpsLoop();

// ---------- network / web / serial ----------
extern volatile uint32_t lastWebMs;
extern IPAddress apIP, staIP;
extern int clientsOnAp;
void netBegin();
void netLoop();
void serialLoop();
bool serialBusy();

// ---------- screen & button ----------
void uiBegin();
void uiLoop();
void uiSplash(const char *line1, const char *line2);
float batteryVolts();
extern bool boardIsR8;   // V4-R8: screen power on GPIO40, octal PSRAM on 33-37 (left alone)

// ---------- helpers ----------
void macStr(const uint8_t *m, char *out);   // "aa:bb:cc:dd:ee:ff"
void csvClean(const char *in, char *out, size_t n); // strip commas/quotes/newlines
