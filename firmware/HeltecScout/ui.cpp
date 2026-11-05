// Heltec Scout - OLED screens and the PRG button
//
// PRG button:
//   one press          -> screen sleep / wake
//   two quick presses  -> next screen now
//   hold 3 seconds     -> "Reboot?" prompt; one press then reboots (logs are kept)
// The board's RST button is wired straight to the chip's reset line, so the
// firmware can't see it being pressed; it simply restarts the board.
#include "scout.h"
#include <Wire.h>
#include <WiFi.h>
#include <SSD1306Wire.h>

static SSD1306Wire oled(OLED_ADDR, OLED_SDA, OLED_SCL, GEOMETRY_128_64);
bool boardIsR8 = false;
static bool oledOk = false, asleep = false, prompt = false;
static int screen = 0;
static const int SCREENS = 5;
static const char *names[SCREENS] = {"DRIVE", "WI-FI", "BLUETOOTH", "LORA", "900 MHz"};
static uint32_t lastFlip = 0, lastDraw = 0, lastInput = 0, promptAt = 0;

static bool probe() { Wire.begin(OLED_SDA, OLED_SCL); Wire.beginTransmission(OLED_ADDR); return Wire.endTransmission() == 0; }

void uiBegin() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_VEXT, OUTPUT); digitalWrite(PIN_VEXT, LOW);
  delay(30);
  pinMode(OLED_RST, OUTPUT); digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH); delay(20);
  bool found = probe();
#ifdef PIN_VEXT_ALT
  if (!found) { pinMode(PIN_VEXT_ALT, OUTPUT); digitalWrite(PIN_VEXT_ALT, LOW); delay(50);
                digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH); delay(20); found = probe(); boardIsR8 = found; }
  else { pinMode(PIN_VEXT_ALT, OUTPUT); digitalWrite(PIN_VEXT_ALT, HIGH); }  // also keeps a V4 GNSS module awake
#endif
  oledOk = oled.init();
  if (oledOk) { oled.flipScreenVertically(); oled.setContrast(200); }
  lastInput = lastFlip = millis();
}

void uiSplash(const char *l1, const char *l2) {
  if (!oledOk) return;
  oled.clear();
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  oled.setFont(ArialMT_Plain_16); oled.drawString(64, 6, "Heltec Scout");
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(64, 30, l1); oled.drawString(64, 44, l2);
  oled.display();
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
}

float batteryVolts() {
  if (boardIsR8) return 0;   // its divider control sits on a PSRAM pin
  pinMode(PIN_VBAT_CTRL, OUTPUT);
#if defined(SCOUT_BOARD_V4)
  digitalWrite(PIN_VBAT_CTRL, HIGH);
#else
  digitalWrite(PIN_VBAT_CTRL, LOW);
#endif
  delay(2);
  uint32_t mv = analogReadMilliVolts(PIN_VBAT);
  pinMode(PIN_VBAT_CTRL, INPUT);
  return mv * 4.9f * 1.045f / 1000.0f;
}

static void header() {
  oled.setFont(ArialMT_Plain_10);
  oled.drawString(0, 0, names[screen]);
  for (int i = 0; i < SCREENS; i++) {                  // page dots
    int x = 74 + i * 6;
    if (i == screen) oled.fillCircle(x, 5, 2); else oled.drawCircle(x, 5, 2);
  }
  oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  oled.drawString(128, 0, !storeOk ? "NO LOG" : storeIsSD ? "SD" : "INT");
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
  if (storeOk && (millis() / 600) % 2) oled.fillCircle(105, 5, 2);   // blinking "recording" dot
  oled.drawHorizontalLine(0, 12, 128);
}

static void fmtBytes(uint64_t b, char *o) {
  if (b >= 1000000000ULL) sprintf(o, "%.1fG", b / 1e9);
  else if (b >= 1000000ULL) sprintf(o, "%.0fM", b / 1e6);
  else sprintf(o, "%.0fK", b / 1e3);
}

static void drawDrive() {
  char l[48], a[12], b[12];
  oled.drawString(0, 14, cfg.apName);
  uint32_t u2 = utcNow();
  if (u2) { time_t tt = u2; struct tm tm; gmtime_r(&tt, &tm); snprintf(l, sizeof l, "%02d:%02dZ", tm.tm_hour, tm.tm_min); }
  else snprintf(l, sizeof l, "up %lum", (unsigned long)(millis() / 60000));
  oled.setTextAlignment(TEXT_ALIGN_RIGHT); oled.drawString(128, 14, l); oled.setTextAlignment(TEXT_ALIGN_LEFT);
  if (staIP != IPAddress(0, 0, 0, 0) && (millis() / 3000) % 2) snprintf(l, sizeof l, "%s", staIP.toString().c_str());
  else snprintf(l, sizeof l, "%s", apIP.toString().c_str());
  oled.drawString(0, 25, l);
  snprintf(l, sizeof l, "%d on", clientsOnAp);
  oled.setTextAlignment(TEXT_ALIGN_RIGHT); oled.drawString(128, 25, l); oled.setTextAlignment(TEXT_ALIGN_LEFT);
  uint64_t t, u; storageInfo(t, u); fmtBytes(u, a); fmtBytes(t, b);
  if (storeOk) snprintf(l, sizeof l, "%s %s/%s  s%04lu", storeIsSD ? "SD" : "Flash", a, b, (unsigned long)sessionNumber());
  else snprintf(l, sizeof l, "No microSD - not logging");
  oled.drawString(0, 36, l);
  if (gLoc.ok && !strncmp(gLoc.src, "gps", 3)) snprintf(l, sizeof l, "GPS %.4f,%.4f %us", gLoc.lat, gLoc.lon, gLoc.sats);
  else if (gLoc.ok) snprintf(l, sizeof l, "Loc %.4f,%.4f set", gLoc.lat, gLoc.lon);
  else if (gpsSeen) snprintf(l, sizeof l, "GPS: searching...");
  else snprintf(l, sizeof l, "GPS: none (estimate later)");
  oled.drawString(0, 47, l);
}

static void drawWifi() {
  char l[48];
  snprintf(l, sizeof l, "%d now  %d seen  #%lu", wifiTotalSeen, wifiUniqueBssids, (unsigned long)wifiScans);
  oled.drawString(0, 14, l);
  for (int i = 0; i < 4 && i < wifiTopCount; i++) {
    WifiNet &w = wifiTop[i];
    char ss[18]; strlcpy(ss, w.ssid[0] ? w.ssid : "(hidden)", sizeof ss);
    snprintf(l, sizeof l, "%4d %s", w.rssi, ss);
    oled.drawString(0, 25 + i * 10, l);
    oled.setTextAlignment(TEXT_ALIGN_RIGHT);
    snprintf(l, sizeof l, "%s%u", w.enc == 0 ? "o" : "", w.ch);
    oled.drawString(128, 25 + i * 10, l);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  }
  if (!wifiTopCount) oled.drawString(0, 36, wifiScans ? "No networks heard" : "First scan soon...");
}

static void drawBle() {
  char l[48];
  snprintf(l, sizeof l, "%d now  %d seen", bleWindowCount, bleUnique);
  oled.drawString(0, 14, l);
  for (int i = 0; i < 4 && i < bleTopCount; i++) {
    BleDev &b = bleTop[i];
    char nm[18];
    if (b.name[0]) strlcpy(nm, b.name, sizeof nm);
    else if (b.kind[0]) strlcpy(nm, b.kind, sizeof nm);
    else snprintf(nm, sizeof nm, "%02x:%02x:%02x", b.addr[3], b.addr[4], b.addr[5]);
    snprintf(l, sizeof l, "%4d %s", b.rssi, nm);
    oled.drawString(0, 25 + i * 10, l);
  }
  if (!bleTopCount) oled.drawString(0, 36, "Listening...");
}

static void drawLora() {
  char l[48];
  if (!radioOk) { oled.drawString(0, 30, "Radio didn't start"); return; }
  snprintf(l, sizeof l, "%.3f SF%u %g", cfg.loraFreq, cfg.loraSf, cfg.loraBw);
  oled.drawString(0, 14, l);
  snprintf(l, sizeof l, "%lu pkts  %lu nodes  %lu bad", (unsigned long)loraPackets, (unsigned long)loraNodes, (unsigned long)loraCrcErr);
  oled.drawString(0, 24, l);
  for (int i = 0; i < 3 && i < loraLastCount; i++) {
    LoraPkt &p = loraLast[i];
    uint32_t ago = (millis() - p.atMs) / 1000;
    if (p.hops >= 0) snprintf(l, sizeof l, "!%08lx %4.0f %4.1f %lus", (unsigned long)p.src, p.rssi, p.snr, (unsigned long)ago);
    else snprintf(l, sizeof l, "raw %ub %4.0f %4.1f %lus", p.len, p.rssi, p.snr, (unsigned long)ago);
    oled.drawString(0, 34 + i * 10, l);
  }
  if (!loraLastCount) oled.drawString(0, 40, "Listening for LoRa...");
}

static void drawRf() {
  char l[48];
  snprintf(l, sizeof l, "floor %.0f  hits %lu", sweepFloor, (unsigned long)rfHits);
  oled.drawString(0, 14, l);
  const int top = 26, h = 28, base = top + h;
  if (sweepBins > 1) {
    for (int x = 0; x < 128; x++) {
      int i = x * (sweepBins - 1) / 127;
      auto ht = [&](float r) { int v = (int)((r - (sweepFloor - 6)) * h / 50.0f); return constrain(v, 0, h); };
      int a = ht(sweepRssi[i]), p = ht(sweepPeak[i]);
      if (a) oled.drawVerticalLine(x, base - a, a);
      oled.setPixel(x, base - p);
    }
  }
  oled.drawHorizontalLine(0, base, 128);
  snprintf(l, sizeof l, "%.0f", cfg.sweepStart); oled.drawString(0, 54, l);
  oled.setTextAlignment(TEXT_ALIGN_RIGHT);
  snprintf(l, sizeof l, "%.0f", cfg.sweepStop); oled.drawString(128, 54, l);
  oled.setTextAlignment(TEXT_ALIGN_CENTER);
  if (rfBusiestMHz > 0) { snprintf(l, sizeof l, "busy %.2f", rfBusiestMHz); oled.drawString(64, 54, l); }
  oled.setTextAlignment(TEXT_ALIGN_LEFT);
}

static void draw() {
  oled.clear();
  if (prompt) {
    oled.setFont(ArialMT_Plain_16); oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.drawString(64, 4, "Reboot?");
    oled.setFont(ArialMT_Plain_10);
    oled.drawString(64, 28, "Press PRG to reboot");
    oled.drawString(64, 42, "Logs are kept");
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
  } else {
    header();
    switch (screen) { case 0: drawDrive(); break; case 1: drawWifi(); break; case 2: drawBle(); break; case 3: drawLora(); break; default: drawRf(); }
  }
  oled.display();
}

// ---------- button ----------
static bool btnWas = false, holdFired = false;
static uint32_t downAt = 0, lastRelease = 0;
static int clicks = 0;

static void setSleep(bool s) {
  asleep = s;
  if (!oledOk) return;
  if (s) oled.displayOff(); else { oled.displayOn(); draw(); }
}

static void doReboot() {
  uiSplash("Rebooting...", "logs saved");
  logEvent("reboot", "button");
  logFlush(true);
  delay(400);
  ESP.restart();
}

static void onSingle() {
  if (prompt) { doReboot(); return; }
  setSleep(!asleep);
}
static void onDouble() {
  if (prompt) { doReboot(); return; }
  if (asleep) { setSleep(false); return; }
  screen = (screen + 1) % SCREENS; lastFlip = millis(); draw();
}

static void buttonLoop() {
  bool down = digitalRead(PIN_BUTTON) == LOW;
  uint32_t now = millis();
  if (down && !btnWas) { downAt = now; holdFired = false; lastInput = now; }
  if (down && !holdFired && now - downAt >= 3000) {
    holdFired = true; clicks = 0;
    if (asleep) setSleep(false);
    prompt = true; promptAt = now; draw();
  }
  if (!down && btnWas && now - downAt > 30) {          // released (30 ms debounce)
    if (!holdFired) {
      if (prompt && now - promptAt > 300) { doReboot(); }
      clicks++; lastRelease = now;
    }
  }
  if (clicks && !down && now - lastRelease > 350) {
    if (clicks >= 2) onDouble(); else onSingle();
    clicks = 0;
  }
  btnWas = down;
  if (prompt && now - promptAt > 8000) { prompt = false; draw(); }   // gave up: back to normal
}

void uiLoop() {
  buttonLoop();
  if (!oledOk) return;
  uint32_t now = millis();
  if (!asleep && cfg.sleepMin && now - lastInput > cfg.sleepMin * 60000UL) setSleep(true);
  if (asleep || prompt) return;
  if (cfg.scrollSec && now - lastFlip > cfg.scrollSec * 1000UL) { screen = (screen + 1) % SCREENS; lastFlip = now; }
  if (now - lastDraw > 250) { lastDraw = now; draw(); }
}
