# Ride Log

A cycling results dashboard that doubles as a bike computer on your phone.
It's a plain web app (HTML, CSS and JavaScript) with no build step, and you can install it like a normal app.

## Features
- Log rides and races: distance, moving time, power, heart rate, climbing, finishing position, notes
- Dashboard: summary tiles, speed / power / heart-rate trend, weekly or monthly distance, sortable results table, personal bests, monthly goal
- **Bike computer**: put your phone on the handlebars and it records speed, distance, moving time, climbing and your real fastest 10 km from GPS, plus:
  - live route map, and a map of every recorded ride
  - auto-pause, manual laps and auto-laps (1 / 5 / 10 km)
  - six customisable data boxes, landscape layout, spoken updates every 1 / 5 / 10 km
  - Bluetooth heart rate, power and cadence with HR / power zones (Chrome on Android and computers; iPhone browsers don't allow Bluetooth)
- **Accounts & cloud sync** (optional): sign in with email and password, and your rides and profile sync between phone and computer. Offline-first: everything saves on the phone first and syncs when you're back online. Setup: [`supabase/README.md`](supabase/README.md)
- Profile (name, monthly goal, weight, FTP, max HR) and **light / dark / system** theme in Settings
- CSV import and export
- Works offline once installed

## Install on your phone
Open the GitHub Pages link, then:
- **Android (Chrome):** tap **Install app** in the toolbar, or ⋮ menu → *Install app*
- **iPhone (Safari):** Share → *Add to Home Screen*

Long-press the installed icon for a **Start ride** shortcut (Android).

## Riding with it
- The screen must stay on: browsers stop GPS when the phone locks. The app asks the phone to keep the screen awake; if the status line says "keep screen on manually", turn off auto-lock.
- GPS with the screen on uses about 10–20% battery per hour.
- A ride in progress is saved every few seconds, so closing the app doesn't lose it.

## Run locally
Any static web server works, e.g.:

```bash
python3 -m http.server 8000
```

Then open http://localhost:8000. GPS and installing need `https://` (or `localhost`).

## Files
| File | Purpose |
|---|---|
| `index.html` | The whole app |
| `config.js` | Supabase URL + publishable key (empty = accounts off) |
| `supabase/` | Database setup script and step-by-step account setup |
| `vendor/supabase.js` | supabase-js 2.45.4 (MIT), bundled for offline use |
| `vendor/leaflet/` | Leaflet 1.9.4 (BSD-2), maps; map tiles © OpenStreetMap contributors |
| `sw.js` | Offline cache. **Bump `VERSION` whenever you change any file** so installed copies update |
| `manifest.webmanifest` | App name, icons and colours used when installing |
| `vendor/chart.umd.min.js` | Chart.js 4.4.1 (MIT), bundled so charts work offline |
| `icons/` | App icons |
