// Ride Log bike computer: ride maths (pure C++, no Arduino calls, so it can be
// unit-tested on a computer). Mirrors the logic of the Ride Log app's recorder:
// GPS filtering, moving time with auto-pause, laps, climbing from the barometer,
// fastest 10 km split, and the route stored as a Google-encoded polyline.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace ridelog {

constexpr double MIN_MOVING = 1.0;     // m/s (3.6 km/h): slower counts as stopped
constexpr double MAX_ACCURACY = 35.0;  // m: ignore fixes worse than this
constexpr double MAX_JUMP = 28.0;      // m/s (~100 km/h): faster = GPS glitch
constexpr double STOP_DELAY = 3.0;     // s stopped before "auto-paused"
constexpr double TRACK_STEP = 8.0;     // m between stored route points
constexpr size_t MAX_TRACK = 40000;    // ~320 km of route at 8 m spacing

inline double haversine(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0, rad = M_PI / 180.0;
  const double dLat = (lat2 - lat1) * rad, dLon = (lon2 - lon1) * rad;
  const double h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1 * rad) * std::cos(lat2 * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * R * std::asin(std::sqrt(h));
}

struct Lap { uint32_t n, d, t; };          // number, metres, moving seconds
struct Pt { int32_t lat, lon; };           // degrees * 1e5 (about 1 m)

// ---------- route storage: Douglas-Peucker simplify + polyline encoding ----------
inline std::vector<Pt> simplify(const std::vector<Pt>& p, double tolM = 4.0) {
  if (p.size() < 3) return p;
  const double kx = 111320.0 * std::cos(p[0].lat / 1e5 * M_PI / 180.0) / 1e5, ky = 110540.0 / 1e5;
  std::vector<uint8_t> keep(p.size(), 0); keep.front() = keep.back() = 1;
  std::vector<std::pair<size_t, size_t>> stack{{0, p.size() - 1}};
  while (!stack.empty()) {
    auto [a, b] = stack.back(); stack.pop_back();
    const double ax = p[a].lon * kx, ay = p[a].lat * ky, dx = p[b].lon * kx - ax, dy = p[b].lat * ky - ay;
    const double len2 = dx * dx + dy * dy;
    double maxD = 0; size_t idx = 0;
    for (size_t i = a + 1; i < b; i++) {
      const double px = p[i].lon * kx, py = p[i].lat * ky;
      double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
      t = std::max(0.0, std::min(1.0, t));
      const double ex = ax + t * dx - px, ey = ay + t * dy - py, d = ex * ex + ey * ey;
      if (d > maxD) { maxD = d; idx = i; }
    }
    if (maxD > tolM * tolM) { keep[idx] = 1; stack.push_back({a, idx}); stack.push_back({idx, b}); }
  }
  std::vector<Pt> out;
  for (size_t i = 0; i < p.size(); i++) if (keep[i]) out.push_back(p[i]);
  return out;
}

inline void encodeValue(int32_t v, std::string& out) {
  uint32_t u = v < 0 ? ~(static_cast<uint32_t>(v) << 1) : (static_cast<uint32_t>(v) << 1);
  while (u >= 0x20) { out += static_cast<char>((0x20 | (u & 0x1f)) + 63); u >>= 5; }
  out += static_cast<char>(u + 63);
}
inline std::string encodePolyline(const std::vector<Pt>& p) {
  std::string out; int32_t pa = 0, po = 0;
  for (const auto& q : p) { encodeValue(q.lat - pa, out); encodeValue(q.lon - po, out); pa = q.lat; po = q.lon; }
  return out;
}

/** Fastest moving time over `meters`, from (cumulative metres, moving seconds) samples. */
inline double bestSplit(const std::vector<std::pair<float, float>>& pts, double meters) {
  double best = -1; size_t i = 0;
  for (size_t j = 0; j < pts.size(); j++) {
    while (i + 1 < j && pts[j].first - pts[i + 1].first >= meters) i++;
    const double dd = pts[j].first - pts[i].first;
    if (dd >= meters) {
      const double t = (pts[j].second - pts[i].second) * meters / dd;
      if (t > 0 && (best < 0 || t < best)) best = t;
    }
  }
  return best;
}

// ---------- training zones ----------
inline int hrZone(int hr, int maxHr) {
  if (hr <= 0 || maxHr <= 0) return 0;
  const double p = double(hr) / maxHr;
  return p < .6 ? 1 : p < .7 ? 2 : p < .8 ? 3 : p < .9 ? 4 : 5;
}
inline int powerZone(int w, int ftp) {
  if (w < 0 || ftp <= 0) return 0;
  const double p = double(w) / ftp;
  return p < .56 ? 1 : p < .76 ? 2 : p < .91 ? 3 : p < 1.06 ? 4 : p < 1.21 ? 5 : p < 1.51 ? 6 : 7;
}

// ---------- the ride being recorded ----------
class Ride {
 public:
  enum Status { IDLE, RUNNING, PAUSED, FINISHED };
  Status status = IDLE;
  bool autoPauseOn = true;
  double autoLapKm = 0;               // 0 = off

  double dist = 0, moving = 0, elev = 0, speed = 0, maxSpeed = 0;
  bool autoPaused = false;
  std::vector<Lap> laps;
  std::vector<Pt> track;
  std::vector<std::pair<float, float>> splits{{0.f, 0.f}};   // every 20 m, for the fastest 10 km
  long hrSum = 0, hrN = 0, pwSum = 0, pwN = 0, cadSum = 0, cadN = 0;
  int hrMax = 0, pwMax = 0;
  uint32_t startedMs = 0, elapsedMs = 0;

  void start(uint32_t nowMs) { *this = Ride{autoPauseOn, autoLapKm}; status = RUNNING; startedMs = runningSince = nowMs; }
  void pause(uint32_t nowMs) {
    if (status != RUNNING) return;
    elapsedMs += nowMs - runningSince; status = PAUSED; hasLast = false; speed = 0; autoPaused = false;
  }
  void resume(uint32_t nowMs) { if (status == PAUSED) { status = RUNNING; runningSince = nowMs; hasLast = false; stillSinceMs = 0; } }
  void finish(uint32_t nowMs) {
    if (status == RUNNING) elapsedMs += nowMs - runningSince;
    if (!laps.empty() && dist - lapD > 50) closeLap();
    status = FINISHED;
  }
  uint32_t elapsed(uint32_t nowMs) const { return elapsedMs + (status == RUNNING ? nowMs - runningSince : 0); }
  double lapDist() const { return dist - lapD; }
  double lapTime() const { return moving - lapT; }

  /** Manual lap. Returns false if the lap would be empty. */
  bool lap() {
    if (status != RUNNING || (lapDist() < 20 && lapTime() < 10)) return false;
    closeLap(); return true;
  }

  /** A GPS fix. speedMs < 0 means "unknown". tMs is the fix time in ms. Returns true if an auto-lap happened. */
  bool onFix(double lat, double lon, double speedMs, double accuracyM, uint32_t tMs) {
    if (status != RUNNING || accuracyM > MAX_ACCURACY) return false;
    if (!hasLast) {
      hasLast = true; lastLat = lat; lastLon = lon; lastT = tMs;
      if (track.empty()) track.push_back(toPt(lat, lon));
      return false;
    }
    const double dt = (tMs - lastT) / 1000.0;
    if (dt <= 0) return false;
    const double d = haversine(lastLat, lastLon, lat, lon), vGps = d / dt;
    if (vGps > MAX_JUMP) { lastLat = lat; lastLon = lon; lastT = tMs; return false; }   // glitch
    const double v = speedMs >= 0 ? speedMs : vGps;
    if (v < MIN_MOVING || d < std::min(3.0, accuracyM * 0.3)) {                          // stopped / jitter
      speed = 0;
      if (!stillSinceMs) stillSinceMs = tMs ? tMs : 1;
      if (autoPauseOn) { if ((tMs - stillSinceMs) / 1000.0 >= STOP_DELAY) autoPaused = true; }
      else if (dt <= 15) moving += dt;
      lastT = tMs;
      return false;
    }
    stillSinceMs = 0; autoPaused = false;
    dist += d;
    if (dt <= 15) moving += dt;
    speed = speed > 0 ? 0.6 * v + 0.4 * speed : v;
    maxSpeed = std::max(maxSpeed, speed);
    if (dist - lastSplitD >= 20) { splits.push_back({float(dist), float(moving)}); lastSplitD = dist; }
    if (track.size() < MAX_TRACK) {
      const Pt& b = track.back();
      if (haversine(b.lat / 1e5, b.lon / 1e5, lat, lon) >= TRACK_STEP) track.push_back(toPt(lat, lon));
    }
    lastLat = lat; lastLon = lon; lastT = tMs;
    if (autoLapKm > 0 && dist >= nextAutoLap * autoLapKm * 1000) {
      nextAutoLap = uint32_t(dist / (autoLapKm * 1000)) + 1;
      closeLap(); return true;
    }
    return false;
  }

  /** Barometric altitude (m). Smoothed, counts rises of 2 m or more. */
  void onAltitude(double altM) {
    if (status != RUNNING) return;
    altSm = altInit ? altSm + 0.15 * (altM - altSm) : altM;
    if (!altInit) { altInit = true; altBase = altSm; return; }
    if (altSm < altBase) altBase = altSm;
    else if (altSm - altBase >= 2.0) { elev += altSm - altBase; altBase = altSm; }
  }

  /** Once a second while recording: sensor values (< 0 = no reading). */
  void onSecond(int hr, int power, int cadence) {
    if (status != RUNNING || autoPaused) return;
    if (hr > 0) { hrSum += hr; hrN++; hrMax = std::max(hrMax, hr); }
    if (power >= 0) { pwSum += power; pwN++; pwMax = std::max(pwMax, power); }
    if (cadence > 0) { cadSum += cadence; cadN++; }
  }

  double avgSpeed() const { return moving > 0 ? dist / moving : 0; }
  double best10k() const { return bestSplit(splits, 10000); }

 private:
  Ride(bool ap, double al) : autoPauseOn(ap), autoLapKm(al) {}
 public:
  Ride() = default;
 private:
  static Pt toPt(double lat, double lon) { return {int32_t(std::lround(lat * 1e5)), int32_t(std::lround(lon * 1e5))}; }
  void closeLap() {
    laps.push_back({uint32_t(laps.size() + 1), uint32_t(std::lround(dist - lapD)), uint32_t(std::lround(moving - lapT))});
    lapD = dist; lapT = moving;
  }
  bool hasLast = false; double lastLat = 0, lastLon = 0; uint32_t lastT = 0, stillSinceMs = 0, runningSince = 0;
  double lapD = 0, lapT = 0, lastSplitD = 0; uint32_t nextAutoLap = 1;
  bool altInit = false; double altSm = 0, altBase = 0;
};

}  // namespace ridelog
