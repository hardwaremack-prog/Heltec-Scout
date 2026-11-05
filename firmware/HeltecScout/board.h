// Heltec Scout - board pin map
// Same wiring as Heltec Shelf Lite, so a board already wired for Shelf Lite needs no changes.
// The V4 firmware is built with -DSCOUT_BOARD_V4; everything else defaults to V3.
#pragma once

#define FW_VERSION "1.0.0"

#if defined(SCOUT_BOARD_V4)
  #define BOARD_NAME "Heltec V4"
  #define BOARD_ID   "v4"
#else
  #define BOARD_NAME "Heltec V3"
  #define BOARD_ID   "v3"
#endif

// --- Built in to the board (same on V3 and V4) ---
#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21
#define OLED_ADDR  0x3C
#define PIN_BUTTON 0      // PRG button (RST is wired straight to the chip's reset line)
#define PIN_VEXT   36     // LOW = OLED/Ve power on (V3 and V4-R2)
#define PIN_LED    35
#define PIN_VBAT   1
#define PIN_VBAT_CTRL 37

// --- SX1262 LoRa radio ---
#define LORA_CS    8
#define LORA_SCK   9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

#if defined(SCOUT_BOARD_V4)
  #define PIN_VEXT_ALT   40 // Vext on the V4-R8 variant (only used if the screen doesn't answer on 36)
  // V4 high-power front end: set to receive-with-LNA
  #define FEM_POWER   7     // HIGH = front-end LDO on
  #define FEM_CSD     2     // HIGH = chip enabled
  #define FEM_CPS     46    // LOW  = no PA (GC1109 V4.2)
  #define FEM_CTX     5     // LOW  = RX LNA (KCT8103L V4.3)
  // GNSS expansion port (L76K / CM121)
  #define GPS_RX_DEF  39
  #define GPS_TX_DEF  38
  #define GPS_EN      34    // LOW = GPS powered
#else
  // V3 has no GPS port. Any 3.3 V NMEA module wired to these pins works.
  #define GPS_RX_DEF  39
  #define GPS_TX_DEF  38
#endif

// --- microSD module on its own SPI bus (same as Shelf Lite) ---
#define SD_CS    4
#define SD_MISO  6
#define SD_SCK   47
#define SD_MOSI  48
