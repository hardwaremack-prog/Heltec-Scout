// Heltec Scout - SX1262: LoRa packet listener + 900 MHz band activity sweep
#include "scout.h"
#include <RadioLib.h>
#include <SPI.h>
#include <set>

static SPIClass loraSPI(FSPI);
static SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI);

bool radioOk = false;
LoraPkt loraLast[8];
int loraLastCount = 0;
uint32_t loraPackets = 0, loraNodes = 0, loraCrcErr = 0;
static std::set<uint32_t> nodes;

int sweepBins = 0;
float sweepRssi[SWEEP_MAX], sweepPeak[SWEEP_MAX], sweepFloor = -120;
static float sweepMaxMin[SWEEP_MAX];    // max hold over the current minute (logged)
static uint16_t binHits[SWEEP_MAX];
uint32_t sweepCount = 0, rfHits = 0;
float rfBusiestMHz = 0;

static volatile bool rxFlag = false;
static uint32_t lastPktMs = 0, lastSweep = 0, lastMinuteLog = 0;
static void IRAM_ATTR onRx() { rxFlag = true; }

static void femReceive() {
#if defined(SCOUT_BOARD_V4)
  pinMode(FEM_POWER, OUTPUT); digitalWrite(FEM_POWER, HIGH);
  pinMode(FEM_CSD, OUTPUT);   digitalWrite(FEM_CSD, HIGH);
  pinMode(FEM_CPS, OUTPUT);   digitalWrite(FEM_CPS, LOW);
  pinMode(FEM_CTX, OUTPUT);   digitalWrite(FEM_CTX, LOW);   // LNA on for both front-end chips
#endif
}

void radioApplySettings() {
  if (!radioOk) return;
  radio.standby();
  radio.setFrequency(cfg.loraFreq);
  radio.setBandwidth(cfg.loraBw);
  radio.setSpreadingFactor(cfg.loraSf);
  radio.setCodingRate(cfg.loraCr);
  radio.setSyncWord(cfg.loraSync);
  radio.setPreambleLength(16);
  sweepBins = min((int)((cfg.sweepStop - cfg.sweepStart) / cfg.sweepStep) + 1, SWEEP_MAX);
  for (int i = 0; i < SWEEP_MAX; i++) { sweepPeak[i] = -130; sweepRssi[i] = -130; sweepMaxMin[i] = -130; binHits[i] = 0; }
  radio.startReceive();
}

void radioBegin() {
  femReceive();
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int st = radio.begin(cfg.loraFreq, cfg.loraBw, cfg.loraSf, cfg.loraCr, cfg.loraSync, 2, 16, 1.8, false);
  if (st != RADIOLIB_ERR_NONE) { radioOk = false; char d[24]; snprintf(d, sizeof d, "code %d", st); logEvent("radio_fail", d); return; }
  radioOk = true;
  radio.setDio2AsRfSwitch(true);
  radio.setCRC(2);
  radio.setRxBoostedGainMode(true);
  radio.setPacketReceivedAction(onRx);
  radioApplySettings();
}

static void handlePacket() {
  uint8_t buf[256];
  size_t len = radio.getPacketLength();
  if (len > sizeof buf) len = sizeof buf;
  int st = radio.readData(buf, len);
  float rssi = radio.getRSSI(), snr = radio.getSNR();
  radio.startReceive();
  if (st == RADIOLIB_ERR_CRC_MISMATCH) { loraCrcErr++; return; }
  if (st != RADIOLIB_ERR_NONE || len == 0) return;
  loraPackets++; lastPktMs = millis();
  LoraPkt p = {};
  p.rssi = rssi; p.snr = snr; p.len = len; p.atMs = millis(); p.hops = -1;
  char hex[2 * 240 + 1]; size_t hl = min(len, (size_t)240);
  for (size_t i = 0; i < hl; i++) sprintf(hex + i * 2, "%02x", buf[i]);
  hex[hl * 2] = 0;
  char line[640];
  if (len >= 16) {   // Meshtastic radio header (always unencrypted)
    p.dst = buf[0] | buf[1] << 8 | buf[2] << 16 | (uint32_t)buf[3] << 24;
    p.src = buf[4] | buf[5] << 8 | buf[6] << 16 | (uint32_t)buf[7] << 24;
    p.id  = buf[8] | buf[9] << 8 | buf[10] << 16 | (uint32_t)buf[11] << 24;
    uint8_t fl = buf[12];
    p.hops = fl & 7; p.chHash = buf[13];
    nodes.insert(p.src); loraNodes = nodes.size();
    snprintf(line, sizeof line, "%.3f,%.1f,%.2f,%u,%08lx,%08lx,%08lx,%u,%u,%u,%u,%02x,%02x,%02x,%s",
             cfg.loraFreq, rssi, snr, (unsigned)len, (unsigned long)p.dst, (unsigned long)p.src, (unsigned long)p.id,
             fl & 7, (fl >> 5) & 7, (fl >> 3) & 1, (fl >> 4) & 1, buf[13], buf[14], buf[15], hex);
  } else {
    snprintf(line, sizeof line, "%.3f,%.1f,%.2f,%u,,,,,,,,,,,%s", cfg.loraFreq, rssi, snr, (unsigned)len, hex);
  }
  logLine(LOG_LORA, line);
  memmove(&loraLast[1], &loraLast[0], sizeof(LoraPkt) * 7);
  loraLast[0] = p;
  if (loraLastCount < 8) loraLastCount++;
}

static int cmpf(const void *a, const void *b) { float x = *(float *)a, y = *(float *)b; return x < y ? -1 : x > y; }

static void sweep() {
  radio.standby();
  radio.setFrequency((cfg.sweepStart + cfg.sweepStop) / 2);   // one image calibration covers the band
  for (int i = 0; i < sweepBins; i++) {
    float f = cfg.sweepStart + i * cfg.sweepStep;
    radio.standby();
    radio.setFrequency(f, true);           // skip re-calibration for speed
    radio.startReceive();
    delayMicroseconds(700);
    sweepRssi[i] = radio.getRSSI(false);   // instantaneous channel power
  }
  radio.standby();
  radio.setFrequency(cfg.loraFreq);
  rxFlag = false;
  radio.startReceive();
  sweepCount++;
  static float tmp[SWEEP_MAX];
  memcpy(tmp, sweepRssi, sizeof(float) * sweepBins);
  qsort(tmp, sweepBins, sizeof(float), cmpf);
  sweepFloor = tmp[sweepBins / 2];
  int best = -1; uint16_t bestHits = 0;
  char line[96];
  for (int i = 0; i < sweepBins; i++) {
    sweepPeak[i] = max(sweepRssi[i], sweepPeak[i] - 1.5f);
    sweepMaxMin[i] = max(sweepMaxMin[i], sweepRssi[i]);
    if (sweepRssi[i] > sweepFloor + cfg.sweepThresh) {
      rfHits++; if (binHits[i] < 65000) binHits[i]++;
      snprintf(line, sizeof line, "hit,%.3f,%.1f,%.1f,", cfg.sweepStart + i * cfg.sweepStep, sweepRssi[i], sweepFloor);
      logLine(LOG_RF, line);
    }
    if (binHits[i] > bestHits) { bestHits = binHits[i]; best = i; }
  }
  if (best >= 0) rfBusiestMHz = cfg.sweepStart + best * cfg.sweepStep;
  // once a minute: the whole band's max-hold, so the analyzer can draw a waterfall
  if (millis() - lastMinuteLog > 60000) {
    lastMinuteLog = millis();
    String d; d.reserve(sweepBins * 5 + 32);
    d += String(cfg.sweepStart, 3); d += ':'; d += String(cfg.sweepStep, 3); d += ':';
    for (int i = 0; i < sweepBins; i++) { if (i) d += ';'; d += String((int)lroundf(sweepMaxMin[i])); sweepMaxMin[i] = -130; }
    char head[48]; snprintf(head, sizeof head, "sweep,,,%.1f,", sweepFloor);
    String full = String(head) + d;
    logLine(LOG_RF, full.c_str());
  }
}

void radioLoop() {
  if (!radioOk) return;
  if (rxFlag) { rxFlag = false; handlePacket(); }
  // sweep every 3 s, but not right after a packet (a reply or rebroadcast often follows)
  if (millis() - lastSweep > 3000 && millis() - lastPktMs > 400) {
    lastSweep = millis();
    sweep();
  }
}
