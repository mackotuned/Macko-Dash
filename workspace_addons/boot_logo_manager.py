from __future__ import annotations

import tempfile
import tkinter as tk
from pathlib import Path
from tkinter import colorchooser, filedialog, messagebox, ttk

from PIL import ImageTk

from boot_logo_tool import BootLogoError, export_boot_logo, render_boot_logo, safe_logo_filename
from device_transfer_ui import open_boot_logo_upload
from utility_ui import LABEL, LINE, PANEL, RED, VOID, WHITE


class BootLogoManagerFrame(ttk.Frame):
    def __init__(self, parent: tk.Misc) -> None:
        super().__init__(parent, padding=24)
        self.source: Path | None = None
        self.preview_image: ImageTk.PhotoImage | None = None
        self.name = tk.StringVar(value="Creator Boot Logo")
        self.mode = tk.StringVar(value="fit")
        self.background = tk.StringVar(value="#000000")
        self.status = tk.StringVar(value="Choose a PNG or JPEG image.")
        self._build_ui()

    def _build_ui(self) -> None:
        self.columnconfigure(1, weight=1)
        self.rowconfigure(0, weight=1)

        controls = tk.Frame(self, background=PANEL, padx=24, pady=22, width=340)
        controls.grid(row=0, column=0, sticky="nsw", padx=(0, 18))
        controls.grid_propagate(False)
        tk.Label(controls, text="BOOT LOGO", background=PANEL, foreground=WHITE,
                 font=("Segoe UI Variable Display Semibold", 16)).pack(anchor="w")

        self._section_label(controls, "IMAGE").pack(anchor="w", pady=(24, 7))
        ttk.Button(controls, text="Choose PNG or JPEG", command=self._choose_image).pack(fill="x")

        self._section_label(controls, "DISPLAY NAME").pack(anchor="w", pady=(20, 7))
        ttk.Entry(controls, textvariable=self.name).pack(fill="x")

        self._section_label(controls, "LAYOUT").pack(anchor="w", pady=(20, 7))
        modes = ttk.Frame(controls)
        modes.pack(fill="x")
        ttk.Radiobutton(modes, text="Fit", value="fit", variable=self.mode,
                        command=self._refresh_preview).pack(side="left")
        ttk.Radiobutton(modes, text="Fill", value="fill", variable=self.mode,
                        command=self._refresh_preview).pack(side="left", padx=(18, 0))

        self._section_label(controls, "BACKGROUND").pack(anchor="w", pady=(20, 7))
        color_row = ttk.Frame(controls)
        color_row.pack(fill="x")
        self.color_button = tk.Button(color_row, width=4, background=self.background.get(),
                                      activebackground=self.background.get(), command=self._choose_color,
                                      relief="flat", borderwidth=1)
        self.color_button.pack(side="left")
        ttk.Label(color_row, textvariable=self.background).pack(side="left", padx=(10, 0))

        actions = ttk.Frame(controls)
        actions.pack(fill="x", side="bottom")
        ttk.Button(actions, text="Export .mdlogo", command=self._export).pack(fill="x")
        ttk.Button(actions, text="Send to Dashboard", style="Accent.TButton",
                   command=self._send).pack(fill="x", pady=(10, 0))

        preview_panel = tk.Frame(self, background=VOID, highlightbackground=LINE,
                                 highlightthickness=1, padx=18, pady=18)
        preview_panel.grid(row=0, column=1, sticky="nsew")
        preview_panel.columnconfigure(0, weight=1)
        preview_panel.rowconfigure(0, weight=1)
        self.preview = tk.Label(preview_panel, text="1024 × 600", background="#000000",
                                foreground=LABEL, font=("Segoe UI Variable Text Semibold", 12))
        self.preview.grid(row=0, column=0)
        ttk.Label(preview_panel, textvariable=self.status, style="TileHint.TLabel",
                  wraplength=680).grid(row=1, column=0, sticky="w", pady=(14, 0))

    @staticmethod
    def _section_label(parent: tk.Misc, text: str) -> tk.Label:
        return tk.Label(parent, text=text, background=PANEL, foreground=LABEL,
                        font=("Segoe UI Variable Text Semibold", 9))

    def _choose_image(self) -> None:
        selected = filedialog.askopenfilename(
            parent=self, title="Choose boot logo image",
            filetypes=[("Image files", "*.png *.jpg *.jpeg"), ("PNG", "*.png"),
                       ("JPEG", "*.jpg *.jpeg")],
        )
        if not selected:
            return
        self.source = Path(selected)
        self.name.set(self.source.stem[:59])
        self._refresh_preview()

    def _choose_color(self) -> None:
        selected = colorchooser.askcolor(self.background.get(), parent=self,
                                          title="Boot logo background")[1]
        if not selected:
            return
        self.background.set(selected.upper())
        self.color_button.configure(background=selected, activebackground=selected)
        self._refresh_preview()

    def _refresh_preview(self) -> None:
        if not self.source:
            return
        try:
            image = render_boot_logo(self.source, self.mode.get(), self.background.get())
            image.thumbnail((768, 450))
            self.preview_image = ImageTk.PhotoImage(image)
            self.preview.configure(image=self.preview_image, text="")
            self.status.set(f"{self.source.name}  ·  {self.mode.get().title()}  ·  1024 × 600")
        except BootLogoError as error:
            self.status.set(str(error))

    def _build_package(self, destination: Path) -> Path | None:
        if not self.source:
            messagebox.showerror("No image", "Choose a PNG or JPEG image first.", parent=self)
            return None
        try:
            return export_boot_logo(self.source, destination, self.name.get(),
                                    self.mode.get(), self.background.get())
        except (BootLogoError, OSError) as error:
            messagebox.showerror("Cannot create boot logo", str(error), parent=self)
            return None

    def _export(self) -> None:
        selected = filedialog.asksaveasfilename(
            parent=self, title="Export MackoDash boot logo",
            initialfile=safe_logo_filename(self.name.get()), defaultextension=".mdlogo",
            filetypes=[("MackoDash boot logos", "*.mdlogo")],
        )
        if not selected:
            return
        package = self._build_package(Path(selected))
        if package:
            self.status.set(f"Exported {package.name}")

    def _send(self) -> None:
        temporary = Path(tempfile.gettempdir()) / "MackoDash" / safe_logo_filename(self.name.get())
        package = self._build_package(temporary)
        if package:
            open_boot_logo_upload(self, package)