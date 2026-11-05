// Heltec Scout - Wi-Fi thumb drive + Wi-Fi / Bluetooth / LoRa / 900 MHz scanner with microSD logging
// for the Heltec WiFi LoRa 32 V3 and V4.
#include "scout.h"
#include <esp_system.h>

static const char *resetName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power_on_or_rst_button";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    default: return "other";
  }
}

void setup() {
  Serial.begin(921600);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(921600);
#endif
  pinMode(PIN_LED, OUTPUT); digitalWrite(PIN_LED, LOW);
  settingsLoad();
  uiBegin();
  uiSplash("Starting up...", BOARD_NAME " v" FW_VERSION);
  bool st = storageBegin();
  uiSplash(st ? (storeIsSD ? "microSD ready" : "No card: using flash") : "No storage!", sessionDir);
  char d[96];
  snprintf(d, sizeof d, "%s fw %s reset=%s", BOARD_NAME, FW_VERSION, resetName());
  logEvent("boot", d);
  netBegin();
  bleBegin();
  radioBegin();
  gpsBegin();
  wifiScanBegin();
  Serial.printf("# Heltec Scout %s on %s, logging to %s\n", FW_VERSION, BOARD_NAME, sessionDir);
}

void loop() {
  serialLoop();
  netLoop();
  radioLoop();
  wifiScanLoop();
  bleLoop();
  gpsLoop();
  uiLoop();
  logFlush();
  // white LED: quick blink for each LoRa packet
  static uint32_t seen = 0, ledOff = 0;
  if (loraPackets != seen) { seen = loraPackets; digitalWrite(PIN_LED, HIGH); ledOff = millis() + 60; }
  if (ledOff && millis() > ledOff) { digitalWrite(PIN_LED, LOW); ledOff = 0; }
  delay(2);
}
