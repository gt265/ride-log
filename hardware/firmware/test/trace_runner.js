// Same as trace_runner.cpp, for the simulator's JavaScript port.
const { Ride, encodePolyline, simplify } = require('../../simulator/ride_logic.js');
const lines = require('fs').readFileSync(0, 'utf8').trim().split('\n');
const r = new Ride(); const f = x => x.toFixed(6);
for (const ln of lines) {
  const [op, ...n] = ln.trim().split(/\s+/); const v = n.map(Number);
  if (op === 'I') { r.autoPauseOn = v[0] !== 0; r.autoLapKm = v[1]; }
  else if (op === 'B') r.start(v[0]);
  else if (op === 'F') r.onFix(v[1], v[2], v[3], v[4], v[0]);
  else if (op === 'A') r.onAltitude(v[0]);
  else if (op === 'S') r.onSecond(v[0], v[1], v[2]);
  else if (op === 'L') r.lap();
  else if (op === 'P') r.pause(v[0]);
  else if (op === 'R') r.resume(v[0]);
  else if (op === 'E') r.finish(v[0]);
}
console.log(`dist ${f(r.dist)}\nmoving ${f(r.moving)}\nelev ${f(r.elev)}\nbest10k ${f(r.best10k())}\nmaxSpeed ${f(r.maxSpeed)}`);
console.log(`hr ${r.hrSum}/${r.hrN} max ${r.hrMax}\npw ${r.pwSum}/${r.pwN}\ncad ${r.cadSum}/${r.cadN}\nelapsed ${r.elapsedMs}`);
console.log('laps' + r.laps.map(l => ` ${l.n}:${l.d}:${l.t}`).join(''));
console.log(`track ${r.track.length}\npolyline ${encodePolyline(simplify(r.track))}`);
