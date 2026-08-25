# MackoDash Utility

MackoDash Utility combines the customer Theme Builder, ESP32-P4 USB Update
Flasher, and Driving Log Viewer in one Windows application. Its Home screen
presents three task tiles so customers choose a job before seeing any controls.

- **Theme Builder** converts validated SquareLine C exports to `.mdtheme.zip`
  packages, previews simulated dashboard values, and copies them to the SD card.
- **Firmware Update** validates and flashes the complete MackoDash ESP32-P4
  firmware ZIP: bootloader, partition table, OTA metadata, application, and
  SPIFFS. NVS settings, odometer data, SD-card themes, and ESP32-C6 firmware
  remain untouched.
- **Driving Log Viewer** opens a MackoDash CSV log directly or finds logs on an
  SD card, then displays horizontally scrollable dyno-style traces with exact
  hover values, tuning presets, and custom channel selection.

Run from source with `C:\Python314\python.exe mackodash_utility.py`. Build the
customer distribution with `build_windows.ps1`. `MackoDashUtility.exe` is the
only customer application; the workflow modules are internal components.