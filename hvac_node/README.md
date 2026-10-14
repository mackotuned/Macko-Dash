# MackoDash HVAC Node

ESP-IDF firmware for a 30-pin DOIT ESP32 DevKit V1 with an ESP32-WROOM-32 module.
It controls the relocated 2000 Civic climate board through 12 mechanical relays and
communicates with the MackoDash rotary ESP32-S3 over encrypted ESP-NOW channel 6.

The original imported design remains unchanged under
`workspace_addons/references/civic_hvac_node/`.

## Shopping List

- [Complete parts list with Amazon links](wiring/CLIMATE_BUILD_LIST.md)
- [Hosyond four-pack of 4-channel 5 V relay modules](https://www.amazon.com/dp/B0C2Z8JF5R)
- [HUABAN ULN2803A DIP-18 driver pack](https://www.amazon.com/dp/B0H35YJFK8)

The complete list includes Amazon links for the ESP32, power supply, protection
parts, enclosure, connectors, wire, assembly supplies, and tools.

## Board wiring

- [Current relay-module wiring diagram](wiring/prime_relay_module_pinout.svg)
- [Relay-module connection table](wiring/prime_relay_module_pin_map.csv)
- [Simplified climate build diagram](wiring/climate_build_diagram.svg)
- [Detailed wiring instructions](wiring/README.md)
- [Original PhotoMOS wiring alternative](wiring/espnow_board_pinout.svg)
- [Machine-readable PhotoMOS pin map](wiring/pin_map_espnow.csv)

## Verified board

- Target: ESP32-D0WD-V3 revision 3.1
- Flash: 4 MB
- Factory STA MAC: `B4:BF:E9:6A:23:14`
- USB bridge: CP210x
- Initial development port: COM7

Never connect vehicle 12 V directly to the DevKit. Use the fused, protected
12 V-to-5 V automotive converter and feed regulated 5 V to `VIN/5V`.
