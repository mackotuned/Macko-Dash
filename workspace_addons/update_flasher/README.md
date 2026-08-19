# MackoDash USB Update Flasher

`MackoDashUpdateFlasher.exe` installs a validated complete ESP32-P4 firmware
bundle over USB. Customers do not need Python or ESP-IDF.

## Customer workflow

1. Connect MackoDash to the Windows PC using its update USB port.
2. Open `MackoDashUpdateFlasher.exe`.
3. Select the official `MackoDash-Firmware.zip` downloaded from GitHub. An
   adjacent bundle is validated automatically when included with the EXE.
4. Confirm the automatically selected dashboard COM port.
5. Select **Flash ESP32-P4 Firmware** and keep USB and power connected.
6. Wait for hash verification and the success message.

The ZIP contains the bootloader, partition table, OTA metadata, application,
and SPIFFS storage images plus a checksum manifest. The flasher rejects missing,
damaged, unexpected, oversized, wrong-offset, wrong-chip, and wrong-project
bundles before enabling the flash button.

The full flash replaces ESP32-P4 SPIFFS storage. It does not erase or write NVS,
so settings and odometer data remain intact. SD-card themes and ESP32-C6 firmware
are not touched.

## Developer validation

```powershell
& 'C:\Python314\python.exe' -m unittest discover `
  -s workspace_addons\update_flasher -p 'test_*.py' -v
```

Build the standalone executable with `build_windows.ps1`. The pinned runtime
dependencies are listed in `requirements.txt`. The build script creates the EXE
and a validated `MackoDash-Firmware.zip` in `dist` from the current ESP-IDF build.