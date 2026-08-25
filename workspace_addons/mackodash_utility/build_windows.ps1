$ErrorActionPreference = 'Stop'
$python = 'C:\Python314\python.exe'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonsRoot = Resolve-Path (Join-Path $root '..')
$projectRoot = Resolve-Path (Join-Path $root '..\..')
$themePackager = Join-Path $addonsRoot 'theme_packager'
$updateFlasher = Join-Path $addonsRoot 'update_flasher'
$logViewer = Join-Path $addonsRoot 'log_viewer'
$buildDir = Join-Path $projectRoot 'build'
$firmwareBundle = Join-Path $root 'dist\MackoDash-Firmware.zip'
$themeInstructions = Join-Path $addonsRoot 'references\MackoDash_SquareLine_Customer_Instructions.txt'

if (-not (Test-Path $python)) {
    throw "Python with Tkinter was not found at $python"
}
if (-not (Test-Path (Join-Path $buildDir 'mackodash.bin'))) {
    throw "Build the complete MackoDash firmware first; $buildDir is incomplete"
}

& $python -c "import tkinter, esptool, serial; print('Tk', tkinter.Tcl().call('info', 'patchlevel')); print('esptool', esptool.__version__)"
& $python -m PyInstaller --noconfirm --clean --onefile --windowed `
    --name MackoDashUtility `
    --paths $addonsRoot `
    --paths $themePackager `
    --paths $updateFlasher `
    --paths $logViewer `
    --add-data "$(Join-Path $addonsRoot 'mackodash_logo.png');." `
    --collect-all esptool `
    --collect-all serial `
    --distpath (Join-Path $root 'dist') `
    --workpath (Join-Path $root 'build') `
    --specpath $root `
    (Join-Path $root 'mackodash_utility.py')

& $python (Join-Path $updateFlasher 'build_firmware_bundle.py') $buildDir $firmwareBundle
Remove-Item (Join-Path $root 'dist\*.bin') -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $root 'CUSTOMER_INSTRUCTIONS.txt') (Join-Path $root 'dist\CUSTOMER_INSTRUCTIONS.txt') -Force
Copy-Item $themeInstructions (Join-Path $root 'dist\MackoDash_SquareLine_Customer_Instructions.txt') -Force

Write-Host "Built MackoDash Utility customer files in $(Join-Path $root 'dist')"