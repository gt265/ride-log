/*
  Ride Log bike computer: firmware for the prototype in hardware/README.md
  Board:  Seeed Studio XIAO ESP32-S3  (Arduino IDE: "XIAO_ESP32S3", PSRAM: "OPI PSRAM")
  Needs these libraries (Arduino IDE → Library Manager):
    Adafruit SHARP Memory Display, Adafruit GFX Library, Adafruit GPS Library,
    Adafruit BMP3XX Library, NimBLE-Arduino (version 1.4.x)

  Buttons
    START  short: start / pause / resume        long (2 s): finish and save the ride
    LAP    short: new lap
    PAGE   short: next screen                   long (3 s): sleep (wake with PAGE)
    On the "Device" screen with no ride running, hold START to switch Wi-Fi transfer on/off.

  Rides are saved in the board's flash as Ride Log JSON. To copy them to the app:
  turn on Wi-Fi transfer, join the "RideLog-xxxx" Wi-Fi network on your phone, open
  http://192.168.4.1 and tap Download. Then in Ride Log: Settings → Import → pick the file.
*/
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include <Adafruit_GPS.h>
#include <Adafruit_BMP3XX.h>
#include <NimBLEDevice.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "config.h"
#include "ride_logic.h"

#ifndef UTC_OFFSET_MIN
#define UTC_OFFSET_MIN 0      // your time zone in minutes (e.g. 60 for CET), used for ride dates/names
#endif

using ridelog::Ride;
static const uint8_t INK = 0, PAPER = 1;           // Sharp memory LCD: 0 = black, 1 = white

Adafruit_SharpMem display(&SPI, PIN_DISP_CS, 400, 240, 2000000);
Adafruit_GPS GPS(&Wire);
Adafruit_BMP3XX baro;
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

int batteryPercent() {
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
        char m[40]; snprintf(m, sizeof m, "%s sensor connected", s->label); flash(m, 3000);
      } else s->client->disconnect();
    }
    delete dev;
  }
  // keep looking for missing sensors (also reconnects after a drop)
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
  // civil date from days (inverse of daysFromCivil)
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
void setWifi(bool on) {
  if (on == wifiOn) return;
  wifiOn = on;
  if (on) {
    char ssid[24]; snprintf(ssid, sizeof ssid, "RideLog-%04X", unsigned(ESP.getEfuseMac() & 0xffff));
    WiFi.mode(WIFI_AP); WiFi.softAP(ssid, AP_PASSWORD);
    server.on("/", handleRoot); server.on("/rides.json", handleRidesJson); server.on("/clear", HTTP_POST, handleClear);
    server.begin(); flash("Wi-Fi transfer on", 3000);
  } else { server.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); flash("Wi-Fi transfer off", 3000); }
}

// ============================================================ drawing
void textAt(const GFXfont* font, const String& s, int x, int y, uint8_t align = 0, uint8_t color = INK) {   // align 0=left 1=centre 2=right; y = baseline
  display.setFont(font); display.setTextColor(color);
  int16_t x1, y1; uint16_t w, h; display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(align == 1 ? x - int(w) / 2 - x1 : align == 2 ? x - int(w) - x1 : x, y); display.print(s);
}
void field(int x, int y, int w, int h, const char* label, const String& value, const char* unit = "") {
  display.drawRoundRect(x, y, w, h, 8, INK);
  textAt(&FreeSans9pt7b, label, x + 8, y + 17);
  textAt(&FreeSansBold18pt7b, value, x + w - 8 - (strlen(unit) ? 30 : 0), y + h - 10, 2);
  if (strlen(unit)) textAt(&FreeSans9pt7b, unit, x + w - 6, y + h - 12, 2);
}
String orDash(int v) { return v >= 0 ? String(v) : String("--"); }

void drawTopBar() {
  display.fillRect(0, 0, 400, 22, INK);
  const char* st = ride.status == Ride::RUNNING ? (ride.autoPaused ? "AUTO-PAUSED" : "REC") : ride.status == Ride::PAUSED ? "PAUSED" : ride.status == Ride::FINISHED ? "SAVED" : "READY";
  textAt(&FreeSans9pt7b, st, 6, 16, 0, PAPER);
  char gps[24]; snprintf(gps, sizeof gps, gpsFix ? "GPS %d" : "GPS ...", sats);
  textAt(&FreeSans9pt7b, gps, 200, 16, 1, PAPER);
  String right = String(batteryPercent()) + "%";
  if (GPS.year) { char t[8]; long e = nowEpoch() + UTC_OFFSET_MIN * 60L; snprintf(t, sizeof t, "%02ld:%02ld", e % 86400 / 3600, e % 3600 / 60); right = String(t) + "  " + right; }
  textAt(&FreeSans9pt7b, right, 394, 16, 2, PAPER);
  int x = 92; for (Sensor* s : sensors) if (s->connected()) { textAt(&FreeSans9pt7b, s->label, x, 16, 0, PAPER); x += 38; }
}

void drawPage() {
  display.clearDisplay();
  drawTopBar();
  const uint32_t now = millis();
  if (page == PAGE_RIDE) {
    const double kmh = (ride.status == Ride::RUNNING && now - lastFixMs < 6000) ? ride.speed * 3.6 : 0;
    display.setTextSize(2); textAt(&FreeSansBold24pt7b, String(kmh, 1), 300, 112, 2); display.setTextSize(1);
    textAt(&FreeSans9pt7b, "km/h", 310, 112);
    const double avg = ride.avgSpeed();
    if (ride.status == Ride::RUNNING && avg > 0 && ride.moving > 30 && ride.speed > 0.5)
      textAt(&FreeSans9pt7b, ride.speed > avg * 1.03 ? "+ avg" : ride.speed < avg * .97 ? "- avg" : "= avg", 310, 90);
    field(4, 124, 194, 54, "Distance", String(ride.dist / 1000, 2), "km");
    field(202, 124, 194, 54, "Moving", fmtTime(ride.moving));
    field(4, 182, 194, 54, "Avg speed", String(avg * 3.6, 1), "km/h");
    field(202, 182, 194, 54, "Climbing", String(lround(ride.elev)), "m");
  } else if (page == PAGE_SENSORS) {
    const int hr = sHr.get(), pw = power3s(), cad = cadenceNow();
    const int hz = ridelog::hrZone(hr, MAX_HR), pz = ridelog::powerZone(pw, FTP_WATTS);
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
  } else {
    textAt(&FreeSansBold12pt7b, "Device", 12, 52);
    char b[64];
    snprintf(b, sizeof b, "Rides saved: %d", rideCount()); textAt(&FreeSans9pt7b, b, 12, 82);
    snprintf(b, sizeof b, "Last: %s", lastSaved[0] ? lastSaved : "-"); textAt(&FreeSans9pt7b, b, 12, 104);
    snprintf(b, sizeof b, "Battery %d%%   GPS %s   Baro %s", batteryPercent(), gpsFix ? "fix" : "searching", baroOk ? "ok" : "missing"); textAt(&FreeSans9pt7b, b, 12, 126);
    if (wifiOn) {
      char ssid[24]; snprintf(ssid, sizeof ssid, "RideLog-%04X", unsigned(ESP.getEfuseMac() & 0xffff));
      textAt(&FreeSansBold12pt7b, "Wi-Fi transfer ON", 12, 160);
      snprintf(b, sizeof b, "Join %s  (password %s)", ssid, AP_PASSWORD); textAt(&FreeSans9pt7b, b, 12, 184);
      textAt(&FreeSans9pt7b, "then open http://192.168.4.1", 12, 206);
    } else textAt(&FreeSans9pt7b, "Hold START to send rides to your phone", 12, 170);
  }
  if (now < flashUntil) {   // message banner
    display.fillRoundRect(20, 92, 360, 56, 10, INK);
    textAt(&FreeSansBold12pt7b, flashMsg, 200, 128, 1, PAPER);
  }
  display.refresh();
}

// ============================================================ buttons
struct Button { uint8_t pin; bool down = false; uint32_t since = 0; bool longDone = false; };
Button bLap{PIN_BTN_LAP}, bStart{PIN_BTN_START}, bPage{PIN_BTN_PAGE};
/** 0 = nothing, 1 = short press (on release), 2 = long press (fires while held) */
int poll(Button& b, uint32_t longMs) {
  const bool pressed = digitalRead(b.pin) == LOW; const uint32_t now = millis();
  if (pressed && !b.down) { b.down = true; b.since = now; b.longDone = false; return 0; }
  if (pressed && b.down && !b.longDone && now - b.since >= longMs) { b.longDone = true; return 2; }
  if (!pressed && b.down) { b.down = false; if (!b.longDone && now - b.since >= 30) return 1; }
  return 0;
}

void goToSleep() {
  display.clearDisplay();
  textAt(&FreeSansBold18pt7b, "Ride Log", 200, 110, 1);
  textAt(&FreeSans9pt7b, "Sleeping - press PAGE to wake", 200, 145, 1);
  display.refresh();
  setWifi(false); GPS.standby(); NimBLEDevice::deinit(true);
  while (digitalRead(PIN_BTN_PAGE) == LOW) delay(10);       // wait for release
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_PAGE, 0);
  esp_deep_sleep_start();
}

void handleButtons() {
  const uint32_t now = millis();
  switch (poll(bStart, 2000)) {
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
  if (poll(bLap, 100000) == 1) {
    if (ride.lap()) { const auto& l = ride.laps.back(); char m[48]; snprintf(m, sizeof m, "Lap %u  %s", unsigned(l.n), fmtTime(l.t).c_str()); flash(m); }
  }
  switch (poll(bPage, 3000)) {
    case 1: page = (page + 1) % PAGE_COUNT; break;
    case 2:
      if (ride.status == Ride::RUNNING || ride.status == Ride::PAUSED) flash("Finish the ride first");
      else goToSleep();
      break;
  }
}

// ============================================================ setup / loop
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_LAP, INPUT_PULLUP); pinMode(PIN_BTN_START, INPUT_PULLUP); pinMode(PIN_BTN_PAGE, INPUT_PULLUP);
  analogReadResolution(12);
  SPI.begin(PIN_DISP_SCK, -1, PIN_DISP_MOSI);
  display.begin(); display.setRotation(0); display.clearDisplay();
  textAt(&FreeSansBold18pt7b, "Ride Log", 200, 110, 1); textAt(&FreeSans9pt7b, "Starting...", 200, 145, 1); display.refresh();

  if (!LittleFS.begin(true)) Serial.println("LittleFS failed");
  Wire.begin(); Wire.setClock(400000);
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
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
  scan->setActiveScan(true); scan->setInterval(100); scan->setWindow(80);
  scan->start(6, nullptr, false); lastScan = millis();
}

uint32_t lastDraw = 0, lastSecond = 0, lastBaro = 0;
void loop() {
  // GPS: read everything waiting on I2C, handle each new sentence
  for (int i = 0; i < 128; i++) GPS.read();
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
  if (now - lastDraw >= 500) { lastDraw = now; drawPage(); }   // the Sharp display also needs refreshing at least once a second
  delay(5);
}
