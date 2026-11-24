# Ride Log bike computer: firmware

The software that runs on the prototype (Seeed XIAO ESP32-S3). It records your ride from GPS and the barometer, connects to Bluetooth heart-rate, power and cadence sensors, and shows four screens on the Sharp display:
- **Ride:** big speed, distance, moving time, average speed, climbing.
- **Sensors:** heart rate, 3-second power, zones, cadence.
- **Laps:** current lap and last lap.
- **Device:** saved rides, battery, GPS status, Wi-Fi transfer.

It saves rides in the board's memory in Ride Log's own format, so they import into the app with the route map, laps and sensor data.

| Folder / file | What it is |
|---|---|
| `RideLogComputer/RideLogComputer.ino` | Main program: screen, buttons, GPS, barometer, Bluetooth, saving, Wi-Fi transfer |
| `RideLogComputer/ride_logic.h` | Ride maths (same rules as the app). Tested on a computer. |
| `RideLogComputer/config.h` | **Your settings:** max HR, FTP, auto-pause, auto-lap, Wi-Fi password, pins |
| `test/ride_logic_test.cpp` | Tests for the ride maths |

## Install (once)
1. Install **Arduino IDE 2** from arduino.cc.
2. Add the ESP32 boards:
   1. Open **File → Preferences → Additional boards manager URLs** and add `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
   2. Open **Tools → Board → Boards Manager**, search **esp32** and install **esp32 by Espressif**.
3. Install the libraries under **Tools → Manage Libraries**:
   - **Adafruit SHARP Memory Display**: say yes when it offers to install **Adafruit GFX** too.
   - **Adafruit GPS Library**
   - **Adafruit BMP3XX Library**
   - **NimBLE-Arduino**: pick a **1.4.x** version from the version dropdown. Version 2 changed its API, and this code is written for 1.4.
4. Set up the board options:
   1. **Tools → Board:** XIAO_ESP32S3.
   2. **Tools → PSRAM:** OPI PSRAM. Long routes are kept in this extra memory.
   3. **Tools → Partition Scheme:** one with a large SPIFFS/LittleFS area, e.g. "8M with spiffs (3MB APP/1.5MB SPIFFS)".

## Upload
1. Open `RideLogComputer/RideLogComputer.ino`.
2. Edit `config.h`: set your **max heart rate, FTP, Wi-Fi password** and time zone (`#define UTC_OFFSET_MIN 60` for UTC+1, etc.).
3. Plug in the XIAO with a USB-C **data** cable, then click **Upload**.
   - If upload fails, hold the tiny **B** (boot) button on the XIAO while plugging it in, then upload again.

## Using it
| Button | Short press | Long press |
|---|---|---|
| **START** | Start / pause / resume | **2 s: finish and save** |
| **LAP** | New lap | |
| **PAGE** | Next screen | **3 s: sleep** (press PAGE to wake) |

- **GPS:** go outside and wait for "GPS" in the top bar to show a satellite count. The first fix can take 1–2 minutes.
- **Sensors:** wake them (put the strap on, spin the cranks) and they connect automatically. The device connects to the first heart-rate, power and cadence sensor it finds.
- **Copying rides to your phone (works on iPhone):**
  1. Go to the **Device** screen and **hold START** to turn Wi-Fi transfer on.
  2. On your phone, join the **RideLog-xxxx** Wi-Fi network, using the password from `config.h`.
  3. Open **http://192.168.4.1** and tap **Download rides**.
  4. In the Ride Log app, go to **Settings → Import** and pick the downloaded file. The rides then sync to your account like any other ride.

## Test the ride maths on a computer

```bash
cd test && g++ -std=c++17 ride_logic_test.cpp -o ride_logic_test && ./ride_logic_test
```

## Status: please read
- **Tested:**
  - the ride maths (`ride_logic.h`) passes all tests, with the same results as the app on the same simulated ride;
  - a ride file in the device's format imports into Ride Log with its route, laps and sensor data;
  - the main program compiles without errors against stand-ins for the libraries.
- **Not tested yet:** it hasn't been compiled with the real ESP32 tools or run on hardware, because the build tools couldn't be downloaded in the environment it was written in. Expect a few small fixes on first compile or first run. Copy any error messages from the Arduino IDE and send them over.
- **Not in this version:**
  - choosing a specific sensor (it takes the first of each type it finds);
  - saving a ride in progress if the battery dies mid-ride;
  - uploading straight to your Ride Log account over home Wi-Fi.
