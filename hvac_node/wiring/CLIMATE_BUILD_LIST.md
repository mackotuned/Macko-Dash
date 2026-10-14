# MackoDash Climate Controller Build List

This list is for the current ESP-NOW build using three 4-channel mechanical relay modules. It matches the active firmware in `hvac_node/main/main.c`.

## Main Parts

| Qty | Item | Specification / example | Purpose |
|---:|---|---|---|
| 1 | ESP32 development board | 30-pin ESPDEVKITV1 / ESP32-WROOM-32 | Runs the HVAC node firmware |
| 3 | 4-channel 5 V relay modules | Hosyond module, Amazon ASIN `B0C2Z8JF5R` | One module each for FAN, MODE, and TEMP |
| 1 | Spare 4-channel relay module | Included in the four-pack above | Replacement module |
| 2 | ULN2803A driver ICs | DIP-18, Amazon ASIN `B0H35YJFK8` | Protects the GPIOs and drives active-low relay inputs |
| 2 | DIP-18 IC sockets | 2.54 mm pitch | Allows driver replacement without desoldering |
| 1 | Automotive buck converter | 9-36 V input, regulated 5 V output, 3 A minimum | Powers the ESP32 and relay modules |
| 1 | TVS diode | SMBJ24A | Suppresses vehicle voltage spikes |
| 1 | Inline fuse holder and 2 A fuse | Ignition-switched input | Protects the climate controller wiring |
| 1 | Electrolytic capacitor | 1000 uF, 10 V minimum; 16 V preferred | Stabilizes the 5 V rail near the relay modules |
| 1 | Perfboard or protoboard | Large enough for the ESP32 and two DIP-18 sockets | Mounts the controller and driver wiring |
| 2 | 15-position female headers | 2.54 mm pitch | Sockets the 30-pin ESP32 board |
| 3 | 5-pin locking connector pairs | One pair each for FAN, MODE, and TEMP | Connects COM/A/B/C/D to the Civic board |
| 1 | Project enclosure | Large enough for the controller and three relay modules | Protects the assembly |
| 12 | M3 standoffs with screws | Four per relay module | Secures the relay modules |

## Amazon Shopping Links

These are non-affiliate Amazon US links. The relay and ULN2803A links are the
specific parts already selected for this build. Generic links are searches
because those listings change frequently; match the specification shown before
ordering.

### Electronics

| Qty | Item | Amazon link | Required specification |
|---:|---|---|---|
| 1 | ESP32 development board | [ESP32-WROOM-32 30-pin DevKit V1](https://www.amazon.com/s?k=ESP32-WROOM-32+30+pin+ESPDEVKITV1) | 30-pin ESPDEVKITV1 / DOIT layout |
| 1 pack | 4-channel relay modules | [Hosyond four-pack, B0C2Z8JF5R](https://www.amazon.com/dp/B0C2Z8JF5R) | 5 V, four channels, selectable LOW trigger; use three modules |
| 1 pack | ULN2803A driver ICs | [ULN2803A DIP pack, B0H35YJFK8](https://www.amazon.com/dp/B0H35YJFK8) | DIP-18 package; use two ICs |
| 1 pack | DIP-18 sockets | [18-pin DIP IC sockets](https://www.amazon.com/s?k=18+pin+DIP+IC+socket+2.54mm) | 2.54 mm pitch; use two |
| 1 | Automotive buck converter | [9-36 V to 5 V 3 A automotive buck](https://www.amazon.com/s?k=automotive+9-36V+to+5V+3A+buck+converter) | Wide input, regulated 5 V, at least 3 A |
| 1 pack | TVS diodes | [SMBJ24A TVS diode](https://www.amazon.com/s?k=SMBJ24A+TVS+diode) | SMBJ24A, unidirectional |
| 1 | Fuse holder and fuses | [Inline blade fuse holder with 2 A fuses](https://www.amazon.com/s?k=inline+blade+fuse+holder+2A+fuse) | Automotive inline holder and 2 A fuse |
| 1 pack | Capacitors | [1000 uF 16 V electrolytic capacitor](https://www.amazon.com/s?k=1000uF+16V+electrolytic+capacitor) | 1000 uF, 10 V minimum; 16 V preferred |
| 1 pack | Perfboard | [Large solderable perfboard](https://www.amazon.com/s?k=large+solderable+perfboard+2.54mm) | 2.54 mm pitch; fits ESP32 and two DIP-18 sockets |
| 1 pack | Female headers | [2.54 mm breakaway female headers](https://www.amazon.com/s?k=2.54mm+breakaway+female+header+socket) | Two 15-position rows for the ESP32 socket |
| 1 kit | 5-pin locking connectors | [5-pin locking wire connectors](https://www.amazon.com/s?k=5+pin+locking+wire+connector+kit) | Three male/female pairs; rated for automotive vibration |
| 1 kit | M3 standoffs | [M3 nylon standoff assortment](https://www.amazon.com/s?k=M3+nylon+standoff+kit) | Twelve standoffs plus screws for three relay modules |
| 1 | Project enclosure | [ABS electronics project box](https://www.amazon.com/s?k=large+ABS+electronics+project+box) | Confirm internal dimensions after laying out all three relay boards |

### Wire And Assembly Supplies

| Qty | Item | Amazon link | Required specification |
|---:|---|---|---|
| 1 kit | Power wire | [18 AWG automotive primary wire](https://www.amazon.com/s?k=18+AWG+automotive+primary+wire+red+black) | Red and black stranded copper wire |
| 1 kit | Signal wire | [22 AWG stranded hookup wire kit](https://www.amazon.com/s?k=22+AWG+stranded+hookup+wire+kit) | Multiple colors for GPIO and driver wiring |
| 1 length | Climate cable | [15-conductor stranded cable](https://www.amazon.com/s?k=15+conductor+22+AWG+stranded+cable) | Fifteen conductors, approximately 22-24 AWG |
| 1 kit | Heat-shrink tubing | [Adhesive heat-shrink assortment](https://www.amazon.com/s?k=adhesive+lined+heat+shrink+tubing+kit) | Multiple sizes |
| 1 kit | Ferrules | [Wire ferrule crimp kit](https://www.amazon.com/s?k=wire+ferrule+crimp+kit) | Fits 18-24 AWG wire |
| 1 kit | Cable labels | [Self-laminating wire labels](https://www.amazon.com/s?k=self+laminating+wire+labels) | Label FAN, MODE, TEMP, COM, A, B, C, and D |
| 1 pack | Strain reliefs | [Nylon cable glands](https://www.amazon.com/s?k=nylon+cable+gland+assortment) | Select sizes to match the enclosure cable diameters |
| 1 set | Distribution blocks | [Small DC power distribution blocks](https://www.amazon.com/s?k=small+DC+power+distribution+block+5V) | Separate 5 V and ground distribution |
| 1 roll | Solder | [63/37 electronics solder](https://www.amazon.com/s?k=63%2F37+electronics+solder) | Rosin-core electronics solder |
| 1 | Flux | [No-clean electronics flux](https://www.amazon.com/s?k=no+clean+electronics+soldering+flux) | Electronics-safe, no-clean type |

### Tools

| Qty | Tool | Amazon link | Notes |
|---:|---|---|---|
| 1 | Soldering station | [Temperature-controlled soldering station](https://www.amazon.com/s?k=temperature+controlled+soldering+station) | Fine and medium tips recommended |
| 1 | Digital multimeter | [Digital multimeter with continuity](https://www.amazon.com/s?k=digital+multimeter+continuity+mode) | Must include resistance and continuity modes |
| 1 | Bench supply | [Adjustable DC bench supply](https://www.amazon.com/s?k=adjustable+DC+bench+power+supply+current+limit) | 0-30 V with adjustable current limit |
| 1 | Wire stripper | [18-24 AWG wire stripper](https://www.amazon.com/s?k=18-24+AWG+wire+stripper) | Sized for the listed wire gauges |
| 1 | Ferrule crimper | [Ferrule crimping tool](https://www.amazon.com/s?k=ferrule+crimping+tool+AWG) | Often included with a ferrule kit |
| 1 | USB cable | [USB-A to Micro-USB data cable](https://www.amazon.com/s?k=USB+A+to+Micro+USB+data+cable) | Must support data, not charge-only |

## Wire And Assembly Supplies

- 18 AWG red and black wire for 12 V input and 5 V distribution.
- 22-24 AWG wire for GPIO, driver, and relay input wiring.
- Fifteen conductors for the Civic climate board: five each for FAN, MODE, and TEMP.
- Heat-shrink tubing, ferrules, cable labels, and strain reliefs.
- Screw-terminal distribution blocks for 5 V and ground are recommended.
- Solder, flux, and insulated hookup wire for the perfboard.

## Tools

- Temperature-controlled soldering iron.
- Digital multimeter with continuity mode.
- Bench supply with adjustable current limit.
- Wire stripper and crimper.
- USB cable for the ESP32.

## Required Connections

### ESP32 To ULN2803A

| Climate function | ESP32 GPIO | ULN input | ULN output | Relay input |
|---|---:|---:|---:|---|
| FAN A | 16 | U1 pin 1 | U1 pin 18 | FAN IN1 |
| FAN B | 17 | U1 pin 2 | U1 pin 17 | FAN IN2 |
| FAN C | 18 | U1 pin 3 | U1 pin 16 | FAN IN3 |
| FAN D | 19 | U1 pin 4 | U1 pin 15 | FAN IN4 |
| MODE A | 23 | U1 pin 5 | U1 pin 14 | MODE IN1 |
| MODE B | 25 | U1 pin 6 | U1 pin 13 | MODE IN2 |
| MODE C | 26 | U1 pin 7 | U1 pin 12 | MODE IN3 |
| MODE D | 27 | U1 pin 8 | U1 pin 11 | MODE IN4 |
| TEMP A | 32 | U2 pin 1 | U2 pin 18 | TEMP IN1 |
| TEMP B | 33 | U2 pin 2 | U2 pin 17 | TEMP IN2 |
| TEMP C | 4 | U2 pin 3 | U2 pin 16 | TEMP IN3 |
| TEMP D | 13 | U2 pin 4 | U2 pin 15 | TEMP IN4 |

For both ULN2803A ICs, connect pin 9 to logic ground and leave pin 10 open. Tie unused U2 inputs 5-8 to ground.

### Relay Contacts To Civic Board

Use the same connector order for all three groups:

| Connector pin | Signal |
|---:|---|
| 1 | COM |
| 2 | A |
| 3 | B |
| 4 | C |
| 5 | D |

For each relay module:

- K1 NO goes to A.
- K2 NO goes to B.
- K3 NO goes to C.
- K4 NO goes to D.
- All four relay COM terminals connect to that group's COM wire.
- Leave every NC terminal disconnected.

Keep FAN COM, MODE COM, and TEMP COM separate. Never connect a Civic dial wire to ESP32 ground, 3.3 V, 5 V, VIN, or a GPIO.

## Recommended Build Order

1. Mount the ESP32 socket and both ULN2803A sockets on the perfboard.
2. Build the fused 12 V input, TVS protection, buck converter, and 5 V distribution.
3. Set the buck output to 5.0 V before connecting the ESP32 or relay modules.
4. Wire all ESP32 GPIOs to the ULN inputs using the table above.
5. Wire the ULN outputs to the three relay-module inputs.
6. Set every relay-module channel to LOW-level trigger.
7. Connect regulated 5 V and logic ground to the ESP32 and all relay modules.
8. Wire each module's NO/COM contacts to its separate 5-pin climate connector.
9. Check for shorts and verify that the three COM groups are not connected together.
10. Flash and test the HVAC node over USB before connecting the Civic climate board.
11. Bench-test the Civic board with a current-limited 12 V supply.
12. Connect vehicle power only after all bench tests pass.

## Safety Checks Before Power

- Buck output measures 5.0 V with no ESP32 connected.
- No continuity exists between FAN COM, MODE COM, and TEMP COM.
- No climate connector pin has continuity to ESP32 ground, 5 V, or 12 V.
- ULN2803A orientation and pin 1 marks are correct.
- Relay modules are set for LOW trigger.
- Vehicle 12 V only enters the fuse, TVS, and buck converter input.
- Buck power is disconnected while programming over USB unless the board's power path has been verified.

The S3 controller and this HVAC node communicate wirelessly on ESP-NOW channel 6. Do not add SDA, SCL, TX, or RX wires between them.
