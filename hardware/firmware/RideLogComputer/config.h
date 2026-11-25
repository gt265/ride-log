// Ride Log bike computer: settings. Change these, then upload again.
#pragma once

// ---- which hardware you built ----
// HW_V1_ALIEXPRESS : Waveshare 2.9" e-paper + ATGM336H GPS + BMP280 + power switch (cheap parts)
// HW_ADAFRUIT      : Adafruit Sharp 2.7" memory LCD + PA1010D GPS + BMP390 (hardware/README.md)
#define HW_V1_ALIEXPRESS
// #define HW_ADAFRUIT

// ---- you ----
#define MAX_HR          190     // for heart-rate zones (0 = off)
#define FTP_WATTS       250     // for power zones (0 = off)
#define AUTO_PAUSE      true
#define AUTO_LAP_KM     0       // 0 = off, or e.g. 1, 5, 10
#define UTC_OFFSET_MIN  0       // your time zone in minutes, e.g. 60 for UTC+1, -300 for New York (winter)

// ---- Wi-Fi transfer hotspot (join it from your phone to copy rides) ----
#define AP_PASSWORD   "ridelog123"   // at least 8 characters; change it

// ---- pins (Seeed XIAO ESP32-S3) ----
#if defined(HW_V1_ALIEXPRESS)
  #define PIN_BTN_LAP    D0
  #define PIN_BTN_START  D1
  #define PIN_BTN_PAGE   D2
  #define PIN_EPD_CS     D3
  //      I2C SDA = D4, SCL = D5 (BMP280)
  #define PIN_EPD_DC     D6
  #define PIN_GPS_RX     D7     // ← GPS module "TX" pin
  #define PIN_EPD_SCK    D8
  #define PIN_EPD_BUSY   D9
  #define PIN_EPD_MOSI   D10
  #define PIN_EPD_RST    -1     // e-paper RST wired to 3V3
  #define PIN_BATT       -1     // no spare pin on v1: battery % not shown
  #define GPS_BAUD       9600
  #define BARO_I2C_ADDR  0x76   // most GY-BMP280 boards; try 0x77 if "Baro missing"
#elif defined(HW_ADAFRUIT)
  #define PIN_BTN_LAP    D0
  #define PIN_BTN_START  D1
  #define PIN_BTN_PAGE   D2     // also wakes the device from sleep
  #define PIN_DISP_CS    D3
  #define PIN_DISP_SCK   D8
  #define PIN_DISP_MOSI  D10
  #define PIN_BATT       D9     // middle of the 220k/220k divider
  #define GPS_I2C_ADDR   0x10
  #define BARO_I2C_ADDR  0x77
#else
  #error "Pick HW_V1_ALIEXPRESS or HW_ADAFRUIT at the top of config.h"
#endif
