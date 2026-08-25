# MackoDash Utility

MackoDash Utility combines the customer Theme Builder, ESP32-P4 USB Update
Flasher, and Driving Log Viewer in one Windows application. Its Home screen
presents three task tiles so customers choose a job before seeing any controls.

- **Theme Builder** converts validated SquareLine C exports to `.mdtheme.zip`
  packages, previews simulated dashboard values, and installs them to the
  dashboard SD card directly or through the dashboard USB cable.
- **Firmware Update** validates and flashes the complete MackoDash ESP32-P4
  firmware ZIP: bootloader, partition table, OTA metadata, application, and
  SPIFFS. NVS settings, odometer data, SD-card themes, and ESP32-C6 firmware
  remain untouched.
- **Driving Log Viewer** opens a MackoDash CSV log, finds logs on a removable
  SD card, or downloads and verifies logs through the dashboard USB cable. It
  displays horizontally scrollable dyno-style traces with exact hover values,
  tuning presets, and custom channel selection.

USB file transfer uses the dashboard's existing serial update cable. The SD
card remains managed by the dashboard and does not appear as a Windows drive.
An SD card must be inserted and mounted before logs can be downloaded or themes
can be installed.

Run from source with `C:\Python314\python.exe mackodash_utility.py`. Build the
customer distribution with `build_windows.ps1`. `MackoDashUtility.exe` is the
only customer application; the workflow modules are internal components.