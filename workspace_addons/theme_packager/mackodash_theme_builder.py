import argparse
import json
import shutil
import sys
import threading
import tkinter as tk
import zipfile
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

ADDONS_ROOT = Path(__file__).resolve().parent.parent
if str(ADDONS_ROOT) not in sys.path:
    sys.path.insert(0, str(ADDONS_ROOT))

from convert_squareline_export import ConversionError, convert
from theme_preview import ThemePreviewWindow
from device_transfer_ui import open_theme_upload
from utility_ui import (
    LINE,
    PANEL,
    WHITE,
    build_brand_header,
    build_step_tile,
    configure_mackodash_style,
    set_details_visible,
)


def configure_theme_builder_style(root: tk.Misc) -> None:
    configure_mackodash_style(root)


class ThemeBuilderFrame(ttk.Frame):
    def __init__(self, parent: tk.Misc) -> None:
        super().__init__(parent)
        self.last_output: Path | None = None

        self.source = tk.StringVar()
        self.output = tk.StringVar()
        self.theme_id = tk.StringVar(value="customer.theme-name")
        self.theme_name = tk.StringVar(value="Theme Name")
        self.width = tk.StringVar(value="1024")
        self.height = tk.StringVar(value="600")
        self.allow_fonts = tk.BooleanVar(value=False)
        self.status = tk.StringVar(value="Select a SquareLine export ZIP to begin.")
        self._build_ui()

    def _build_ui(self) -> None:
        frame = ttk.Frame(self, padding=(26, 20))
        frame.pack(fill="both", expand=True)
        header = build_brand_header(
            frame,
            "Theme Builder",
            "Turn a SquareLine Studio export into a MackoDash theme and copy it directly to an SD card.",
        )
        header.grid(row=0, column=0, sticky="ew", pady=(0, 12))

        source_tile, source_body = build_step_tile(
            frame, 1, "Choose your design", "Select the ZIP containing your complete one-screen SquareLine export."
        )
        source_tile.grid(row=1, column=0, sticky="ew", pady=5)
        ttk.Entry(source_body, textvariable=self.source, state="readonly").grid(
            row=0, column=0, sticky="ew", padx=(0, 10)
        )
        ttk.Button(source_body, text="Choose ZIP", command=self._choose_source).grid(row=0, column=1)

        identity_tile, identity_body = build_step_tile(
            frame, 2, "Name the theme", "Choose the display name customers see and a unique lowercase theme ID."
        )
        identity_tile.grid(row=2, column=0, sticky="ew", pady=5)
        ttk.Label(identity_body, text="Theme name", style="Tile.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Entry(identity_body, textvariable=self.theme_name).grid(row=0, column=1, sticky="ew", padx=(12, 0))
        ttk.Label(identity_body, text="Theme ID", style="Tile.TLabel").grid(row=1, column=0, sticky="w", pady=(9, 0))
        ttk.Entry(identity_body, textvariable=self.theme_id).grid(row=1, column=1, sticky="ew", padx=(12, 0), pady=(9, 0))
        identity_body.columnconfigure(1, weight=1)

        build_tile, build_body = build_step_tile(
            frame, 3, "Build and install", "Build the validated package, then copy it to the dashboard SD card."
        )
        build_tile.grid(row=3, column=0, sticky="ew", pady=5)
        self.convert_button = ttk.Button(
            build_body, text="Build Theme", style="Accent.TButton", command=self._start_conversion
        )
        self.convert_button.grid(row=0, column=0, sticky="w")
        self.copy_button = ttk.Button(build_body, text="Copy to SD Card", command=self._copy_to_sd, state="disabled")
        self.copy_button.grid(row=0, column=1, sticky="w", padx=(10, 0))
        self.preview_button = ttk.Button(build_body, text="Preview Theme", command=self._open_preview, state="disabled")
        self.preview_button.grid(row=0, column=2, sticky="w", padx=(10, 0))
        self.send_button = ttk.Button(build_body, text="Send over USB", command=self._send_over_usb)
        self.send_button.grid(row=0, column=3, sticky="w", padx=(10, 0))
        ttk.Label(build_body, textvariable=self.status, style="TileHint.TLabel", wraplength=430).grid(
            row=1, column=0, columnspan=5, sticky="w", pady=(12, 0)
        )
        build_body.columnconfigure(4, weight=1)

        self.details_button = ttk.Button(
            frame, text="Show technical details", style="Quiet.TButton", command=self._toggle_details
        )
        self.details_button.grid(row=4, column=0, sticky="w", pady=(8, 5))
        self.details_frame = ttk.Frame(frame, style="Tile.TFrame", padding=16)
        self.details_frame.grid(row=5, column=0, sticky="nsew")
        self.details_frame.columnconfigure(1, weight=1)

        ttk.Label(self.details_frame, text="Output package", style="Tile.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Entry(self.details_frame, textvariable=self.output).grid(row=0, column=1, sticky="ew", padx=(12, 8))
        ttk.Button(self.details_frame, text="Choose", command=self._choose_output).grid(row=0, column=2)

        ttk.Label(self.details_frame, text="Canvas size", style="Tile.TLabel").grid(row=1, column=0, sticky="w", pady=(10, 0))
        size = ttk.Frame(self.details_frame, style="Tile.TFrame")
        size.grid(row=1, column=1, sticky="w", pady=(10, 0), padx=(12, 0))
        ttk.Entry(size, textvariable=self.width, width=8).pack(side="left")
        ttk.Label(size, text=" x ", style="Tile.TLabel").pack(side="left")
        ttk.Entry(size, textvariable=self.height, width=8).pack(side="left")

        ttk.Checkbutton(
            self.details_frame,
            text="Allow custom-font substitution (development only)",
            variable=self.allow_fonts,
        ).grid(row=2, column=1, columnspan=2, sticky="w", pady=(10, 6))
        self.report = tk.Text(
            self.details_frame,
            height=8,
            bg=PANEL,
            fg=WHITE,
            insertbackground=WHITE,
            highlightbackground=LINE,
            highlightthickness=1,
            relief="flat",
            padx=12,
            pady=10,
            font=("Cascadia Mono", 9),
            state="disabled",
        )
        self.report.grid(row=3, column=0, columnspan=3, sticky="nsew", pady=(6, 0))
        self.details_frame.rowconfigure(3, weight=1)
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(5, weight=1)
        set_details_visible(self.details_button, self.details_frame, False)

    def _toggle_details(self) -> None:
        set_details_visible(
            self.details_button,
            self.details_frame,
            not self.details_frame.winfo_ismapped(),
        )

    def _file_row(self, parent: ttk.Frame, row: int, label: str, variable: tk.StringVar, command) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=7)
        ttk.Entry(parent, textvariable=variable).grid(row=row, column=1, sticky="ew", padx=(14, 8), pady=7)
        ttk.Button(parent, text="Browse", command=command).grid(row=row, column=2, pady=7)

    def _entry_row(self, parent: ttk.Frame, row: int, label: str, variable: tk.StringVar) -> None:
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=7)
        ttk.Entry(parent, textvariable=variable).grid(row=row, column=1, columnspan=2, sticky="ew", padx=(14, 0), pady=7)

    def _choose_source(self) -> None:
        selected = filedialog.askopenfilename(title="Choose SquareLine export", filetypes=[("ZIP exports", "*.zip"), ("All files", "*.*")])
        if selected:
            self.source.set(selected)
            if not self.output.get() or self.output.get().endswith(".mdtheme.zip"):
                self.output.set(str(Path(selected).with_suffix(".mdtheme.zip")))

    def _choose_output(self) -> None:
        selected = filedialog.asksaveasfilename(title="Save MackoDash theme", defaultextension=".mdtheme.zip", filetypes=[("MackoDash themes", "*.mdtheme.zip")])
        if selected:
            self.output.set(selected)

    def _start_conversion(self) -> None:
        try:
            width = int(self.width.get())
            height = int(self.height.get())
        except ValueError:
            messagebox.showerror("Invalid canvas", "Canvas width and height must be whole numbers.")
            return
        args = argparse.Namespace(
            source=Path(self.source.get()), output=Path(self.output.get()), theme_id=self.theme_id.get().strip(),
            name=self.theme_name.get().strip(), width=width, height=height,
            allow_font_substitution=self.allow_fonts.get(),
        )
        self.convert_button.configure(state="disabled")
        self.copy_button.configure(state="disabled")
        self.preview_button.configure(state="disabled")
        self.status.set("Validating and converting SquareLine export...")
        threading.Thread(target=self._convert_worker, args=(args,), daemon=True).start()

    def _convert_worker(self, args: argparse.Namespace) -> None:
        try:
            report = convert(args)
        except (ConversionError, OSError, zipfile.BadZipFile) as error:
            self.after(0, self._conversion_failed, str(error))
            return
        self.after(0, self._conversion_done, args.output, report)

    def _set_report(self, value: str) -> None:
        self.report.configure(state="normal")
        self.report.delete("1.0", "end")
        self.report.insert("1.0", value)
        self.report.configure(state="disabled")

    def _conversion_failed(self, error: str) -> None:
        self.convert_button.configure(state="normal")
        self.status.set("Conversion stopped. Fix the issue below and try again.")
        self._set_report(f"ERROR\n\n{error}")

    def _conversion_done(self, output: Path, report: dict) -> None:
        self.last_output = output
        self.convert_button.configure(state="normal")
        self.copy_button.configure(state="normal")
        self.preview_button.configure(state="normal")
        self.status.set(f"Theme created: {output}")
        self._set_report(json.dumps(report, indent=2))

    def _open_preview(self) -> None:
        if not self.last_output or not self.last_output.exists():
            return
        try:
            ThemePreviewWindow(self, self.last_output)
        except (OSError, ValueError, KeyError, json.JSONDecodeError, zipfile.BadZipFile) as error:
            messagebox.showerror("Preview unavailable", str(error))

    def _copy_to_sd(self) -> None:
        if not self.last_output or not self.last_output.exists():
            return
        root = filedialog.askdirectory(title="Choose the SD card drive")
        if not root:
            return
        destination = Path(root) / "MACKODASH" / "THEMES" / self.last_output.name
        try:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.last_output, destination)
        except OSError as error:
            messagebox.showerror("Copy failed", str(error))
            return
        messagebox.showinfo("Theme copied", f"Copied to:\n{destination}")

    def _send_over_usb(self) -> None:
        package = self.last_output if self.last_output and self.last_output.exists() else None
        open_theme_upload(self, package)