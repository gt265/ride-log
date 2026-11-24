// Host test for ride_logic.h:  g++ -std=c++17 -O1 ride_logic_test.cpp -o t && ./t
#include "../RideLogComputer/ride_logic.h"
#include <cstdio>
#include <cstdlib>
using namespace ridelog;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
  // 30 km/h north for 600 s, stopped 60 s, 36 km/h for 600 s; one GPS glitch; climbing 50 m in the second half
  Ride r; r.autoLapKm = 1; r.start(0);
  double lat = 52.0; int autoLaps = 0;
  for (int i = 0; i < 1260; i++) {
    const double v = i < 600 ? 8.333 : i < 660 ? 0 : 10;
    lat += v / 111320.0;
    const double jitter = v == 0 ? ((i * 7919) % 100 - 50) * 2e-8 : 0;
    const double lon = 4.0 + (i == 300 ? 0.01 : 0);                     // glitch at i=300
    if (r.onFix(lat + jitter, lon, v == 0 ? 0 : -1, 6, uint32_t(i) * 1000)) autoLaps++;
    r.onAltitude(10 + (i > 660 ? (i - 660) / 12.0 : 0));
    r.onSecond(150, 220, 90);
    if (i == 420) r.lap();
  }
  r.finish(1260000);
  CHECK(std::fabs(r.dist - 11000) < 120, "distance %.0f m (expect ~11000, glitch ignored)", r.dist);
  CHECK(std::fabs(r.moving - 1200) < 5, "moving time %.0f s (60 s stop excluded)", r.moving);
  CHECK(std::fabs(r.elev - 50) < 6, "climbing %.1f m (expect ~50)", r.elev);
  CHECK(std::fabs(r.best10k() - 1080) < 15, "fastest 10 km %.0f s (expect ~1080)", r.best10k());
  CHECK(autoLaps == 10, "auto-laps %d (expect 10)", autoLaps);
  CHECK(r.laps.size() == 12, "total laps %zu (10 auto + 1 manual + final)", r.laps.size());
  CHECK(r.hrN > 1100 && r.hrSum / r.hrN == 150, "avg HR %ld over %ld s (paused seconds skipped)", r.hrSum / r.hrN, r.hrN);
  CHECK(r.track.size() > 1000, "route points %zu", r.track.size());
  const auto simp = simplify(r.track);
  CHECK(simp.size() < 20, "simplified route %zu points (a straight line)", simp.size());
  // auto-pause off: stopped time counts
  Ride q; q.autoPauseOn = false; q.start(0); lat = 52;
  for (int i = 0; i < 120; i++) { const double v = i < 60 ? 8 : 0; lat += v / 111320.0; q.onFix(lat, 4, v == 0 ? 0 : -1, 6, i * 1000); }
  CHECK(std::fabs(q.moving - 119) < 2, "auto-pause off: moving %.0f s includes the stop", q.moving);
  CHECK(hrZone(150, 190) == 3 && powerZone(220, 250) == 3 && powerZone(400, 250) == 7, "zones");
  // polyline encoding: Google's documented example
  const std::vector<Pt> g{{3850000, -12020000}, {4070000, -12095000}, {4325200, -12645300}};
  CHECK(encodePolyline(g) == "_p~iF~ps|U_ulLnnqC_mqNvxq`@", "polyline matches Google's reference example");
  std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
  return fails ? 1 : 0;
}
