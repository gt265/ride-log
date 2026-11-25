// Replays a text trace through ride_logic.h and prints the results (used to check the JS port).
#include "../RideLogComputer/ride_logic.h"
#include <cstdio>
#include <cstring>
using namespace ridelog;
int main() {
  Ride r; char op[4]; double a, b, c, d, e;
  while (scanf("%3s", op) == 1) {
    if (!strcmp(op, "I")) { (void)scanf("%lf %lf", &a, &b); r.autoPauseOn = a != 0; r.autoLapKm = b; }
    else if (!strcmp(op, "B")) { (void)scanf("%lf", &a); r.start(uint32_t(a)); }
    else if (!strcmp(op, "F")) { (void)scanf("%lf %lf %lf %lf %lf", &a, &b, &c, &d, &e); r.onFix(b, c, d, e, uint32_t(a)); }
    else if (!strcmp(op, "A")) { (void)scanf("%lf", &a); r.onAltitude(a); }
    else if (!strcmp(op, "S")) { (void)scanf("%lf %lf %lf", &a, &b, &c); r.onSecond(int(a), int(b), int(c)); }
    else if (!strcmp(op, "L")) r.lap();
    else if (!strcmp(op, "P")) { (void)scanf("%lf", &a); r.pause(uint32_t(a)); }
    else if (!strcmp(op, "R")) { (void)scanf("%lf", &a); r.resume(uint32_t(a)); }
    else if (!strcmp(op, "E")) { (void)scanf("%lf", &a); r.finish(uint32_t(a)); }
  }
  printf("dist %.6f\nmoving %.6f\nelev %.6f\nbest10k %.6f\nmaxSpeed %.6f\n", r.dist, r.moving, r.elev, r.best10k(), r.maxSpeed);
  printf("hr %ld/%ld max %d\npw %ld/%ld\ncad %ld/%ld\nelapsed %u\n", r.hrSum, r.hrN, r.hrMax, r.pwSum, r.pwN, r.cadSum, r.cadN, r.elapsedMs);
  printf("laps"); for (auto& l : r.laps) printf(" %u:%u:%u", l.n, l.d, l.t); printf("\n");
  printf("track %zu\npolyline %s\n", r.track.size(), encodePolyline(simplify(r.track)).c_str());
}
