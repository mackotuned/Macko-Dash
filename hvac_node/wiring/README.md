# ESP-NOW HVAC Relay Board Wiring

This is the current board wiring for the 30-pin ESPDEVKITV1 / ESP32-WROOM-32.
The rotary S3 link is wireless ESP-NOW channel 6, so there are no SDA, SCL, TX,
RX, or other signal wires between the two ESP32 boards.

## Start here

- [Complete shopping list with Amazon links](CLIMATE_BUILD_LIST.md)
- [Simplified four-step wiring overview](climate_build_diagram.svg)
- [Detailed pin-level assembly reference](prime_relay_module_pinout.svg)
- [Machine-readable connection table](prime_relay_module_pin_map.csv)

Use the simplified diagram first and the detailed pinout only when landing each
individual wire.

## Civic climate-board bench power

Use `civic_board_bench_power.svg` when powering the 1999-2000 Civic heater
control board on the bench. The drawing is a direct PCB-connector view from the
harness side. It identifies 8P pin 6 as ground, 8P pin 8 as IG2 power, and 20P
pin 10 as +B power from the factory service-manual input/output table.

Start at 12.0 V with a 100 mA current limit and a 500 mA or 1 A inline fuse.
Connect supply positive to both power inputs and supply negative to 8P pin 6.
Do not apply power to the 12P connector or any other cavity.

## Amazon Prime build

The current Prime-only build uses these items:

- Hosyond four-pack of 4-channel 5 V relay modules, Amazon ASIN `B0C2Z8JF5R`.
  Use three modules and keep the fourth as a spare.
- ULN2803A DIP-18 driver pack, Amazon ASIN `B0H35YJFK8`. Use two ICs.
- Automotive 9-36 V input to regulated 5 V/3 A buck converter.

Follow `prime_relay_module_pinout.svg` and `prime_relay_module_pin_map.csv` for
this build. Set every relay-module channel for LOW-level trigger. The ULN2803A
stages invert the active-high ESP32 outputs into the required active-low module
inputs while preventing the module's 5 V input circuitry from reaching a GPIO.

Use a 2 A input fuse and a 1000 uF/10 V or higher output capacitor for this
version. Three mechanical relay boards draw substantially more current than the
PhotoMOS design.

The remaining PhotoMOS sections describe the original lower-power alternative.

## Climate connectors

Use the same pin order for all three locking connectors:

| Connector | Pin 1 | Pin 2 | Pin 3 | Pin 4 | Pin 5 |
|---|---|---|---|---|---|
| J1 FAN | COM | A | B | C | D |
| J2 MODE | COM | A | B | C | D |
| J3 TEMP | COM | A | B | C | D |

Do not join FAN COM, MODE COM, and TEMP COM together. Each COM is shared only by
the four PhotoMOS output pins belonging to that dial.

## Relay input pin map

| Dial | A | B | C | D |
|---|---:|---:|---:|---:|
| FAN | GPIO16 | GPIO17 | GPIO18 | GPIO19 |
| MODE | GPIO23 | GPIO25 | GPIO26 | GPIO27 |
| TEMP | GPIO32 | GPIO33 | GPIO4 | GPIO13 |

For every AQY212EH:

1. ESP32 GPIO -> individual 330 ohm resistor -> AQY212EH pin 1 (LED anode).
2. AQY212EH pin 2 (LED cathode) -> ESP32 GND.
3. AQY212EH pin 3 -> the assigned A/B/C/D climate-board pad.
4. AQY212EH pin 4 -> that dial's COM pad.

Pins 3 and 4 are a floating, non-polarized MOSFET switch. The chosen orientation
keeps the board consistent. Never connect a climate dial pad directly to ESP32
3.3 V, 5 V, VIN, or GND.

## Power input

```text
Ignition-switched +12 V
        |
      F1 1 A
        |
        +---------------------> buck IN+
        |
      D1 SMBJ24A
  band/cathode toward +12 V
        |
Car ground -------------------> buck IN-

buck OUT+ regulated 5 V ------> ESPDEVKITV1 VIN/5V
buck OUT- --------------------> ESPDEVKITV1 GND
C1 470 uF / 10 V+ across 5 V and GND near the DevKit
```

Disconnect vehicle/buck power while programming over USB unless the exact
DevKit power-path behavior has been verified. Never apply vehicle 12 V to VIN,
5V, 3V3, or any GPIO.

## PCB/layout notes

- Keep the ESP32 antenna end at the board edge with no copper, wires, relays, or
  enclosure metal beneath or directly in front of the antenna.
- Socket the DevKit with two 15-position, 2.54 mm female headers.
- Socketing the DIP-4 PhotoMOS relays is optional but makes replacement easier.
- Keep the three isolated climate connector traces away from the 12 V input.
- Place D1 immediately after F1 and close to the buck input connector.
- Place C1 close to the ESPDEVKITV1 VIN and GND pins.
- Use a star-like ground return between the buck, ESP32, and relay LED cathodes.
- Label J1/J2/J3 and COM/A/B/C/D on both sides of the board.
