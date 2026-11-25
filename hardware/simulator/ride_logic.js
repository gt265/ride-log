// Line-by-line JavaScript port of firmware/RideLogComputer/ride_logic.h, used by the simulator.
// Kept in step with the C++ by test/compare_ports.sh (same input trace → same numbers).
(function (root) {
  'use strict';
  const MIN_MOVING = 1.0, MAX_ACCURACY = 35.0, MAX_JUMP = 28.0, STOP_DELAY = 3.0, TRACK_STEP = 8.0, MAX_TRACK = 40000;
  const f32 = Math.fround;
  const lround = v => (v < 0 ? -Math.round(-v) : Math.round(v));     // C lround: halves away from zero

  function haversine(lat1, lon1, lat2, lon2) {
    const R = 6371000.0, rad = Math.PI / 180.0;
    const dLat = (lat2 - lat1) * rad, dLon = (lon2 - lon1) * rad;
    const h = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
              Math.cos(lat1 * rad) * Math.cos(lat2 * rad) * Math.sin(dLon / 2) * Math.sin(dLon / 2);
    return 2 * R * Math.asin(Math.sqrt(h));
  }

  function simplify(p, tolM = 4.0) {
    if (p.length < 3) return p.slice();
    const kx = 111320.0 * Math.cos(p[0].lat / 1e5 * Math.PI / 180.0) / 1e5, ky = 110540.0 / 1e5;
    const keep = new Uint8Array(p.length); keep[0] = keep[p.length - 1] = 1;
    const stack = [[0, p.length - 1]];
    while (stack.length) {
      const [a, b] = stack.pop();
      const ax = p[a].lon * kx, ay = p[a].lat * ky, dx = p[b].lon * kx - ax, dy = p[b].lat * ky - ay;
      const len2 = dx * dx + dy * dy;
      let maxD = 0, idx = 0;
      for (let i = a + 1; i < b; i++) {
        const px = p[i].lon * kx, py = p[i].lat * ky;
        let t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0;
        t = Math.max(0.0, Math.min(1.0, t));
        const ex = ax + t * dx - px, ey = ay + t * dy - py, d = ex * ex + ey * ey;
        if (d > maxD) { maxD = d; idx = i; }
      }
      if (maxD > tolM * tolM) { keep[idx] = 1; stack.push([a, idx]); stack.push([idx, b]); }
    }
    return p.filter((_, i) => keep[i]);
  }

  function encodePolyline(p) {
    let out = '', pa = 0, po = 0;
    const enc = v => {
      let u = (v < 0 ? ~(v << 1) : (v << 1)) >>> 0;
      while (u >= 0x20) { out += String.fromCharCode((0x20 | (u & 0x1f)) + 63); u >>>= 5; }
      out += String.fromCharCode(u + 63);
    };
    for (const q of p) { enc(q.lat - pa); enc(q.lon - po); pa = q.lat; po = q.lon; }
    return out;
  }

  function bestSplit(pts, meters) {
    let best = -1, i = 0;
    for (let j = 0; j < pts.length; j++) {
      while (i + 1 < j && pts[j][0] - pts[i + 1][0] >= meters) i++;
      const dd = pts[j][0] - pts[i][0];
      if (dd >= meters) {
        const t = (pts[j][1] - pts[i][1]) * meters / dd;
        if (t > 0 && (best < 0 || t < best)) best = t;
      }
    }
    return best;
  }

  function hrZone(hr, maxHr) {
    if (hr <= 0 || maxHr <= 0) return 0;
    const p = hr / maxHr;
    return p < .6 ? 1 : p < .7 ? 2 : p < .8 ? 3 : p < .9 ? 4 : 5;
  }
  function powerZone(w, ftp) {
    if (w < 0 || ftp <= 0) return 0;
    const p = w / ftp;
    return p < .56 ? 1 : p < .76 ? 2 : p < .91 ? 3 : p < 1.06 ? 4 : p < 1.21 ? 5 : p < 1.51 ? 6 : 7;
  }

  const IDLE = 0, RUNNING = 1, PAUSED = 2, FINISHED = 3;
  class Ride {
    constructor(autoPauseOn = true, autoLapKm = 0) {
      this.status = IDLE; this.autoPauseOn = autoPauseOn; this.autoLapKm = autoLapKm;
      this.dist = 0; this.moving = 0; this.elev = 0; this.speed = 0; this.maxSpeed = 0;
      this.autoPaused = false; this.laps = []; this.track = []; this.splits = [[0, 0]];
      this.hrSum = 0; this.hrN = 0; this.pwSum = 0; this.pwN = 0; this.cadSum = 0; this.cadN = 0; this.hrMax = 0; this.pwMax = 0;
      this.startedMs = 0; this.elapsedMs = 0;
      this.hasLast = false; this.lastLat = 0; this.lastLon = 0; this.lastT = 0; this.stillSinceMs = 0; this.runningSince = 0;
      this.lapD = 0; this.lapT = 0; this.lastSplitD = 0; this.nextAutoLap = 1;
      this.altInit = false; this.altSm = 0; this.altBase = 0;
    }
    start(nowMs) { Object.assign(this, new Ride(this.autoPauseOn, this.autoLapKm)); this.status = RUNNING; this.startedMs = this.runningSince = nowMs; }
    pause(nowMs) {
      if (this.status !== RUNNING) return;
      this.elapsedMs += nowMs - this.runningSince; this.status = PAUSED; this.hasLast = false; this.speed = 0; this.autoPaused = false;
    }
    resume(nowMs) { if (this.status === PAUSED) { this.status = RUNNING; this.runningSince = nowMs; this.hasLast = false; this.stillSinceMs = 0; } }
    finish(nowMs) {
      if (this.status === RUNNING) this.elapsedMs += nowMs - this.runningSince;
      if (this.laps.length && this.dist - this.lapD > 50) this.closeLap();
      this.status = FINISHED;
    }
    elapsed(nowMs) { return this.elapsedMs + (this.status === RUNNING ? nowMs - this.runningSince : 0); }
    lapDist() { return this.dist - this.lapD; }
    lapTime() { return this.moving - this.lapT; }
    lap() {
      if (this.status !== RUNNING || (this.lapDist() < 20 && this.lapTime() < 10)) return false;
      this.closeLap(); return true;
    }
    onFix(lat, lon, speedMs, accuracyM, tMs) {
      if (this.status !== RUNNING || accuracyM > MAX_ACCURACY) return false;
      if (!this.hasLast) {
        this.hasLast = true; this.lastLat = lat; this.lastLon = lon; this.lastT = tMs;
        if (!this.track.length) this.track.push(Ride.toPt(lat, lon));
        return false;
      }
      const dt = (tMs - this.lastT) / 1000.0;
      if (dt <= 0) return false;
      const d = haversine(this.lastLat, this.lastLon, lat, lon), vGps = d / dt;
      if (vGps > MAX_JUMP) { this.lastLat = lat; this.lastLon = lon; this.lastT = tMs; return false; }
      const v = speedMs >= 0 ? speedMs : vGps;
      if (v < MIN_MOVING || d < Math.min(3.0, accuracyM * 0.3)) {
        this.speed = 0;
        if (!this.stillSinceMs) this.stillSinceMs = tMs ? tMs : 1;
        if (this.autoPauseOn) { if ((tMs - this.stillSinceMs) / 1000.0 >= STOP_DELAY) this.autoPaused = true; }
        else if (dt <= 15) this.moving += dt;
        this.lastT = tMs;
        return false;
      }
      this.stillSinceMs = 0; this.autoPaused = false;
      this.dist += d;
      if (dt <= 15) this.moving += dt;
      this.speed = this.speed > 0 ? 0.6 * v + 0.4 * this.speed : v;
      this.maxSpeed = Math.max(this.maxSpeed, this.speed);
      if (this.dist - this.lastSplitD >= 20) { this.splits.push([f32(this.dist), f32(this.moving)]); this.lastSplitD = this.dist; }
      if (this.track.length < MAX_TRACK) {
        const b = this.track[this.track.length - 1];
        if (haversine(b.lat / 1e5, b.lon / 1e5, lat, lon) >= TRACK_STEP) this.track.push(Ride.toPt(lat, lon));
      }
      this.lastLat = lat; this.lastLon = lon; this.lastT = tMs;
      if (this.autoLapKm > 0 && this.dist >= this.nextAutoLap * this.autoLapKm * 1000) {
        this.nextAutoLap = Math.floor(this.dist / (this.autoLapKm * 1000)) + 1;
        this.closeLap(); return true;
      }
      return false;
    }
    onAltitude(altM) {
      if (this.status !== RUNNING) return;
      this.altSm = this.altInit ? this.altSm + 0.15 * (altM - this.altSm) : altM;
      if (!this.altInit) { this.altInit = true; this.altBase = this.altSm; return; }
      if (this.altSm < this.altBase) this.altBase = this.altSm;
      else if (this.altSm - this.altBase >= 2.0) { this.elev += this.altSm - this.altBase; this.altBase = this.altSm; }
    }
    onSecond(hr, power, cadence) {
      if (this.status !== RUNNING || this.autoPaused) return;
      if (hr > 0) { this.hrSum += hr; this.hrN++; this.hrMax = Math.max(this.hrMax, hr); }
      if (power >= 0) { this.pwSum += power; this.pwN++; this.pwMax = Math.max(this.pwMax, power); }
      if (cadence > 0) { this.cadSum += cadence; this.cadN++; }
    }
    avgSpeed() { return this.moving > 0 ? this.dist / this.moving : 0; }
    best10k() { return bestSplit(this.splits, 10000); }
    static toPt(lat, lon) { return { lat: lround(lat * 1e5), lon: lround(lon * 1e5) }; }
    closeLap() {
      this.laps.push({ n: this.laps.length + 1, d: lround(this.dist - this.lapD), t: lround(this.moving - this.lapT) });
      this.lapD = this.dist; this.lapT = this.moving;
    }
  }
  Object.assign(Ride, { IDLE, RUNNING, PAUSED, FINISHED });

  const api = { Ride, haversine, simplify, encodePolyline, bestSplit, hrZone, powerZone, lround };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.RideLogic = api;
})(typeof window !== 'undefined' ? window : globalThis);
