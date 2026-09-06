# MackoDash Utility

MackoDash Utility combines Theme Studio, the SquareLine Theme Builder, Boot Logo
Manager, ESP32-P4 USB Update Flasher, and Driving Log Viewer in one Windows application. Its Home
screen presents five task tiles so customers choose a job before seeing any
controls.

- **Theme Studio** creates dashboard themes directly on a 1024x600 visual
  canvas. Users can add and drag supported LVGL 8.4 controls, edit geometry and
  styles, resize with a canvas handle, reorder layers, use grid snapping, and
  zoom from 50% to 200% or fit the full design to the workbench. The contextual
  inspector includes arc start/end angles, bar and arc preview values,
  live bar range/opacity/radius/rotation, image-backed bar fills, label alignment,
  image zoom, and live ECU bindings. Montserrat text supports continuous runtime
  sizes from 8px through 200px for labels, gauge values, and tick labels.
  Native needle widgets support custom ranges, angles, length, width, and color.
  Analog tach widgets add configurable dial faces, borders, arcs, minor and major
  ticks, numeric scale labels, needles, and optional center values.
  Analog speedometer widgets provide the same controls with speed-specific
  defaults. Every color-backed widget part can be made transparent independently.
  The canvas opens centered in a padded workbench, supports wheel scrolling,
  Shift+wheel horizontal scrolling, cursor-centered zoom, and middle-button panning.
  Typical or longest values can be simulated, and Performance or Street
  templates provide starting points. PNG and GIF assets can be embedded directly
  into portable project files. Projects support undo/redo, save/load, direct
  dashboard-ready `.mdtheme.zip` export, and USB installation.
- **Theme Builder** converts validated SquareLine C exports to `.mdtheme.zip`
  packages, previews simulated dashboard values, and installs them to the
  dashboard SD card directly or through the dashboard USB cable.
- **Boot Logo** accepts PNG and JPEG images, previews Fit or Fill composition
  against a selectable background color, exports the validated 1024x600 RGB565
  `.mdlogo` format, and installs it over USB. Installed logos are selected or
  deleted under **Settings > Display > Boot Logo**; the built-in logo remains as
  the permanent fallback.
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