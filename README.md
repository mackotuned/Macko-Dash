<div align="center">

<img src="https://raw.githubusercontent.com/mackotuned/MackoDash-Flash-Tools/MackoDash/mackodash-hero.png" alt="MackoDash digital dashboard installed in a vehicle" width="780">

# MackoDash

**A configurable digital gauge cluster for the 9-inch Elecrow CrowPanel ESP32-P4/C6**

[Get Flash Tools](https://github.com/mackotuned/MackoDash-Flash-Tools) | [Firmware Update Guide](https://github.com/mackotuned/MackoDash-Flash-Tools#firmware-update) | [Custom Theme Guide](https://github.com/mackotuned/MackoDash-Flash-Tools/blob/MackoDash/squareline-theme-guide.md) | [Get Support](#support)

</div>

MackoDash brings ECU, vehicle, warning, and odometer data together on a responsive 1024x600 touch display. Choose a built-in layout, configure the channels and units that matter to you, or create a custom dashboard theme.

## Start Here

You do not need ESP-IDF, Python, or programming tools to update or customize your MackoDash.

1. Open the [MackoDash Flash Tools](https://github.com/mackotuned/MackoDash-Flash-Tools) repository.
2. Select **Code**, then **Download ZIP**, and extract the complete folder on a Windows PC.
3. Keep all extracted files together and open `MackoDashUtility.exe`.

The utility includes two customer tools:

| Tool | What it does |
| --- | --- |
| **Firmware Update** | Safely installs official ESP32-P4 dashboard firmware over USB |
| **Theme Builder** | Converts a supported SquareLine Studio export into an SD-card theme |

## Hardware and Compatibility

MackoDash currently targets:

- 9-inch Elecrow CrowPanel with ESP32-P4 and ESP32-C6
- 1024x600 EK79007 MIPI DSI display
- GT911 capacitive touch controller
- EK Honda Civic and RD1 Honda CR-V installations
- Hondata S300 V3 and supported aftermarket ECU CAN streams
- Built-in CAN transceiver for connection to the vehicle CAN bus

This firmware is built specifically for the MackoDash CrowPanel hardware configuration. Do not flash it to a different ESP32 board or display.

## First Startup

Before driving with MackoDash:

1. Confirm the display and touch controls start normally.
2. Open **Settings > ECU** and select the correct protocol, or use automatic detection.
3. Set speed, temperature, pressure, and distance units.
4. Set the vehicle's VTEC point, redline, and warning thresholds.
5. Use a simulation mode to become familiar with the selected theme and warning behavior.
6. With the vehicle stationary, start the engine and verify every displayed value against a trusted source.
7. Confirm warning audio, brightness, and screen visibility before road use.

Settings, theme selection, odometer data, and trip data are retained after power is removed.

## Dashboard Features

- Five built-in themes: MackoDash V1, Race LCD, HalDash, Endurance, and Touring
- Up to 30 custom themes from a FAT-formatted microSD card
- Live RPM, speed, gear, coolant, intake air, AFR, timing, boost/MAP, battery, throttle, oil pressure, injector duty, knock, and fuel data
- Reassignable gauge channels on built-in themes
- Independent speed, temperature, pressure, and distance units
- Configurable VTEC point, redline, warning thresholds, and warning chimes
- Selectable redline screen-flash color
- Persistent odometer plus Trip A and Trip B
- Adjustable display brightness and value smoothing
- Idle, Cruise, Full Throttle, and Redline simulation modes
- Protected factory reset

## Supported ECU Protocols

The ECU settings page supports automatic detection or manual selection of:

- Hondata
- Haltech
- Link G4X
- MegaSquirt
- Emtron
- MaxxECU
- ECUMaster Black

Available channels depend on what the selected ECU broadcasts. Always verify channel scaling and behavior for the specific ECU calibration before relying on a displayed value.

## Firmware Updates

Use the official Windows updater from [MackoDash Flash Tools](https://github.com/mackotuned/MackoDash-Flash-Tools#firmware-update).

1. Connect the dashboard to the Windows PC with its update USB cable.
2. Keep the dashboard powered and the USB cable connected throughout the update.
3. Open the **Firmware Update** tab in MackoDash Utility.
4. Select **Download Latest** to fetch and validate the current official firmware, or use **Choose ZIP** for a bundle already on the PC.
5. Confirm the dashboard COM port.
6. Select **Flash ESP32-P4 Firmware** and wait for verification to finish.

The full firmware ZIP contains the ESP32-P4 bootloader, partition table, OTA metadata, dashboard application, and SPIFFS storage. NVS settings, odometer and trip data, SD-card themes, and ESP32-C6 firmware are not touched. Onboard SPIFFS is replaced by the version included in the firmware bundle.

> Do not disconnect USB or dashboard power until verification has finished.

## Custom Themes

Use the **Theme Builder** included in [MackoDash Flash Tools](https://github.com/mackotuned/MackoDash-Flash-Tools#custom-themes). The complete [SquareLine Theming Guide](https://github.com/mackotuned/MackoDash-Flash-Tools/blob/MackoDash/squareline-theme-guide.md) lists every supported live value, object name, gauge type, range, and design rule.

Quick workflow:

1. Create one 1024x600 dashboard screen in SquareLine Studio using LVGL 8.x.
2. Name live Labels, Bars, Arcs, units, and the settings button using the theming guide.
3. Export the complete SquareLine C project and ZIP the exported folder.
4. Open **Theme Builder**, select the ZIP, and enter a theme name and unique lowercase ID.
5. Build the theme, select **Copy to SD Card**, and choose the microSD card.
6. Insert the card into MackoDash and reboot.

Themes are stored as `*.mdtheme.zip` files in `/MACKODASH/THEMES`. Invalid packages are not activated; MackoDash falls back to a built-in theme instead.

## Troubleshooting

| What you see | What to check |
| --- | --- |
| Updater says the COM port is busy | Close ESP-IDF Monitor, serial terminals, and other flashing programs, then retry |
| Firmware ZIP does not show as Validated | Download the official `MackoDash-Firmware.zip` again without modifying or extracting it |
| ECU values remain at zero | Confirm vehicle power, CAN wiring, transceiver connection, ECU protocol, and CAN bitrate |
| Some channels remain unavailable | Confirm that the selected ECU protocol broadcasts those channels |
| Theme does not appear | Use a FAT-formatted card and confirm the package is in `/MACKODASH/THEMES` |
| Theme fails strict validation | Compare every live object name and font against the SquareLine Theming Guide |
| Touch or display does not start normally | Power down, check all panel connections, and restart before using the vehicle |

## Support

For customer updater or theme-tool help, use the [MackoDash Flash Tools issue tracker](https://github.com/mackotuned/MackoDash-Flash-Tools/issues).

- Email: [mackotuned@gmail.com](mailto:mackotuned@gmail.com)
- Instagram: [@cream_civic](https://instagram.com/cream_civic)
- TikTok: [@mackotuned](https://tiktok.com/@mackotuned)

When requesting help, include the vehicle, ECU, selected protocol, firmware version, what you expected, what happened, and a clear photo or video when relevant. Do not post private vehicle or account information in a public issue.

## Safety

MackoDash is custom automotive electronics software. Installation and use are at your own risk.

- Configure and test the dashboard while the vehicle is stationary.
- Verify CAN scaling, units, warning thresholds, wiring, and displayed values against trusted equipment.
- Never depend on an unverified value for engine protection or safe vehicle operation.
- MackoDash must not replace required factory safety systems, warning indicators, or legally required instrumentation.
- Do not operate the settings interface or troubleshoot the dashboard while driving.

## For Developers

This repository contains the ESP32-P4 firmware and development sources. Customer downloads belong in [MackoDash Flash Tools](https://github.com/mackotuned/MackoDash-Flash-Tools).

Requirements:

- Visual Studio Code with the Espressif ESP-IDF extension
- ESP-IDF 5.5.x
- ESP32-P4 toolchain
- USB data connection to the CrowPanel

Build workflow:

1. Clone this repository and open it in Visual Studio Code.
2. Configure the ESP-IDF extension for ESP-IDF 5.5.x.
3. Set the target to `esp32p4`.
4. Select the CrowPanel ESP32-P4 USB serial port.
5. Build, flash, and monitor with the ESP-IDF extension.

The project uses [partitions.csv](partitions.csv), with two 6 MiB application slots.

### Repository Layout

- `main/` - application, UI, settings, themes, simulation, and update logic
- `main/canbus/` - CAN driver, protocol loader, and protocol definitions
- `main/odometer/` - persistent odometer and trip storage
- `components/` - CrowPanel board support and local components
- `spiffs/` - files packed into the SPIFFS partition
- `workspace_addons/` - internal utility and theme-tool development sources

## Project Status

MackoDash is under active development. Firmware changes are built, flashed, and tested on the 9-inch CrowPanel before publication.

This project is provided for personal and educational use. Commercial use is not permitted without authorization.
