$ErrorActionPreference = 'Stop'
$python = 'C:\Python314\python.exe'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$addonsRoot = Resolve-Path (Join-Path $root '..')
$projectRoot = Resolve-Path (Join-Path $root '..\..')
$themePackager = Join-Path $addonsRoot 'theme_packager'
$updateFlasher = Join-Path $addonsRoot 'update_flasher'
$firmware = Join-Path $projectRoot 'build\mackodash.bin'
$themeInstructions = Join-Path $addonsRoot 'references\MackoDash_SquareLine_Customer_Instructions.txt'

if (-not (Test-Path $python)) {
    throw "Python with Tkinter was not found at $python"
}
if (-not (Test-Path $firmware)) {
    throw "Build the MackoDash firmware first; $firmware was not found"
}

& $python -c "import tkinter, esptool, serial; print('Tk', tkinter.Tcl().call('info', 'patchlevel')); print('esptool', esptool.__version__)"
& $python -m PyInstaller --noconfirm --clean --onefile --windowed `
    --name MackoDashUtility `
    --paths $themePackager `
    --paths $updateFlasher `
    --collect-all esptool `
    --collect-all serial `
    --distpath (Join-Path $root 'dist') `
    --workpath (Join-Path $root 'build') `
    --specpath $root `
    (Join-Path $root 'mackodash_utility.py')

Copy-Item $firmware (Join-Path $root 'dist\mackodash.bin') -Force
Copy-Item (Join-Path $root 'CUSTOMER_INSTRUCTIONS.txt') (Join-Path $root 'dist\CUSTOMER_INSTRUCTIONS.txt') -Force
Copy-Item $themeInstructions (Join-Path $root 'dist\MackoDash_SquareLine_Customer_Instructions.txt') -Force

Write-Host "Built MackoDash Utility customer files in $(Join-Path $root 'dist')"