/*
  Ride Log bike computer: firmware (Seeed XIAO ESP32-S3)
  Pick your hardware at the top of config.h:
    HW_V1_ALIEXPRESS  Waveshare 2.9" e-paper (296x128) + ATGM336H GPS (UART) + BMP280 + power switch
    HW_ADAFRUIT       Adafruit Sharp 2.7" memory LCD (400x240) + PA1010D GPS (I2C) + BMP390

  Arduino IDE: board "XIAO_ESP32S3", PSRAM "OPI PSRAM". Libraries (Library Manager):
    Adafruit GFX Library, Adafruit GPS Library, NimBLE-Arduino (version 1.4.x), and
    v1:       GxEPD2, Adafruit BMP280 Library
    Adafruit: Adafruit SHARP Memory Display, Adafruit BMP3XX Library

  Buttons
    START  short: start / pause / resume        long (2 s): finish and save the ride
    LAP    short: new lap
    PAGE   short: next screen                   long (3 s): v1: full screen refresh / Adafruit: sleep
    On the "Device" screen with no ride running, hold START to switch Wi-Fi transfer on/off.

  Copy rides to the app: Wi-Fi transfer on → join "RideLog-xxxx" on your phone → open
  http://192.168.4.1 → Download → in Ride Log: Settings → Import.
*/
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GPS.h>
#include <NimBLEDevice.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "config.h"
#include "ride_logic.h"

#if defined(HW_V1_ALIEXPRESS)
  #include <GxEPD2_BW.h>
  #include <Adafruit_BMP280.h>
  // Waveshare 2.9" e-Paper Module V2 (SSD1680). If the picture is blank or garbled, try GxEPD2_290_BS.
  GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(GxEPD2_290_T94_V2(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));
  static const uint16_t INK = GxEPD_BLACK, PAPER = GxEPD_WHITE;
  Adafruit_GPS GPS(&Serial1);
  Adafruit_BMP280 baro;
  #define SCREEN_W 296
  #define SCREEN_H 128
#else
  #include <Adafruit_SharpMem.h>
  #include <Adafruit_BMP3XX.h>
  Adafruit_SharpMem display(&SPI, PIN_DISP_CS, 400, 240, 2000000);
  static const uint16_t INK = 0, PAPER = 1;          // Sharp memory LCD: 0 = black, 1 = white
  Adafruit_GPS GPS(&Wire);
  Adafruit_BMP3XX baro;
  #define SCREEN_W 400
  #define SCREEN_H 240
#endif

using ridelog::Ride;
WebServer server(80);

Ride ride;
bool gpsFix = false, baroOk = false, wifiOn = false;
int sats = 0; double gpsAccuracy = 99;
uint32_t lastFixMs = 0;
enum Page { PAGE_RIDE, PAGE_SENSORS, PAGE_LAPS, PAGE_DEVICE, PAGE_COUNT };
int page = PAGE_RIDE;
char flashMsg[64] = ""; uint32_t flashUntil = 0;
char lastSaved[48] = "";

// ============================================================ helpers
void flash(const char* msg, uint32_t ms = 5000) { strncpy(flashMsg, msg, sizeof(flashMsg) - 1); flashUntil = millis() + ms; }

int batteryPercent() {   // -1 = can't measure on this hardware
  if (PIN_BATT < 0) return -1;
  const float v = analogReadMilliVolts(PIN_BATT) * 2 / 1000.0f;   // 220k/220k divider
  return constrain(int((v - 3.30f) / (4.20f - 3.30f) * 100), 0, 100);
}

String fmtTime(double sec, bool withSec = true) {
  const long s = lround(sec); char b[16];
  if (withSec) snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
  else snprintf(b, sizeof b, "%ld:%02ld", s / 3600, s / 60 % 60);
  return b;
}

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2; const long era = (y >= 0 ? y : y - 399) / 400; const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + long(doe) - 719468;
}
long nowEpoch() {   // from the GPS clock (UTC)
  if (GPS.year == 0) return 0;
  return daysFromCivil(2000 + GPS.year, GPS.month, GPS.day) * 86400L + GPS.hour * 3600L + GPS.minute * 60L + GPS.seconds;
}

// ============================================================ buttons (interrupt-driven, so presses aren't missed
// while the e-paper is busy refreshing)
struct Button {
  uint8_t pin; uint32_t longMs;
  volatile bool down = false, longDone = false, pendingShort = false, pendingLong = false;
  volatile uint32_t downAt = 0, upAt = 0;
};
Button bLap{PIN_BTN_LAP, 100000}, bStart{PIN_BTN_START, 2000}, bPage{PIN_BTN_PAGE, 3000};
void IRAM_ATTR edge(Button& b) {
  const bool pressed = digitalRead(b.pin) == LOW; const uint32_t t = millis();
  if (pressed && !b.down && t - b.upAt > 30) { b.down = true; b.downAt = t; b.longDone = false; }
  else if (!pressed && b.down && t - b.downAt > 30) {
    b.down = false; b.upAt = t;
    if (!b.longDone) { if (t - b.downAt >= b.longMs) b.pendingLong = true; else b.pendingShort = true; }
  }
}
void IRAM_ATTR isrLap() { edge(bLap); }
void IRAM_ATTR isrStart() { edge(bStart); }
void IRAM_ATTR isrPage() { edge(bPage); }
bool redrawNow = false;   // set by any button press so the screen answers straight away
/** 0 = nothing, 1 = short press, 2 = long press */
int takePress(Button& b) {
  int r = 0;
  if (b.down && !b.longDone && millis() - b.downAt >= b.longMs) { b.longDone = true; r = 2; }   // fires while held
  else if (b.pendingLong) { b.pendingLong = false; r = 2; }
  else if (b.pendingShort) { b.pendingShort = false; r = 1; }
  if (r) redrawNow = true;
  return r;
}

// ============================================================ Bluetooth sensors (heart rate, power, cadence)
struct Sensor {
  const char* label; NimBLEUUID svc, chr;
  NimBLEAdvertisedDevice* found = nullptr; NimBLEClient* client = nullptr;
  volatile int value = -1; volatile uint32_t at = 0;
  int get() const { return millis() - at < 5000 ? value : -1; }
  bool connected() const { return client && client->isConnected(); }
};
Sensor sHr{"HR", NimBLEUUID((uint16_t)0x180D), NimBLEUUID((uint16_t)0x2A37)};
Sensor sPw{"PWR", NimBLEUUID((uint16_t)0x1818), NimBLEUUID((uint16_t)0x2A63)};
Sensor sCad{"CAD", NimBLEUUID((uint16_t)0x1816), NimBLEUUID((uint16_t)0x2A5B)};
Sensor* sensors[] = {&sHr, &sPw, &sCad};
volatile int cadence = -1; volatile uint32_t cadenceAt = 0;
struct { int revs = -1, time = 0; uint32_t lastChange = 0; } crank;
int pwBuf[3] = {-1, -1, -1}; uint32_t pwBufAt[3] = {0, 0, 0}; int pwIdx = 0;

void crankData(uint16_t revs, uint16_t time) {
  const uint32_t now = millis();
  if (crank.revs >= 0) {
    const uint16_t dr = uint16_t(revs - crank.revs); const float dt = uint16_t(time - crank.time) / 1024.0f;
    if (dr > 0 && dr < 20 && dt > 0) { cadence = lroundf(dr / dt * 60); cadenceAt = now; crank.lastChange = now; }
    else if (now - crank.lastChange > 3000) { cadence = 0; cadenceAt = now; }
  }
  crank.revs = revs; crank.time = time;
}
int cadenceNow() { return millis() - cadenceAt < 5000 ? cadence : -1; }
int power3s() {
  int sum = 0, n = 0; const uint32_t now = millis();
  for (int i = 0; i < 3; i++) if (pwBuf[i] >= 0 && now - pwBufAt[i] < 3500) { sum += pwBuf[i]; n++; }
  return n ? sum / n : -1;
}
void onHr(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  if (len < 2) return;
  sHr.value = (d[0] & 1) && len >= 3 ? (d[1] | d[2] << 8) : d[1]; sHr.at = millis();
}
void onPower(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  if (len < 4) return;
  const uint16_t f = d[0] | d[1] << 8; const int w = max(0, int(int16_t(d[2] | d[3] << 8)));
  sPw.value = w; sPw.at = millis(); pwBuf[pwIdx] = w; pwBufAt[pwIdx] = millis(); pwIdx = (pwIdx + 1) % 3;
  size_t o = 4; if (f & 1) o += 1; if (f & 4) o += 2; if (f & 16) o += 6;
  if ((f & 32) && !sCad.connected() && len >= o + 4) crankData(d[o] | d[o + 1] << 8, d[o + 2] | d[o + 3] << 8);
}
void onCsc(NimBLERemoteCharacteristic*, uint8_t* d, size_t len, bool) {
  if (len < 1) return;
  size_t o = 1; if (d[0] & 1) o += 6;
  if ((d[0] & 2) && len >= o + 4) crankData(d[o] | d[o + 1] << 8, d[o + 2] | d[o + 3] << 8);
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    for (Sensor* s : sensors)
      if (!s->connected() && !s->found && dev->isAdvertisingService(s->svc)) { s->found = new NimBLEAdvertisedDevice(*dev); return; }
  }
};
uint32_t lastScan = 0;

/** Connect any sensor the scan found. Pairs with the first sensor of each type it sees. */
void serviceSensors() {
  for (Sensor* s : sensors) {
    if (!s->found) continue;
    NimBLEAdvertisedDevice* dev = s->found; s->found = nullptr;
    if (!s->client) s->client = NimBLEDevice::createClient();
    if (s->client->connect(dev)) {
      NimBLERemoteService* svc = s->client->getService(s->svc);
      NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(s->chr) : nullptr;
      if (chr && chr->canNotify()) {
        chr->subscribe(true, s == &sHr ? onHr : s == &sPw ? onPower : onCsc);
        char m[40]; snprintf(m, sizeof m, "%s connected", s->label); flash(m, 3000);
      } else s->client->disconnect();
    }
    delete dev;
  }
  const bool missing = !sHr.connected() || !sPw.connected() || !sCad.connected();
  if (missing && millis() - lastScan > 20000 && !NimBLEDevice::getScan()->isScanning()) {
    lastScan = millis(); NimBLEDevice::getScan()->start(6, nullptr, false);
  }
}

// ============================================================ saving rides (Ride Log JSON)
String jsonEscape(const std::string& s) {
  String o; o.reserve(s.size() + 8);
  for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
  return o;
}

bool saveRide() {
  const long epoch = nowEpoch() + UTC_OFFSET_MIN * 60L;
  const long day = epoch > 0 ? epoch / 86400 : 0;
  long z = day + 719468, era = (z >= 0 ? z : z - 146096) / 146097; unsigned doe = unsigned(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; int y = int(yoe) + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
  char date[12]; snprintf(date, sizeof date, "%04d-%02u-%02u", epoch > 0 ? y : 2000, epoch > 0 ? m : 1, epoch > 0 ? d : 1);
  const int hour = epoch > 0 ? int(epoch % 86400 / 3600) : 12;
  const char* part = hour < 5 ? "Night" : hour < 12 ? "Morning" : hour < 17 ? "Afternoon" : hour < 21 ? "Evening" : "Night";
  char id[24]; snprintf(id, sizeof id, "rlc%lx%04x", epoch, unsigned(esp_random() & 0xffff));

  String j; j.reserve(4096 + ride.track.size() * 3);
  j += "{\"id\":\""; j += id; j += "\",\"date\":\""; j += date; j += "\",\"name\":\""; j += part; j += " ride\",\"type\":\"training\"";
  j += ",\"distance\":" + String(ride.dist / 1000.0, 2) + ",\"time\":" + String(max(1L, lround(ride.moving)));
  j += ",\"elevation\":" + String(lround(ride.elev));
  j += ",\"power\":" + (ride.pwN ? String(ride.pwSum / ride.pwN) : String("null"));
  j += ",\"hr\":" + (ride.hrN ? String(ride.hrSum / ride.hrN) : String("null"));
  j += ",\"cadence\":" + (ride.cadN ? String(ride.cadSum / ride.cadN) : String("null"));
  j += ",\"maxHr\":" + (ride.hrMax ? String(ride.hrMax) : String("null"));
  const double b = ride.best10k();
  j += ",\"best10k\":" + (b > 0 ? String(lround(b)) : String("null"));
  j += ",\"position\":null,\"notes\":\"Recorded with the Ride Log bike computer. Max " + String(ride.maxSpeed * 3.6, 1) + " km/h.\"";
  if (ride.track.size() > 1) { j += ",\"track\":\""; j += jsonEscape(ridelog::encodePolyline(ridelog::simplify(ride.track))); j += "\""; }
  if (!ride.laps.empty()) {
    j += ",\"laps\":[";
    for (size_t i = 0; i < ride.laps.size(); i++) { if (i) j += ","; j += "{\"n\":" + String(ride.laps[i].n) + ",\"d\":" + String(ride.laps[i].d) + ",\"t\":" + String(ride.laps[i].t) + "}"; }
    j += "]";
  }
  j += ",\"updatedAt\":0}";

  LittleFS.mkdir("/rides");
  File f = LittleFS.open(String("/rides/") + id + ".json", "w");
  if (!f) return false;
  const bool ok = f.print(j) == j.length(); f.close();
  snprintf(lastSaved, sizeof lastSaved, "%s %.1f km", date, ride.dist / 1000.0);
  return ok;
}

int rideCount() { int n = 0; File dir = LittleFS.open("/rides"); if (!dir) return 0; for (File f = dir.openNextFile(); f; f = dir.openNextFile()) n++; return n; }

// ============================================================ Wi-Fi transfer
void handleRoot() {
  String h = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Ride Log</title>"
               "<style>body{font:16px system-ui;margin:24px;max-width:520px}a.b,button{display:block;text-align:center;padding:14px;border-radius:12px;"
               "background:#2a78d6;color:#fff;text-decoration:none;font-weight:600;border:0;width:100%;font-size:16px;margin:12px 0}button{background:#c93b3a}"
               "li{margin:6px 0}</style><h1>Ride Log bike computer</h1>");
  h += "<p>" + String(rideCount()) + " ride(s) saved on the device.</p><a class=b href='/rides.json'>Download rides</a>";
  h += F("<p>Then in the Ride Log app: <b>Settings → Import</b> and choose the downloaded file. Rides already in the app are skipped.</p>"
         "<form method=post action='/clear' onsubmit=\"return confirm('Delete all rides from the device? Import them first!')\"><button>Delete rides from device</button></form><ul>");
  File dir = LittleFS.open("/rides");
  if (dir) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) h += "<li>" + String(f.name()) + " (" + String(f.size() / 1024.0, 1) + " KB)</li>";
  h += "</ul>";
  server.send(200, "text/html; charset=utf-8", h);
}
void handleRidesJson() {
  server.sendHeader("Content-Disposition", "attachment; filename=ridelog-device-rides.json");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  bool first = true; File dir = LittleFS.open("/rides");
  if (dir) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!first) server.sendContent(",");
    first = false; server.sendContent(f.readString());
  }
  server.sendContent("]"); server.sendContent("");
}
void handleClear() {
  File dir = LittleFS.open("/rides"); std::vector<String> names;
  if (dir) for (File f = dir.openNextFile(); f; f = dir.openNextFile()) names.push_back(String("/rides/") + f.name());
  for (auto& n : names) LittleFS.remove(n);
  server.sendHeader("Location", "/"); server.send(303);
}
void apName(char* out, size_t n) { snprintf(out, n, "RideLog-%04X", unsigned(ESP.getEfuseMac() & 0xffff)); }
void setWifi(bool on) {
  if (on == wifiOn) return;
  wifiOn = on;
  if (on) {
    char ssid[24]; apName(ssid, sizeof ssid);
    WiFi.mode(WIFI_AP); WiFi.softAP(ssid, AP_PASSWORD);
    server.on("/", handleRoot); server.on("/rides.json", handleRidesJson); server.on("/clear", HTTP_POST, handleClear);
    server.begin(); flash("Wi-Fi transfer on", 3000);
  } else { server.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); flash("Wi-Fi transfer off", 3000); }
}

// ============================================================ drawing (works on both screens via Adafruit_GFX)
void textAt(const GFXfont* font, const String& s, int x, int y, uint8_t align = 0, uint16_t color = INK) {   // align 0=left 1=centre 2=right; y = baseline
  display.setFont(font); display.setTextColor(color);
  int16_t x1, y1; uint16_t w, h; display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(align == 1 ? x - int(w) / 2 - x1 : align == 2 ? x - int(w) - x1 : x, y); display.print(s);
}
/** A labelled box. Big = 18pt value (Sharp), otherwise 12pt (e-paper). */
void field(int x, int y, int w, int h, const char* label, const String& value, const char* unit = "", bool big = SCREEN_W > 300) {
  display.drawRoundRect(x, y, w, h, 6, INK);
  textAt(&FreeSans9pt7b, label, x + 6, y + 14);
  const int unitW = strlen(unit) ? (big ? 30 : 26) : 0;
  textAt(big ? &FreeSansBold18pt7b : &FreeSansBold12pt7b, value, x + w - 6 - unitW, y + h - (big ? 10 : 6), 2);
  if (unitW) textAt(&FreeSans9pt7b, unit, x + w - 4, y + h - (big ? 12 : 7), 2);
}
String orDash(int v) { return v >= 0 ? String(v) : String("--"); }

void drawTopBar() {
  const int bh = SCREEN_H > 200 ? 22 : 17;
  display.fillRect(0, 0, SCREEN_W, bh, INK);
  const char* st = ride.status == Ride::RUNNING ? (ride.autoPaused ? "AUTO-PAUSE" : "REC") : ride.status == Ride::PAUSED ? "PAUSED" : ride.status == Ride::FINISHED ? "SAVED" : "READY";
  textAt(&FreeSans9pt7b, st, 4, bh - 4, 0, PAPER);
  char gps[16]; snprintf(gps, sizeof gps, gpsFix ? "GPS %d" : "GPS ..", sats);
  textAt(&FreeSans9pt7b, gps, SCREEN_W / 2 + 10, bh - 4, 1, PAPER);
  String right; const int bat = batteryPercent();
  if (GPS.year) { char t[8]; long e = nowEpoch() + UTC_OFFSET_MIN * 60L; snprintf(t, sizeof t, "%02ld:%02ld", e % 86400 / 3600, e % 3600 / 60); right = t; }
  if (bat >= 0) right += (right.length() ? "  " : "") + String(bat) + "%";
  textAt(&FreeSans9pt7b, right, SCREEN_W - 4, bh - 4, 2, PAPER);
  if (SCREEN_W > 300) { int x = 110; for (Sensor* s : sensors) if (s->connected()) { textAt(&FreeSans9pt7b, s->label, x, bh - 4, 0, PAPER); x += 38; } }
}

void drawPageContents() {
  const uint32_t now = millis();
  const double kmh = (ride.status == Ride::RUNNING && now - lastFixMs < 6000) ? ride.speed * 3.6 : 0;
  const double avg = ride.avgSpeed();
  const char* trend = (ride.status == Ride::RUNNING && avg > 0 && ride.moving > 30 && ride.speed > 0.5)
                        ? (ride.speed > avg * 1.03 ? "+avg" : ride.speed < avg * .97 ? "-avg" : "=avg") : "";
  const int hr = sHr.get(), pw = power3s(), cad = cadenceNow();
  const int hz = ridelog::hrZone(hr, MAX_HR), pz = ridelog::powerZone(pw, FTP_WATTS);
#if SCREEN_W > 300
  // ---------------- Sharp 400 x 240 ----------------
  if (page == PAGE_RIDE) {
    display.setTextSize(2); textAt(&FreeSansBold24pt7b, String(kmh, 1), 300, 112, 2); display.setTextSize(1);
    textAt(&FreeSans9pt7b, "km/h", 310, 112); textAt(&FreeSans9pt7b, trend, 310, 90);
    field(4, 124, 194, 54, "Distance", String(ride.dist / 1000, 2), "km");
    field(202, 124, 194, 54, "Moving", fmtTime(ride.moving));
    field(4, 182, 194, 54, "Avg speed", String(avg * 3.6, 1), "km/h");
    field(202, 182, 194, 54, "Climbing", String(lround(ride.elev)), "m");
  } else if (page == PAGE_SENSORS) {
    field(4, 28, 194, 100, "Heart rate", orDash(hr), "bpm");
    field(202, 28, 194, 100, "Power 3s", orDash(pw), "W");
    field(4, 132, 128, 104, "HR zone", hz ? "Z" + String(hz) : String("--"));
    field(136, 132, 128, 104, "Pwr zone", pz ? "Z" + String(pz) : String("--"));
    field(268, 132, 128, 104, "Cadence", orDash(cad));
  } else if (page == PAGE_LAPS) {
    field(4, 28, 194, 100, "Lap time", fmtTime(ride.lapTime()));
    field(202, 28, 194, 100, "Lap dist", String(ride.lapDist() / 1000, 2), "km");
    if (!ride.laps.empty()) {
      const auto& l = ride.laps.back();
      field(4, 132, 194, 104, ("Lap " + String(l.n)).c_str(), fmtTime(l.t));
      field(202, 132, 194, 104, "Lap km/h", String(l.t ? l.d / double(l.t) * 3.6 : 0, 1));
    } else textAt(&FreeSans9pt7b, "Press LAP to start a new lap", 200, 190, 1);
  }
#else
  // ---------------- e-paper 296 x 128 ----------------
  if (page == PAGE_RIDE) {
    display.setTextSize(2); textAt(&FreeSansBold18pt7b, String(kmh, kmh >= 100 ? 0 : 1), 170, 96, 2); display.setTextSize(1);   // ~50 px tall digits
    textAt(&FreeSans9pt7b, "km/h", 8, 120); textAt(&FreeSans9pt7b, trend, 172, 120, 2);
    field(178, 19, 116, 35, "Dist km", String(ride.dist / 1000, 2));
    field(178, 56, 116, 35, "Moving", fmtTime(ride.moving));
    field(178, 93, 116, 35, "Avg km/h", String(avg * 3.6, 1));
  } else if (page == PAGE_SENSORS) {
    field(2, 19, 145, 53, "Heart rate", orDash(hr), "bpm");
    field(149, 19, 145, 53, "Power 3s", orDash(pw), "W");
    field(2, 74, 96, 53, "HR zone", hz ? "Z" + String(hz) : String("--"));
    field(100, 74, 96, 53, "Pwr zone", pz ? "Z" + String(pz) : String("--"));
    field(198, 74, 96, 53, "Cadence", orDash(cad));
  } else if (page == PAGE_LAPS) {
    field(2, 19, 145, 53, "Lap time", fmtTime(ride.lapTime()));
    field(149, 19, 145, 53, "Lap dist", String(ride.lapDist() / 1000, 2), "km");
    if (!ride.laps.empty()) {
      const auto& l = ride.laps.back();
      field(2, 74, 145, 53, ("Lap " + String(l.n)).c_str(), fmtTime(l.t));
      field(149, 74, 145, 53, "Lap km/h", String(l.t ? l.d / double(l.t) * 3.6 : 0, 1));
    } else textAt(&FreeSans9pt7b, "Press LAP for a new lap", 148, 104, 1);
  }
#endif
  if (page == PAGE_DEVICE) {
    const int lh = SCREEN_H > 200 ? 22 : 17; int y = SCREEN_H > 200 ? 52 : 36;
    char b[72];
    textAt(&FreeSansBold12pt7b, "Device", 6, y); y += lh + 2;
    snprintf(b, sizeof b, "Rides saved: %d   Climb: %ld m", rideCount(), lround(ride.elev)); textAt(&FreeSans9pt7b, b, 6, y); y += lh;
    snprintf(b, sizeof b, "Last: %s", lastSaved[0] ? lastSaved : "-"); textAt(&FreeSans9pt7b, b, 6, y); y += lh;
    snprintf(b, sizeof b, "GPS %s  Baro %s", gpsFix ? "fix" : "searching", baroOk ? "ok" : "missing"); textAt(&FreeSans9pt7b, b, 6, y); y += lh;
    if (wifiOn) {
      char ssid[24]; apName(ssid, sizeof ssid);
      snprintf(b, sizeof b, "Wi-Fi %s / %s", ssid, AP_PASSWORD); textAt(&FreeSans9pt7b, b, 6, y); y += lh;
      textAt(&FreeSans9pt7b, "open http://192.168.4.1", 6, y);
    } else textAt(&FreeSans9pt7b, "Hold START: send rides to phone", 6, y);
  }
  if (millis() < flashUntil) {   // message banner
    const int bw = SCREEN_W - 24, bh = SCREEN_H > 200 ? 56 : 40, by = (SCREEN_H - bh) / 2 + 8;
    display.fillRoundRect(12, by, bw, bh, 8, INK);
    textAt(SCREEN_H > 200 ? &FreeSansBold12pt7b : &FreeSans9pt7b, flashMsg, SCREEN_W / 2, by + bh / 2 + 6, 1, PAPER);
  }
}

#if defined(HW_V1_ALIEXPRESS)
uint32_t lastFullRefresh = 0; bool forceFull = true;
void drawPage() {
  // e-paper: fast partial updates; a full refresh every 3 minutes clears the faint "ghost" of old digits
  const bool full = forceFull || millis() - lastFullRefresh > 180000;
  if (full) { display.setFullWindow(); lastFullRefresh = millis(); forceFull = false; }
  else display.setPartialWindow(0, 0, display.width(), display.height());
  display.firstPage();
  do { display.fillScreen(PAPER); drawTopBar(); drawPageContents(); } while (display.nextPage());
}
#else
void drawPage() { display.clearDisplay(); drawTopBar(); drawPageContents(); display.refresh(); }
#endif

void splash(const char* line) {
#if defined(HW_V1_ALIEXPRESS)
  display.setFullWindow(); display.firstPage();
  do { display.fillScreen(PAPER); textAt(&FreeSansBold18pt7b, "Ride Log", SCREEN_W / 2, 60, 1); textAt(&FreeSans9pt7b, line, SCREEN_W / 2, 92, 1); } while (display.nextPage());
#else
  display.clearDisplay(); textAt(&FreeSansBold18pt7b, "Ride Log", SCREEN_W / 2, 110, 1); textAt(&FreeSans9pt7b, line, SCREEN_W / 2, 145, 1); display.refresh();
#endif
}

#if defined(HW_ADAFRUIT)
void goToSleep() {
  splash("Sleeping - press PAGE to wake");
  setWifi(false); GPS.standby(); NimBLEDevice::deinit(true);
  while (digitalRead(PIN_BTN_PAGE) == LOW) delay(10);       // wait for release
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_PAGE, 0);
  esp_deep_sleep_start();
}
#endif

// ============================================================ button actions
void handleButtons() {
  const uint32_t now = millis();
  switch (takePress(bStart)) {
    case 1:
      if (ride.status == Ride::IDLE || ride.status == Ride::FINISHED) {
        ride.autoPauseOn = AUTO_PAUSE; ride.autoLapKm = AUTO_LAP_KM; ride.start(now);
        flash(gpsFix ? "Ride started" : "Started - waiting for GPS");
      } else if (ride.status == Ride::RUNNING) { ride.pause(now); flash("Paused - hold START to finish"); }
      else if (ride.status == Ride::PAUSED) { ride.resume(now); flash("Resumed", 2000); }
      break;
    case 2:
      if (ride.status == Ride::RUNNING || ride.status == Ride::PAUSED) {
        ride.finish(now);
        flash(ride.dist < 100 ? "Too short - not saved" : saveRide() ? "Ride saved" : "SAVE FAILED", 5000);
        page = PAGE_RIDE;
      } else if (page == PAGE_DEVICE) setWifi(!wifiOn);
      break;
  }
  if (takePress(bLap) == 1) {
    if (ride.lap()) { const auto& l = ride.laps.back(); char m[48]; snprintf(m, sizeof m, "Lap %u  %s", unsigned(l.n), fmtTime(l.t).c_str()); flash(m); }
  }
  switch (takePress(bPage)) {
    case 1: page = (page + 1) % PAGE_COUNT; break;
    case 2:
#if defined(HW_V1_ALIEXPRESS)
      forceFull = true;                                       // v1 turns off with the power switch
#else
      if (ride.status == Ride::RUNNING || ride.status == Ride::PAUSED) flash("Finish the ride first");
      else goToSleep();
#endif
      break;
  }
}

// ============================================================ setup / loop
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_LAP, INPUT_PULLUP); pinMode(PIN_BTN_START, INPUT_PULLUP); pinMode(PIN_BTN_PAGE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_LAP), isrLap, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_START), isrStart, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_PAGE), isrPage, CHANGE);
  analogReadResolution(12);

#if defined(HW_V1_ALIEXPRESS)
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);      // no MISO: D9 is the e-paper BUSY pin
  display.init(0, true, 2, false);
  display.setRotation(1);                                   // landscape, 296 x 128
#else
  SPI.begin(PIN_DISP_SCK, -1, PIN_DISP_MOSI);
  display.begin(); display.setRotation(0);
#endif
  splash("Starting...");

  if (!LittleFS.begin(true)) Serial.println("LittleFS failed");
  Wire.begin(); Wire.setClock(400000);
#if defined(HW_V1_ALIEXPRESS)
  Serial1.setRxBufferSize(2048);                            // the e-paper refresh blocks for ~0.6 s; keep GPS data meanwhile
  GPS.begin(GPS_BAUD);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, -1);      // receive only: the GPS sends NMEA at 1 Hz by default
  baroOk = baro.begin(BARO_I2C_ADDR);
  if (baroOk) baro.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X2, Adafruit_BMP280::SAMPLING_X16,
                               Adafruit_BMP280::FILTER_X16, Adafruit_BMP280::STANDBY_MS_63);
#else
  GPS.begin(GPS_I2C_ADDR);
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
  GPS.wakeup();
  baroOk = baro.begin_I2C(BARO_I2C_ADDR, &Wire);
  if (baroOk) {
    baro.setTemperatureOversampling(BMP3_OVERSAMPLING_2X);
    baro.setPressureOversampling(BMP3_OVERSAMPLING_8X);
    baro.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    baro.setOutputDataRate(BMP3_ODR_25_HZ);
  }
#endif
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
  scan->setActiveScan(true); scan->setInterval(100); scan->setWindow(80);
  scan->start(6, nullptr, false); lastScan = millis();
}

uint32_t lastDraw = 0, lastSecond = 0, lastBaro = 0;
void loop() {
  // GPS: read everything waiting, handle each new sentence
  for (int i = 0; i < 512; i++) GPS.read();
  if (GPS.newNMEAreceived() && GPS.parse(GPS.lastNMEA())) {
    gpsFix = GPS.fix; sats = GPS.satellites; gpsAccuracy = GPS.HDOP > 0 ? GPS.HDOP * 5.0 : 99;   // rough: HDOP x 5 m
    if (gpsFix && strstr(GPS.lastNMEA(), "RMC")) {
      lastFixMs = millis();
      if (ride.onFix(GPS.latitudeDegrees, GPS.longitudeDegrees, GPS.speed * 0.514444, gpsAccuracy, lastFixMs)) {
        const auto& l = ride.laps.back(); char m[48]; snprintf(m, sizeof m, "Lap %u  %s", unsigned(l.n), fmtTime(l.t).c_str()); flash(m);
      }
    }
  }
  const uint32_t now = millis();
  if (baroOk && now - lastBaro >= 250) { lastBaro = now; const float alt = baro.readAltitude(1013.25); if (!isnan(alt)) ride.onAltitude(alt); }
  if (now - lastSecond >= 1000) { lastSecond = now; ride.onSecond(sHr.get(), power3s(), cadenceNow()); }
  handleButtons();
  serviceSensors();
  if (wifiOn) server.handleClient();
#if defined(HW_V1_ALIEXPRESS)
  const uint32_t every = ride.status == Ride::RUNNING ? 1000 : 4000;   // e-paper: once a second while riding
#else
  const uint32_t every = 500;                                          // Sharp: also needs refreshing at least once a second
#endif
  if (now - lastDraw >= every || redrawNow) { lastDraw = now; redrawNow = false; drawPage(); }
  delay(5);
}
