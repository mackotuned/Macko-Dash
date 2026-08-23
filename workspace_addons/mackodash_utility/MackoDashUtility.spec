# -*- mode: python ; coding: utf-8 -*-
from PyInstaller.utils.hooks import collect_all

datas = [('C:\\Users\\mackb\\OneDrive\\Desktop\\FT550 esp\\mackodash_with_build\\workspace_addons\\mackodash_logo.png', '.')]
binaries = []
hiddenimports = []
tmp_ret = collect_all('esptool')
datas += tmp_ret[0]; binaries += tmp_ret[1]; hiddenimports += tmp_ret[2]
tmp_ret = collect_all('serial')
datas += tmp_ret[0]; binaries += tmp_ret[1]; hiddenimports += tmp_ret[2]


a = Analysis(
    ['C:\\Users\\mackb\\OneDrive\\Desktop\\FT550 esp\\mackodash_with_build\\workspace_addons\\mackodash_utility\\mackodash_utility.py'],
    pathex=['C:\\Users\\mackb\\OneDrive\\Desktop\\FT550 esp\\mackodash_with_build\\workspace_addons', 'C:\\Users\\mackb\\OneDrive\\Desktop\\FT550 esp\\mackodash_with_build\\workspace_addons\\theme_packager', 'C:\\Users\\mackb\\OneDrive\\Desktop\\FT550 esp\\mackodash_with_build\\workspace_addons\\update_flasher'],
    binaries=binaries,
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='MackoDashUtility',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
