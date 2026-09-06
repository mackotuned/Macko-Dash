from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import messagebox, ttk


ADDONS_ROOT = Path(__file__).resolve().parent.parent
THEME_PACKAGER = ADDONS_ROOT / "theme_packager"
UPDATE_FLASHER = ADDONS_ROOT / "update_flasher"
LOG_VIEWER = ADDONS_ROOT / "log_viewer"
for module_path in (ADDONS_ROOT, THEME_PACKAGER, UPDATE_FLASHER, LOG_VIEWER):
    path_text = str(module_path)
    if path_text not in sys.path:
        sys.path.insert(0, path_text)

from mackodash_theme_builder import ThemeBuilderFrame, configure_theme_builder_style
from mackodash_theme_studio import ThemeStudioFrame
from boot_logo_manager import BootLogoManagerFrame
from mackodash_log_viewer import LogViewerFrame
from mackodash_update_flasher import UpdateFlasherFrame, configure_update_flasher_style
from utility_ui import (
    LABEL,
    LINE,
    PANEL,
    PANEL_ACTIVE,
    RED,
    RoundedPanel,
    VOID,
    WHITE,
    VerticalScrollFrame,
    build_brand_header,
    configure_mackodash_style,
)


class MackoDashUtility(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("MackoDash Utility 2.1")
        self.geometry("1440x900")
        self.minsize(1180, 740)
        self.configure(background=VOID)

        configure_mackodash_style(self)
        configure_theme_builder_style(self)
        configure_update_flasher_style(self)

        self.shell = ttk.Frame(self)
        self.shell.pack(fill="both", expand=True)
        self.sidebar = tk.Frame(self.shell, background="#111318", width=196)
        self.sidebar.pack(side="left", fill="y")
        self.sidebar.pack_propagate(False)
        self.content = ttk.Frame(self.shell)
        self.content.pack(side="right", fill="both", expand=True)

        self.home_page = ttk.Frame(self.content, padding=(34, 24))
        self.home_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self._build_home()

        self.theme_page = self._build_workflow_page("Build a Theme")
        self.studio_page = self._build_workflow_page("Theme Studio", scroll=False)
        self.update_page = self._build_workflow_page("Update Firmware")
        self.log_page = self._build_workflow_page("View Driving Logs")
        self.logo_page = self._build_workflow_page("Boot Logo", scroll=False)
        self.theme_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.studio_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.update_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.log_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.logo_page.place(relx=0, rely=0, relwidth=1, relheight=1)
        self.theme_builder = ThemeBuilderFrame(self.theme_page.workflow.content)
        self.theme_studio = ThemeStudioFrame(self.studio_page.workflow)
        self.update_flasher = UpdateFlasherFrame(self.update_page.workflow.content)
        self.log_viewer = LogViewerFrame(self.log_page.workflow.content)
        self.logo_manager = BootLogoManagerFrame(self.logo_page.workflow)
        self.theme_builder.pack(fill="both", expand=True)
        self.theme_studio.pack(fill="both", expand=True)
        self.update_flasher.pack(fill="both", expand=True)
        self.log_viewer.pack(fill="both", expand=True)
        self.logo_manager.pack(fill="both", expand=True)
        self._build_sidebar()
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
        choices.rowconfigure((0, 1, 2), weight=1, uniform="tools")
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
            "THEME STUDIO",
            "Design a MackoDash theme on a drag-and-drop canvas.",
            "No SquareLine project required",
            lambda: self._show_page(self.studio_page),
        )
        self._build_home_tile(
            choices,
            2,
            "BUILD A THEME",
            "Convert a SquareLine design and copy it to an SD card.",
            "SquareLine ZIP + SD card",
            lambda: self._show_page(self.theme_page),
        )
        self._build_home_tile(
            choices,
            3,
            "BOOT LOGO",
            "Turn a PNG or JPEG into a dashboard startup logo and send it over USB.",
            "Image file + USB cable",
            lambda: self._show_page(self.logo_page),
        )
        self._build_home_tile(
            choices,
            4,
            "VIEW DRIVING LOGS",
            "Explore recorded sessions with selectable dyno-style graphs.",
            "MackoDash CSV log or SD card",
            lambda: self._show_page(self.log_page),
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
        tile = RoundedPanel(parent, radius=14)
        tile.configure(cursor="hand2")
        row, grid_column = divmod(column, 2)
        tile.grid(row=row, column=grid_column, sticky="nsew", padx=(0, 7) if grid_column == 0 else (7, 0),
              pady=(0, 7) if row == 0 else (7, 0))
        body = tile.content
        accent = tk.Frame(body, background=RED, height=5)
        accent.pack(fill="x")
        card = tk.Frame(body, background=PANEL, padx=28, pady=24)
        card.pack(fill="both", expand=True)
        number = tk.Label(
            card, text=f"0{column + 1}", background=RED, foreground=WHITE,
            font=("Segoe UI Variable Text Semibold", 11), padx=10, pady=6,
        )
        number.pack(anchor="w")
        heading = tk.Label(
            card, text=title, background=PANEL, foreground=WHITE,
            font=("Segoe UI Variable Display Semibold", 16), justify="left", wraplength=250,
        )
        heading.pack(anchor="w", pady=(18, 8))
        copy = tk.Label(
            card, text=description, background=PANEL, foreground="#b0b2b8",
            font=("Segoe UI Variable Text", 10), wraplength=250, justify="left",
        )
        copy.pack(anchor="w")
        need = tk.Label(
            card, text=f"YOU NEED\n{requirement}", background=PANEL, foreground=LABEL,
            font=("Segoe UI Variable Text Semibold", 9), justify="left",
        )
        need.pack(anchor="w", side="bottom")
        for widget in (tile, body, accent, card, number, heading, copy, need):
            widget.bind("<Button-1>", lambda _event, action=command: action())

    def _build_sidebar(self) -> None:
        brand = tk.Frame(self.sidebar, background="#111318", padx=18, pady=22)
        brand.pack(fill="x")
        tk.Label(brand, text="M", background=RED, foreground=WHITE,
                 font=("Segoe UI Variable Display Semibold", 18), width=2, pady=4).pack(side="left")
        tk.Label(brand, text="MACKODASH\nUTILITY", background="#111318", foreground=WHITE,
                 font=("Segoe UI Variable Text Semibold", 10), justify="left").pack(side="left", padx=(10, 0))
        tk.Frame(self.sidebar, background=LINE, height=1).pack(fill="x", padx=14, pady=(0, 12))
        navigation = (
            ("Home", self.home_page),
            ("Theme Studio", self.studio_page),
            ("Firmware Update", self.update_page),
            ("Theme Builder", self.theme_page),
            ("Boot Logo", self.logo_page),
            ("Driving Logs", self.log_page),
        )
        self.nav_buttons = {}
        for label, page in navigation:
            button = tk.Button(
                self.sidebar, text=label, anchor="w", command=lambda target=page: self._show_page(target),
                background="#111318", activebackground=PANEL_ACTIVE, foreground=LABEL,
                activeforeground=WHITE, font=("Segoe UI Variable Text Semibold", 10),
                padx=20, pady=12, borderwidth=0, relief="flat", cursor="hand2",
            )
            button.pack(fill="x", padx=8, pady=2)
            self.nav_buttons[page] = button
        tk.Label(self.sidebar, text="MackoDash  |  LVGL 8.4", background="#111318", foreground="#606570",
                 font=("Segoe UI Variable Text", 8)).pack(side="bottom", pady=18)

    def _build_workflow_page(self, title: str, scroll: bool = True) -> ttk.Frame:
        page = ttk.Frame(self.content)
        toolbar = ttk.Frame(page, padding=(24, 14))
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text=title, style="Title.TLabel").pack(side="left")
        workflow = VerticalScrollFrame(page) if scroll else ttk.Frame(page)
        workflow.pack(fill="both", expand=True)
        page.workflow = workflow
        return page

    def _show_page(self, page: ttk.Frame) -> None:
        page.tkraise()
        for target, button in getattr(self, "nav_buttons", {}).items():
            selected = target is page
            button.configure(background=RED if selected else "#111318",
                             foreground=WHITE if selected else LABEL)

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