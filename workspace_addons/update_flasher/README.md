# MackoDash USB Update Flasher

`MackoDashUpdateFlasher.exe` updates only the ESP32-P4 dashboard application
over its USB serial connection. Customers do not need Python or ESP-IDF.

## Customer workflow

1. Connect MackoDash to the Windows PC using its update USB port.
2. Open `MackoDashUpdateFlasher.exe`.
3. The included `mackodash.bin` is validated automatically. Use **Browse** only
  when installing a newer official firmware file.
4. Confirm the automatically selected dashboard COM port.
5. Select **Flash ESP32-P4 Update** and keep USB and power connected.
6. Wait for hash verification and the success message.

The flasher rejects damaged files, wrong-chip images, oversized applications,
and ESP-IDF images whose project name is not `mackodash`. It forces the target
to ESP32-P4 and writes only the application at `0x20000`.

It does not erase or write NVS, OTA metadata, bootloader, partition table,
SPIFFS storage, SD-card themes, or ESP32-C6 firmware.

## Developer validation

```powershell
& 'C:\Python314\python.exe' -m unittest discover `
  -s workspace_addons\update_flasher -p 'test_*.py' -v
```

Build the standalone executable with `build_windows.ps1`. The pinned runtime
dependencies are listed in `requirements.txt`. The build script places the EXE
and the current `mackodash.bin` together in `dist`; distribute both files in the
same folder so the firmware loads automatically.