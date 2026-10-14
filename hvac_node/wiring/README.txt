MackoDash ESP-NOW HVAC board

J1 FAN:  1 COM, 2 A, 3 B, 4 C, 5 D
J2 MODE: 1 COM, 2 A, 3 B, 4 C, 5 D
J3 TEMP: 1 COM, 2 A, 3 B, 4 C, 5 D

FAN:  A GPIO16, B GPIO17, C GPIO18, D GPIO19
MODE: A GPIO23, B GPIO25, C GPIO26, D GPIO27
TEMP: A GPIO32, B GPIO33, C GPIO4,  D GPIO13

Each channel: GPIO -> 330R -> AQY212EH pin 1; pin 2 -> ESP32 GND;
pin 3 -> dial A/B/C/D; pin 4 -> that dial COM.

Power: ignition +12V -> 1A fuse -> protected automotive 5V buck -> VIN/5V.
SMBJ24A across fused buck input, band toward +12V. 470uF across 5V/GND.
No wires to the S3: ESP-NOW channel 6.
