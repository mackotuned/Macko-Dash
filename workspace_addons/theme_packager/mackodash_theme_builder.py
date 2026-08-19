import argparse
import json
import shutil
import threading
import tkinter as tk
import zipfile
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from convert_squareline_export import ConversionError, convert


def configure_theme_builder_style(root: tk.Misc) -> None:
    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure("TFrame", background="#101214")
    style.configure("TLabel", background="#101214", foreground="#f3f4f6", font=("Segoe UI", 10))
    style.configure("Title.TLabel", font=("Segoe UI Semibold", 21), foreground="#ffffff")
    style.configure("Hint.TLabel", foreground="#a5abb3")
    style.configure("TButton", font=("Segoe UI Semibold", 10), padding=(12, 8))
    style.configure("Accent.TButton", background="#e22936", foreground="#ffffff")
    style.map("Accent.TButton", background=[("active", "#ff3b47"), ("disabled", "#603038")])
    style.configure("TEntry", fieldbackground="#1b1f23", foreground="#ffffff", insertcolor="#ffffff", padding=7)
    style.configure("TCheckbutton", background="#101214", foreground="#f3f4f6")


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
        frame = ttk.Frame(self, padding=24)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="MackoDash Theme Builder", style="Title.TLabel").grid(row=0, column=0, columnspan=3, sticky="w")
        ttk.Label(frame, text="Convert one-screen SquareLine exports into validated SD-card themes.", style="Hint.TLabel").grid(row=1, column=0, columnspan=3, sticky="w", pady=(2, 20))

        self._file_row(frame, 2, "SquareLine export", self.source, self._choose_source)
        self._file_row(frame, 3, "Output package", self.output, self._choose_output)
        self._entry_row(frame, 4, "Theme name", self.theme_name)
        self._entry_row(frame, 5, "Theme ID", self.theme_id)

        ttk.Label(frame, text="Canvas size").grid(row=6, column=0, sticky="w", pady=7)
        size = ttk.Frame(frame)
        size.grid(row=6, column=1, sticky="w", pady=7)
        ttk.Entry(size, textvariable=self.width, width=8).pack(side="left")
        ttk.Label(size, text=" x ").pack(side="left")
        ttk.Entry(size, textvariable=self.height, width=8).pack(side="left")

        ttk.Checkbutton(frame, text="Allow custom-font substitution (development only)", variable=self.allow_fonts).grid(row=7, column=1, columnspan=2, sticky="w", pady=(8, 3))
        ttk.Label(frame, text="Leave this off for customer packages. Missing exact fonts will stop conversion.", style="Hint.TLabel").grid(row=8, column=1, columnspan=2, sticky="w")

        buttons = ttk.Frame(frame)
        buttons.grid(row=9, column=0, columnspan=3, sticky="ew", pady=(20, 12))
        self.convert_button = ttk.Button(buttons, text="Build Theme", style="Accent.TButton", command=self._start_conversion)
        self.convert_button.pack(side="left")
        self.copy_button = ttk.Button(buttons, text="Copy to SD Card", command=self._copy_to_sd, state="disabled")
        self.copy_button.pack(side="left", padx=10)

        ttk.Label(frame, textvariable=self.status, style="Hint.TLabel", wraplength=690).grid(row=10, column=0, columnspan=3, sticky="w", pady=(0, 8))
        self.report = tk.Text(frame, height=13, bg="#171a1e", fg="#d9dde2", insertbackground="#ffffff", relief="flat", padx=12, pady=10, font=("Cascadia Mono", 9), state="disabled")
        self.report.grid(row=11, column=0, columnspan=3, sticky="nsew")
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(11, weight=1)

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
            if not self.output.get():
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
        self.status.set(f"Theme created: {output}")
        self._set_report(json.dumps(report, indent=2))

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


class ThemeBuilder(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("MackoDash Theme Builder")
        self.geometry("760x620")
        self.minsize(680, 560)
        self.configure(background="#101214")
        configure_theme_builder_style(self)
        ThemeBuilderFrame(self).pack(fill="both", expand=True)


if __name__ == "__main__":
    ThemeBuilder().mainloop()