# MackoDash Utility

MackoDash Utility combines the customer Theme Builder and ESP32-P4 USB Update
Flasher in one standalone Windows application with top tabs.

- **Theme Builder** converts validated SquareLine C exports to `.mdtheme.zip`
  packages and copies them to the SD card.
- **Firmware Update** validates and flashes only the MackoDash ESP32-P4 app at
  `0x20000`, preserving settings, themes, storage, and ESP32-C6 firmware.

Run from source with `C:\Python314\python.exe mackodash_utility.py`. Build the
standalone customer distribution with `build_windows.ps1`. The existing
standalone Theme Builder and Update Flasher remain available and unchanged.