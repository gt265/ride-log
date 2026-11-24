// Ride Log bike computer: settings. Change these, then upload again.
#pragma once

// ---- you ----
#define MAX_HR        190     // for heart-rate zones (0 = off)
#define FTP_WATTS     250     // for power zones (0 = off)
#define AUTO_PAUSE    true
#define AUTO_LAP_KM   0       // 0 = off, or e.g. 1, 5, 10

// ---- Wi-Fi transfer hotspot (join it from your phone to copy rides) ----
#define AP_PASSWORD   "ridelog123"   // at least 8 characters; change it

// ---- pins (Seeed XIAO ESP32-S3), matching hardware/README.md ----
#define PIN_BTN_LAP    D0
#define PIN_BTN_START  D1
#define PIN_BTN_PAGE   D2     // also wakes the device from sleep
#define PIN_DISP_CS    D3
#define PIN_DISP_SCK   D8
#define PIN_DISP_MOSI  D10
#define PIN_BATT       D9     // middle of the 220k/220k divider
#define GPS_I2C_ADDR   0x10
#define BARO_I2C_ADDR  0x77
