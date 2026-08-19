$ErrorActionPreference = 'Stop'
$python = 'C:\Python314\python.exe'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not (Test-Path $python)) {
    throw "Python with Tkinter was not found at $python"
}

& $python -c "import tkinter; print('Tk', tkinter.Tcl().call('info', 'patchlevel'))"
& $python -m PyInstaller --noconfirm --clean --onefile --windowed `
    --name MackoDashThemeBuilder `
    --distpath (Join-Path $root 'dist') `
    --workpath (Join-Path $root 'build') `
    --specpath $root `
    (Join-Path $root 'mackodash_theme_builder.py')

Write-Host "Built $(Join-Path $root 'dist\MackoDashThemeBuilder.exe')"