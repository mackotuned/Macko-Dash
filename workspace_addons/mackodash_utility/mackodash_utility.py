from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk


ADDONS_ROOT = Path(__file__).resolve().parent.parent
THEME_PACKAGER = ADDONS_ROOT / "theme_packager"
UPDATE_FLASHER = ADDONS_ROOT / "update_flasher"
for module_path in (THEME_PACKAGER, UPDATE_FLASHER):
    path_text = str(module_path)
    if path_text not in sys.path:
        sys.path.insert(0, path_text)

from mackodash_theme_builder import ThemeBuilderFrame, configure_theme_builder_style
from mackodash_update_flasher import UpdateFlasherFrame, configure_update_flasher_style


class MackoDashUtility(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("MackoDash Utility")
        self.geometry("900x720")
        self.minsize(780, 640)
        self.configure(background="#101214")

        configure_theme_builder_style(self)
        configure_update_flasher_style(self)
        self._configure_notebook_style()

        self.notebook = ttk.Notebook(self, style="Utility.TNotebook")
        self.notebook.pack(fill="both", expand=True, padx=14, pady=14)

        self.theme_builder = ThemeBuilderFrame(self.notebook)
        self.update_flasher = UpdateFlasherFrame(self.notebook)
        self.notebook.add(self.theme_builder, text="  Theme Builder  ")
        self.notebook.add(self.update_flasher, text="  Firmware Update  ")
        self.protocol("WM_DELETE_WINDOW", self._close)

    def _configure_notebook_style(self) -> None:
        style = ttk.Style(self)
        style.configure("Utility.TNotebook", background="#101214", borderwidth=0, tabmargins=(0, 0, 0, 8))
        style.configure(
            "Utility.TNotebook.Tab",
            background="#1b1f23",
            foreground="#a5abb3",
            font=("Segoe UI Semibold", 11),
            padding=(18, 11),
            borderwidth=0,
        )
        style.map(
            "Utility.TNotebook.Tab",
            background=[("selected", "#e22936"), ("active", "#2b3036")],
            foreground=[("selected", "#ffffff"), ("active", "#ffffff")],
        )

    def _close(self) -> None:
        if self.update_flasher._busy:
            messagebox.showwarning(
                "Update in progress",
                "Do not close MackoDash Utility until flash verification finishes.",
            )
            return
        self.destroy()


if __name__ == "__main__":
    MackoDashUtility().mainloop()