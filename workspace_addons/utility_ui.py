from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import ttk


VOID = "#0c0d10"
PANEL = "#17191e"
PANEL_ACTIVE = "#22252c"
LINE = "#30343d"
SURFACE = "#111318"
SURFACE_ACTIVE = "#292d35"
RED = "#e4002b"
RED_ACTIVE = "#ff203f"
RED_DEEP = "#4a0413"
WHITE = "#f4f3ef"
AMBER = "#ffb020"
GREEN = "#39ff8c"
LABEL = "#a1a6b0"


class VerticalScrollFrame(ttk.Frame):
    def __init__(self, parent: tk.Misc) -> None:
        super().__init__(parent)
        self.canvas = tk.Canvas(self, background=VOID, borderwidth=0, highlightthickness=0)
        scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.content = ttk.Frame(self.canvas)
        self._window = self.canvas.create_window((0, 0), window=self.content, anchor="nw")
        self.canvas.configure(yscrollcommand=scrollbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.content.bind("<Configure>", self._sync_scroll_region)
        self.canvas.bind("<Configure>", self._sync_content_width)
        self.canvas.bind("<Enter>", self._bind_mousewheel)
        self.canvas.bind("<Leave>", self._unbind_mousewheel)

    def _sync_scroll_region(self, _event: tk.Event) -> None:
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def _sync_content_width(self, event: tk.Event) -> None:
        self.canvas.itemconfigure(self._window, width=event.width)

    def _bind_mousewheel(self, _event: tk.Event) -> None:
        self.canvas.bind_all("<MouseWheel>", self._on_mousewheel)

    def _unbind_mousewheel(self, _event: tk.Event) -> None:
        self.canvas.unbind_all("<MouseWheel>")

    def _on_mousewheel(self, event: tk.Event) -> None:
        self.canvas.yview_scroll(-int(event.delta / 120), "units")


class RoundedPanel(tk.Canvas):
    def __init__(self, parent: tk.Misc, *, fill: str = PANEL, radius: int = 12, padding: int = 6) -> None:
        super().__init__(parent, background=VOID, borderwidth=0, highlightthickness=0)
        self.fill = fill
        self.radius = radius
        self.padding = padding
        self._shape = self.create_polygon(0, 0, fill=fill, outline=LINE, smooth=True)
        self.content = tk.Frame(self, background=fill)
        self._window = self.create_window(padding, padding, window=self.content, anchor="nw")
        self.bind("<Configure>", self._resize)
        self.content.bind("<Configure>", self._fit_content)

    def _fit_content(self, event: tk.Event) -> None:
        self.configure(height=event.height + self.padding * 2)

    def _resize(self, event: tk.Event) -> None:
        padding = self.padding
        width = max(1, event.width - padding * 2)
        height = max(1, event.height - padding * 2)
        self.itemconfigure(self._window, width=width, height=height)
        radius = min(self.radius, width // 2, height // 2)
        x0, y0, x1, y1 = padding, padding, event.width - padding, event.height - padding
        points = (
            x0 + radius, y0, x1 - radius, y0, x1, y0, x1, y0 + radius,
            x1, y1 - radius, x1, y1, x1 - radius, y1, x0 + radius, y1,
            x0, y1, x0, y1 - radius, x0, y0 + radius, x0, y0,
        )
        self.coords(self._shape, *points)


def asset_path(filename: str) -> Path:
    if getattr(sys, "frozen", False):
        return Path(getattr(sys, "_MEIPASS")) / filename
    return Path(__file__).resolve().parent / filename


def configure_mackodash_style(root: tk.Misc) -> None:
    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure("TFrame", background=VOID)
    style.configure("TLabel", background=VOID, foreground=WHITE, font=("Segoe UI Variable Text", 10))
    style.configure("Title.TLabel", background=VOID, foreground=WHITE, font=("Segoe UI Variable Display Semibold", 24))
    style.configure("Subtitle.TLabel", background=VOID, foreground=LABEL, font=("Segoe UI Variable Text", 10))
    style.configure("Section.TLabel", background=PANEL, foreground=LABEL, font=("Segoe UI Variable Text Semibold", 9))
    style.configure("Tile.TFrame", background=PANEL, borderwidth=0, relief="flat")
    style.configure("Surface.TFrame", background=SURFACE, borderwidth=0, relief="flat")
    style.configure("Surface.TLabel", background=SURFACE, foreground=LABEL, font=("Segoe UI Variable Text", 10))
    style.configure("Tile.TLabel", background=PANEL, foreground=WHITE, font=("Segoe UI Variable Text", 10))
    style.configure("TileTitle.TLabel", background=PANEL, foreground=WHITE, font=("Segoe UI Variable Display Semibold", 14))
    style.configure("TileHint.TLabel", background=PANEL, foreground=LABEL, font=("Segoe UI Variable Text", 9))
    style.configure("Good.Tile.TLabel", background=PANEL, foreground=GREEN, font=("Segoe UI Semibold", 10))
    style.configure("Warn.Tile.TLabel", background=PANEL, foreground=AMBER, font=("Segoe UI Semibold", 10))
    style.configure("TButton", background=PANEL_ACTIVE, foreground=WHITE, font=("Segoe UI Variable Text Semibold", 10), padding=(14, 10), borderwidth=0, relief="flat")
    style.map("TButton", background=[("active", SURFACE_ACTIVE), ("pressed", RED_DEEP), ("disabled", PANEL)], foreground=[("disabled", LABEL)])
    style.configure("Accent.TButton", background=RED, foreground="#ffffff", font=("Segoe UI Variable Text Semibold", 10), padding=(18, 11), borderwidth=0, relief="flat")
    style.map("Accent.TButton", background=[("active", RED_ACTIVE), ("disabled", RED_DEEP)], foreground=[("disabled", LABEL)])
    style.configure("Quiet.TButton", background=PANEL, foreground=LABEL, font=("Segoe UI Variable Text Semibold", 9), padding=(10, 7), borderwidth=0, relief="flat")
    style.map("Quiet.TButton", background=[("active", PANEL_ACTIVE)], foreground=[("active", WHITE)])
    style.configure("TEntry", fieldbackground=SURFACE, foreground=WHITE, insertcolor=WHITE, padding=9, borderwidth=1, bordercolor=LINE, lightcolor=LINE, darkcolor=LINE)
    style.map("TEntry", bordercolor=[("focus", RED)])
    style.configure("TCombobox", fieldbackground=SURFACE, foreground=WHITE, padding=9, borderwidth=1, bordercolor=LINE, arrowcolor=WHITE)
    style.map("TCombobox", fieldbackground=[("readonly", SURFACE)], foreground=[("readonly", WHITE)], bordercolor=[("focus", RED)])
    style.configure("TCheckbutton", background=PANEL, foreground=WHITE, font=("Segoe UI Variable Text", 9), padding=3)
    style.map("TCheckbutton", background=[("active", PANEL)], foreground=[("active", WHITE)], indicatorcolor=[("selected", RED)])
    style.configure("TScrollbar", background=PANEL_ACTIVE, troughcolor=SURFACE, borderwidth=0, arrowcolor=LABEL)
    style.map("TScrollbar", background=[("active", LINE)])
    style.configure("TNotebook", background=VOID, borderwidth=0, tabmargins=0)
    style.configure("TNotebook.Tab", background=PANEL, foreground=LABEL, padding=(18, 10), borderwidth=0)
    style.map("TNotebook.Tab", background=[("selected", PANEL_ACTIVE), ("active", SURFACE_ACTIVE)], foreground=[("selected", WHITE), ("active", WHITE)])


def build_brand_header(parent: tk.Misc, title: str, subtitle: str) -> tk.Frame:
    header = tk.Frame(parent, background=VOID, height=116)
    header.grid_propagate(False)
    header.columnconfigure(1, weight=1)

    logo_path = asset_path("mackodash_logo.png")
    if logo_path.is_file():
        logo = tk.PhotoImage(file=logo_path).subsample(5, 5)
        logo_label = tk.Label(header, image=logo, background=VOID, borderwidth=0)
        logo_label.image = logo
        logo_label.grid(row=0, column=0, rowspan=2, sticky="w", padx=(0, 22))

    ttk.Label(header, text=title, style="Title.TLabel").grid(row=0, column=1, sticky="sw", pady=(20, 0))
    ttk.Label(header, text=subtitle, style="Subtitle.TLabel", wraplength=620, justify="left").grid(
        row=1, column=1, sticky="nw", pady=(3, 16)
    )
    return header


def build_step_tile(parent: tk.Misc, step: int, title: str, hint: str) -> tuple[RoundedPanel, ttk.Frame]:
    tile = RoundedPanel(parent)
    content = tile.content
    content.configure(padx=18, pady=18)
    content.columnconfigure(1, weight=1)

    badge = tk.Label(
        content,
        text=f"{step:02d}",
        background=RED,
        foreground="#ffffff",
        font=("Segoe UI Semibold", 11),
        width=3,
        padx=3,
        pady=4,
    )
    badge.grid(row=0, column=0, rowspan=2, sticky="nw", padx=(0, 12))
    ttk.Label(content, text=title, style="TileTitle.TLabel").grid(row=0, column=1, sticky="w")
    ttk.Label(content, text=hint, style="TileHint.TLabel", wraplength=570, justify="left").grid(
        row=1, column=1, sticky="w", pady=(1, 0)
    )

    body = ttk.Frame(content, style="Tile.TFrame")
    body.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(15, 0))
    body.columnconfigure(0, weight=1)
    return tile, body


def set_details_visible(button: ttk.Button, frame: tk.Widget, visible: bool) -> None:
    if visible:
        frame.grid()
        button.configure(text="Hide technical details")
    else:
        frame.grid_remove()
        button.configure(text="Show technical details")
