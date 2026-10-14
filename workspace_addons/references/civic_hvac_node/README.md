# Civic HVAC Node

Control a 2000 Honda Civic LX climate control board from an ESP32-S3 dash.

The factory climate board stays in a case on its three factory plugs and keeps doing all the real work (blower, mode motor, blend door). Its three dials are removed. An ESP32 DevKitC fakes the dials with 12 PhotoMOS relays, and the S3 dash tells the DevKitC what to set over I2C.

```
S3 dash ──I2C──► DevKitC ──12 PhotoMOS──► dial pads on climate board ──3 factory plugs──► car
```

Firmware is plain **ESP-IDF** (C), built in VS Code with the Espressif ESP-IDF extension. Both projects build with ESP-IDF 6.0.

## What's in this folder

| Path | What it is |
|---|---|
| `wiring/wiring_diagram.svg` / `.png` | Full wiring diagram: power, I2C, all 12 relays, dial pads |
| `wiring/pin_map.csv` | Same wiring as a table, one row per relay |
| `BOM.csv` | Parts list |
| `firmware/hvac_node/` | ESP-IDF project for the DevKitC (target `esp32`) |
| `firmware/s3_link_test/` | ESP-IDF test project for the S3 (target `esp32s3`) |
| `firmware/s3_link_test/components/hvac_link/` | The component to copy into your dash project |

## How the dials work

Each dial has 5 pads on the board: **COM, A, B, C, D**. Each click connects COM to a different combination of A–D (a 4-bit Gray code). The board's microcontroller reads that combination. To fake a dial, the DevKitC closes the right relays between COM and A/B/C/D.

| Dial | Positions | Status |
|---|---|---|
| Temp | 15 (0 = full cold, 14 = full hot) | All confirmed with a meter |
| Mode | 5 (face, face+feet, feet, feet+defrost, defrost) | 0–3 confirmed. Defrost (4) measured B+C+D; if it doesn't select, change it to 13. |
| Fan | 11 (0 = off, 1–10) | 0, 2–4, 6–10 measured. 1 and 5 are best guesses; confirm with `raw` in test mode. |

The code tables are at the top of `firmware/hvac_node/main/main.c` (`FAN_CODES`, `MODE_CODES`, `TEMP_CODES`). Each number is the code with A = 1, B = 2, C = 4, D = 8 added together, e.g. C+D = 12.

## Building the hardware

1. **Remove the three dials** from the climate board (desolder braid or a solder sucker). Leave the pads clean.
2. **Solder 5 wires to each dial's pads** (COM, A, B, C, D, printed on the board). Label them.
3. **Build the relay board** on perfboard following `wiring/wiring_diagram.png`. Use DIP-4 sockets for the relays if you can. For every relay:
   - ESP32 GPIO → 330 Ω → relay pin 1 (pin 1 has the dot/notch)
   - relay pin 2 → ESP32 GND
   - relay pin 3 → that dial's A, B, C or D wire
   - relay pin 4 → that dial's COM wire (the 4 relays of a dial share COM)
4. **Power**: ignition-switched +12 V → 1 A fuse → buck converter → DevKitC 5V/VIN. TVS diode across the buck input (band towards +12 V). Buck ground to car ground.
5. **I2C to the S3**: DevKitC GPIO21 → S3 SDA, GPIO22 → S3 SCL, GND → GND. 4.7 kΩ pull-ups from SDA and SCL to the S3's 3.3 V (skip them if your S3 bus already has pull-ups). Keep the cable under about 1 m.

## Flashing the DevKitC (VS Code + ESP-IDF extension)

1. **File → Open Folder** → `firmware/hvac_node`.
2. In the status bar: set the target to **esp32**, pick the DevKitC's COM port.
3. **Build, Flash and Monitor** (the flame icon), or in an ESP-IDF terminal:
   ```
   idf.py set-target esp32
   idf.py -p COM5 flash monitor
   ```
4. The monitor shows `Civic HVAC node ready, I2C address 0x42` and the command list.

`sdkconfig.defaults` turns on the new I2C slave driver (needed on ESP-IDF 5.5; on 6.0 it's already the default and the build prints a harmless "unknown kconfig symbol" warning).

## Testing, in this order

**1. Bench test the relays (no car, climate board unplugged).**
Meter on continuity, black probe on the TEMP COM wire. In the monitor type `temp 0`: red probe on A should beep, B/C/D should not. Try `temp 1` (A and B beep), `temp 14` (only D). Do a few positions on each dial. `off` opens every relay.

**2. In the car (climate board on its factory plugs, ignition on).**
Type `fan 3`, `mode 4`, `temp 0` and so on, and check the car responds.

**3. Calibrate fan 1, fan 5 and mode 4.**
`raw fan 7` puts code 7 directly on the fan pads. Try codes until the blower does what you expect for that position, then edit `FAN_CODES` / `MODE_CODES` in `main.c` and re-flash. For the fan, the codes not used by any other fan position are 0, 2, 3, 6 and 8, so try those as well as the current guesses (7 for pos 1, 13 for pos 5).

**4. Link test from the S3.**
Open `firmware/s3_link_test`, set `SDA_PIN` / `SCL_PIN` in `main/main.c` to your dash's I2C pins, target **esp32s3**, flash. It finds the node, then cycles through four settings every 4 seconds; the node's monitor logs each one (`dash: fan 3 mode 0 temp 0`).

## Monitor commands on the DevKitC

| Command | Does |
|---|---|
| `fan 3`, `mode 2`, `temp 10` | Step a dial to a position |
| `raw fan 12` | Put a raw code (0–15) on a dial's pads |
| `off` | Open all relays |
| `status` | Show current positions |
| `help` | List commands |

## Using it from your dash

Copy `firmware/s3_link_test/components/hvac_link` into your dash project's `components/` folder and add `hvac_link` to the `REQUIRES` of the component that uses it. It uses the ESP-IDF `i2c_master` driver (5.2 or newer). Then, after your dash creates its I2C bus:

```c
#include "hvac_link.h"

hvac_link_init(bus);                           // once, with your i2c_master_bus_handle_t
hvac_link_set(3, HVAC_MODE_FACE, 7);           // fan 3, face vents, temp middle
if (hvac_link_probe() != ESP_OK) { /* node missing */ }
```

`hvac_link_set` returns `ESP_OK` when the node acknowledged the packet. The node steps each dial one click at a time (60 ms per click) like a hand turning the knob, so a big change takes up to about a second.

### I2C protocol (if you want to write your own)

- Address **0x42**, 100 kHz, write only.
- 5 bytes: `[0xA5, fan, mode, temp, checksum]`, checksum = `(fan + mode + temp) & 0xFF`.
- Ranges: fan 0–10, mode 0–4, temp 0–14. Out-of-range values are clamped.
- `hvac_protocol.h` holds these constants; keep the copy in `hvac_node/main/` and the one in `hvac_link/include/` identical.

## Notes

- A/C and recirc buttons are not controlled.
- On power-up the node sets fan off, mode face and temp middle (position 7).
- Compiled with ESP-IDF 6.0 but not yet run on hardware. Expect to adjust things on the first build.
