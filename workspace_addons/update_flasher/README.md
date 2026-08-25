# MackoDash USB Update Flasher

The Firmware Update workflow inside `MackoDashUtility.exe` installs a validated
complete ESP32-P4 firmware bundle over USB. Customers do not need Python or
ESP-IDF.

## Customer workflow

1. Connect MackoDash to the Windows PC using its update USB port.
2. Open MackoDash Utility and choose **Update Firmware**.
3. Select the official `MackoDash-Firmware.zip` downloaded from GitHub. An
   adjacent bundle is validated automatically when included with the EXE.
4. Confirm the automatically selected dashboard COM port.
5. Select **Install Firmware** and keep USB and power connected.
6. Wait for hash verification and the success message.

The three numbered tiles guide customers through update selection, USB
connection, and installation. Flash logs are available under **Show technical
details** when troubleshooting is needed.

The ZIP contains the bootloader, partition table, OTA metadata, application,
and SPIFFS storage images plus a checksum manifest. The flasher rejects missing,
damaged, unexpected, oversized, wrong-offset, wrong-chip, and wrong-project
bundles before enabling the flash button. The required files may be at the ZIP
root or inside one top-level folder, as produced by GitHub downloads.

The full flash replaces ESP32-P4 SPIFFS storage. It does not erase or write NVS,
so settings and odometer data remain intact. SD-card themes and ESP32-C6 firmware
are not touched.

## Developer validation

```powershell
& 'C:\Python314\python.exe' -m unittest discover `
  -s workspace_addons\update_flasher -p 'test_*.py' -v
```

The customer executable is `MackoDashUtility.exe`; there is no separate updater
download. Runtime dependencies remain listed in `requirements.txt` for the
combined build.