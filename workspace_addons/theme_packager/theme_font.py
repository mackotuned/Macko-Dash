from __future__ import annotations

import ctypes
import sys
from pathlib import Path


PREVIEW_FONT = "Montserrat SemiBold"


def register_preview_font() -> None:
    if sys.platform != "win32":
        return
    candidates = (
        Path(getattr(sys, "_MEIPASS", "")) / "Montserrat-SemiBold.ttf",
        Path(__file__).resolve().parents[2] / "main" / "fonts" / "Montserrat-SemiBold.ttf",
    )
    for path in candidates:
        if path.is_file():
            ctypes.windll.gdi32.AddFontResourceExW(str(path), 0x10, 0)
            return
