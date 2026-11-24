# Selling the Ride Log bike computer for under $100

The prototype in this folder uses hobby boards (Adafruit and Seeed): about **$130–150 in parts** for a single unit.
To sell at **$99 or less**, the parts inside each unit need to cost about **$25–40**, because shop fees, shipping, returns and marketing take a large share of the price.
That means a **custom circuit board** with the chips soldered on directly, instead of plug-in boards, and a **molded case** instead of a 3D-printed one.

## The market
Budget GPS bike computers already sell for **$40–75**. The iGPSPORT BSC100S is about $40–50, and it has a 2.6" screen, Bluetooth + ANT+ and a 40 h battery.
Ride Log can't win on price alone. Possible selling points:
- It works with **your own app and accounts**: rides sync automatically, with no extra app to install.
- It connects to **Bluetooth sensors on any phone**, iPhone included.
- It's **open and repairable**: a replaceable battery, and published firmware.
- Optionally, a **premium sunlight screen** (the Sharp memory LCD) that budget brands don't use.

## Production parts (per unit, at 1,000 units)
✓ = price checked online (Nov 2026). Everything else is an **estimate to confirm with supplier quotes**.

| Part | Choice | Approx. per unit |
|---|---|---|
| Main chip, with Bluetooth + Wi-Fi | ESP32-C3-MINI-1-N4 module (pre-certified radio) | **$2.05 ✓** (LCSC, 650+) |
| GPS | Quectel L76K or AT6558-based module + ceramic patch antenna | $5–7 |
| Screen, option A (cheaper) | 2.4–2.7" transflective monochrome LCD with backlight (the kind budget brands use) | $3–6 |
| Screen, option B (premium) | Sharp LS027B7DH01 2.7" memory LCD | **$17–20 ✓** (DigiKey/LCSC; 28-week lead time at DigiKey) |
| Barometer | SPL06-001 or BMP280 | $0.50–1 |
| Battery | 800–1000 mAh LiPo with protection | $2–3 |
| Charging, power and USB-C | TP4054 charger, LDO regulator, USB-C socket, ESD protection | $1–1.50 |
| Buttons, passives | 3 side buttons, resistors and capacitors | $0.50–1 |
| Circuit board, assembly and test | 4-layer board, machine assembled, tested | $3–5 |
| Case | 2 injection-molded parts, window, gasket, screws | $2–3 |
| In the box | Garmin-style mount, USB-C cable, box, quick-start card | $2–3 |
| **Total with screen A** | | **≈ $22–32** |
| **Total with screen B** | | **≈ $37–46** |

### Where the money goes at $99 (rough)
| | Screen A | Screen B |
|---|---|---|
| Parts and build | $27 | $42 |
| Shipping to customer, payment fees, warranty and returns reserve | $15 | $15 |
| **Left over (marketing, profit, paying back start-up costs)** | **≈ $57** | **≈ $42** |

Selling on Amazon instead of your own site costs roughly another $15–20 per unit in fees.
With screen A you could also price at **$69–79**, which is closer to the competition.

## One-time start-up costs (estimates)
| Item | Approx. |
|---|---|
| Circuit board design, 2–3 prototype rounds | $1–3k |
| Injection mold tooling (aluminium, 2 parts) | $5–15k |
| Radio and safety testing: FCC (USA), CE / UKCA (EU and UK). Using a pre-certified radio module keeps this cheaper. | $5–12k |
| Lithium battery shipping certification (UN38.3); often covered by the battery supplier's report | $0–3k |
| First batch: 500 units × ~$30 | ~$15k |
| **Total to launch** | **≈ $26–48k** |

**You can't legally sell finished electronics without the FCC/CE step, even a small batch.** Two ways to start smaller:
1. **Pre-orders / crowdfunding**: collect orders first, then pay for tooling and testing.
2. **DIY kit**: sell the 3D-printed case, a wiring board and firmware, and let buyers add ready-made, already-certified modules. Kits don't need molds, and testing is much simpler. Check the rules for your country.

## What changes in the design
- **Custom circuit board** shaped to fit the case. It replaces the five hobby boards and lets the case shrink to about **75 × 48 × 18 mm**.
- **Case for injection molding**:
  - even 1.8 mm walls, with draft angles so parts release from the mold;
  - a gasket groove for rain (target IPX6/7) and molded rubber buttons;
  - a built-in Garmin quarter-turn mount.
- **Firmware**: the ride screen, Bluetooth sensors, and ride upload to Ride Log over Wi-Fi or the phone app.
