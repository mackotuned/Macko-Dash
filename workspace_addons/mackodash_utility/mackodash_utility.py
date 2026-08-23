from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk


ADDONS_ROOT = Path(__file__).resolve().parent.parent
THEME_PACKAGER = ADDONS_ROOT / "theme_packager"
UPDATE_FLASHER = ADDONS_ROOT / "update_flasher"
for module_path in (ADDONS_ROOT, THEME_PACKAGER, UPDATE_FLASHER):
    path_text = str(module_path)
    if path_text not in sys.path:
        sys.path.insert(0, path_text)

from mackodash_theme_builder import ThemeBuilderFrame, configure_theme_builder_style
from mackodash_update_flasher import UpdateFlasherFrame, configure_update_flasher_style
from utility_ui import (
    LINE,
    PANEL,
    RED,
    VOID,
    WHITE,
    VerticalScrollFrame,
    build_brand_header,
    configure_mackodash_style,
)


class MackoDashUtility(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("MackoDash Utility")
        self.geometry("940x840")
        self.minsize(800, 720)
        self.configure(background=VOID)

        configure_mackodash_style(self)
        configure_theme_builder_style(self)
        configure_update_flasher_style(self)

        self.content = ttk.Frame(self)
        self.content.pack(fill="both", expand=True)

        self.home_page = ttk.Frame(self.content, padding=(34, 24))
        self.home_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self._build_home()

        self.theme_page = self._build_workflow_page("Build a Theme")
        self.update_page = self._build_workflow_page("Update Firmware")
        self.theme_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.update_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.theme_builder = ThemeBuilderFrame(self.theme_page.workflow.content)
        self.update_flasher = UpdateFlasherFrame(self.update_page.workflow.content)
        self.theme_builder.pack(fill="both", expand=True)
        self.update_flasher.pack(fill="both", expand=True)
        self._show_page(self.home_page)
        self.protocol("WM_DELETE_WINDOW", self._close)

    def _build_home(self) -> None:
        header = build_brand_header(
            self.home_page,
            "MackoDash Utility",
            "Choose what you want to do. Each tool guides you through the job one step at a time.",
        )
        header.pack(fill="x", pady=(0, 26))

        choices = ttk.Frame(self.home_page)
        choices.pack(fill="both", expand=True)
        choices.columnconfigure((0, 1), weight=1, uniform="tools")
        choices.rowconfigure(0, weight=1)
        self._build_home_tile(
            choices,
            0,
            "UPDATE FIRMWARE",
            "Install an official complete MackoDash update over USB.",
            "Firmware ZIP + USB cable",
            lambda: self._show_page(self.update_page),
        )
        self._build_home_tile(
            choices,
            1,
            "BUILD A THEME",
            "Convert a SquareLine design and copy it to an SD card.",
            "SquareLine ZIP + SD card",
            lambda: self._show_page(self.theme_page),
        )

    def _build_home_tile(
        self,
        parent: tk.Misc,
        column: int,
        title: str,
        description: str,
        requirement: str,
        command,
    ) -> None:
        tile = tk.Frame(parent, background=PANEL, highlightbackground=LINE, highlightthickness=1, cursor="hand2")
        tile.grid(row=0, column=column, sticky="nsew", padx=(0, 10) if column == 0 else (10, 0))
        accent = tk.Frame(tile, background=RED, height=7)
        accent.pack(fill="x")
        body = tk.Frame(tile, background=PANEL, padx=28, pady=28)
        body.pack(fill="both", expand=True)
        number = tk.Label(
            body, text=f"0{column + 1}", background=RED, foreground=WHITE,
            font=("Segoe UI Semibold", 12), padx=10, pady=6,
        )
        number.pack(anchor="w")
        heading = tk.Label(
            body, text=title, background=PANEL, foreground=WHITE,
            font=("Segoe UI Semibold", 20), justify="left",
        )
        heading.pack(anchor="w", pady=(24, 10))
        copy = tk.Label(
            body, text=description, background=PANEL, foreground="#b0b2b8",
            font=("Segoe UI", 11), wraplength=310, justify="left",
        )
        copy.pack(anchor="w")
        need = tk.Label(
            body, text=f"YOU NEED\n{requirement}", background=PANEL, foreground="#84868d",
            font=("Segoe UI Semibold", 9), justify="left",
        )
        need.pack(anchor="w", side="bottom")
        for widget in (tile, accent, body, number, heading, copy, need):
            widget.bind("<Button-1>", lambda _event, action=command: action())

    def _build_workflow_page(self, title: str) -> ttk.Frame:
        page = ttk.Frame(self.content)
        toolbar = ttk.Frame(page, padding=(20, 10))
        toolbar.pack(fill="x")
        ttk.Button(toolbar, text="Home", style="Quiet.TButton", command=lambda: self._show_page(self.home_page)).pack(
            side="left"
        )
        ttk.Label(toolbar, text=title, style="Subtitle.TLabel").pack(side="left", padx=(12, 0))
        workflow = VerticalScrollFrame(page)
        workflow.pack(fill="both", expand=True)
        page.workflow = workflow
        return page

    def _show_page(self, page: ttk.Frame) -> None:
        page.tkraise()

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