// Writes a realistic test ride trace: curving route, two stops, a GPS glitch, a pause, laps, hills, sensors.
let seed = 7; const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
const out = []; let lat = 51.5007, lon = -0.1246, head = 0.3, alt = 12, t = 0;
out.push(`I 1 2`, `B 0`);
for (let i = 0; i < 2400; i++) {
  t = i * 1000 + Math.floor(rnd() * 40);
  const stopped = (i > 500 && i < 545) || (i > 1500 && i < 1530);
  const v = stopped ? 0 : 7 + 3 * Math.sin(i / 300) + rnd();
  head += 0.004 + (rnd() - 0.5) * 0.01;
  lat += v * Math.cos(head) / 111320 + (stopped ? (rnd() - .5) * 3e-6 : 0);
  lon += v * Math.sin(head) / (111320 * Math.cos(lat * Math.PI / 180));
  alt += v * 0.03 * Math.sin(i / 200);
  if (i === 900 || i === 901) { out.push(`F ${t} ${lat + 0.01} ${lon} -1 6`); continue; }   // glitch
  if (i === 1200) out.push(`P ${t}`);
  if (i === 1260) out.push(`R ${t}`);
  if (i === 700 || i === 1800) out.push('L');
  out.push(`F ${t} ${lat.toFixed(7)} ${lon.toFixed(7)} ${stopped ? 0 : (i % 7 ? -1 : (v * 0.514444 / 0.514444).toFixed(2))} ${(4 + rnd() * 4).toFixed(1)}`);
  for (let k = 0; k < 4; k++) out.push(`A ${(alt + (rnd() - .5) * 0.6).toFixed(2)}`);
  out.push(`S ${Math.round(120 + v * 4 + rnd() * 5)} ${Math.round(v * 25 + rnd() * 30)} ${stopped ? 0 : 85 + Math.round(rnd() * 10)}`);
}
out.push(`E ${t + 1000}`);
console.log(out.join('\n'));
