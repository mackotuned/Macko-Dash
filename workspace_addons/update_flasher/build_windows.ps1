$ErrorActionPreference = 'Stop'
$python = 'C:\Python314\python.exe'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Resolve-Path (Join-Path $root '..\..')
$buildDir = Join-Path $projectRoot 'build'
$firmwareBundle = Join-Path $root 'dist\MackoDash-Firmware.zip'

if (-not (Test-Path $python)) {
    throw "Python with Tkinter was not found at $python"
}
if (-not (Test-Path (Join-Path $buildDir 'mackodash.bin'))) {
    throw "Build the complete MackoDash firmware first; $buildDir is incomplete"
}

& $python -c "import tkinter, esptool, serial; print('Tk', tkinter.Tcl().call('info', 'patchlevel')); print('esptool', esptool.__version__)"
& $python -m PyInstaller --noconfirm --clean --onefile --windowed `
    --name MackoDashUpdateFlasher `
    --collect-all esptool `
    --collect-all serial `
    --distpath (Join-Path $root 'dist') `
    --workpath (Join-Path $root 'build') `
    --specpath $root `
    (Join-Path $root 'mackodash_update_flasher.py')

& $python (Join-Path $root 'build_firmware_bundle.py') $buildDir $firmwareBundle
Remove-Item (Join-Path $root 'dist\*.bin') -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root 'CUSTOMER_INSTRUCTIONS.txt') (Join-Path $root 'dist\CUSTOMER_INSTRUCTIONS.txt') -Force

Write-Host "Built customer files in $(Join-Path $root 'dist')"