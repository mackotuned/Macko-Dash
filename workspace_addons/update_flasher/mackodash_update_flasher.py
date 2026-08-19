from __future__ import annotations

import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from serial.tools import list_ports

from firmware import FirmwareInfo, FirmwareValidationError, flash_firmware, validate_firmware


def configure_update_flasher_style(root: tk.Misc) -> None:
    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure("TFrame", background="#101214")
    style.configure("TLabel", background="#101214", foreground="#f3f4f6", font=("Segoe UI", 10))
    style.configure("Title.TLabel", font=("Segoe UI Semibold", 21), foreground="#ffffff")
    style.configure("Hint.TLabel", foreground="#a5abb3")
    style.configure("Good.TLabel", foreground="#55d98b")
    style.configure("TButton", font=("Segoe UI Semibold", 10), padding=(12, 8))
    style.configure("Accent.TButton", background="#e22936", foreground="#ffffff")
    style.map("Accent.TButton", background=[("active", "#ff3b47"), ("disabled", "#603038")])
    style.configure("TEntry", fieldbackground="#1b1f23", foreground="#ffffff", insertcolor="#ffffff", padding=7)
    style.configure("TCombobox", fieldbackground="#1b1f23", foreground="#ffffff", padding=7)


class UpdateFlasherFrame(ttk.Frame):
    def __init__(self, parent: tk.Misc) -> None:
        super().__init__(parent)

        self.firmware_path = tk.StringVar()
        self.port = tk.StringVar()
        self.status = tk.StringVar(value="Choose an official MackoDash firmware file to begin.")
        self.firmware_info: FirmwareInfo | None = None
        self._events: queue.Queue[tuple[str, object]] = queue.Queue()
        self._busy = False

        self._build_ui()
        self._refresh_ports()
        self.after(100, self._load_adjacent_firmware)
        self.after(75, self._process_events)

    def _build_ui(self) -> None:
        frame = ttk.Frame(self, padding=24)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="MackoDash USB Update Flasher", style="Title.TLabel").grid(
            row=0, column=0, columnspan=3, sticky="w"
        )
        ttk.Label(
            frame,
            text="Installs a complete validated ESP32-P4 firmware ZIP. NVS settings, odometer data, SD-card themes, and the C6 are preserved.",
            style="Hint.TLabel",
            wraplength=750,
        ).grid(row=1, column=0, columnspan=3, sticky="w", pady=(2, 20))

        ttk.Label(frame, text="Firmware").grid(row=2, column=0, sticky="w", pady=7)
        self.firmware_entry = ttk.Entry(frame, textvariable=self.firmware_path, state="readonly")
        self.firmware_entry.grid(row=2, column=1, sticky="ew", padx=(14, 8), pady=7)
        self.browse_button = ttk.Button(frame, text="Browse", command=self._choose_firmware)
        self.browse_button.grid(row=2, column=2, pady=7)

        ttk.Label(frame, text="USB port").grid(row=3, column=0, sticky="w", pady=7)
        self.port_combo = ttk.Combobox(frame, textvariable=self.port, state="readonly")
        self.port_combo.grid(row=3, column=1, sticky="ew", padx=(14, 8), pady=7)
        self.refresh_button = ttk.Button(frame, text="Refresh", command=self._refresh_ports)
        self.refresh_button.grid(row=3, column=2, pady=7)

        self.metadata = ttk.Label(
            frame,
            text="No firmware validated.",
            style="Hint.TLabel",
            wraplength=750,
            justify="left",
        )
        self.metadata.grid(row=4, column=0, columnspan=3, sticky="w", pady=(12, 14))

        warning = tk.Frame(frame, background="#21181a", highlightbackground="#553038", highlightthickness=1)
        warning.grid(row=5, column=0, columnspan=3, sticky="ew", pady=(0, 16))
        tk.Label(
            warning,
            text="Keep the dashboard powered and the USB cable connected until verification finishes.",
            background="#21181a",
            foreground="#f4c5ca",
            font=("Segoe UI Semibold", 10),
            padx=14,
            pady=12,
        ).pack(anchor="w")

        buttons = ttk.Frame(frame)
        buttons.grid(row=6, column=0, columnspan=3, sticky="ew", pady=(0, 12))
        self.flash_button = ttk.Button(
            buttons, text="Flash ESP32-P4 Firmware", style="Accent.TButton", command=self._confirm_flash, state="disabled"
        )
        self.flash_button.pack(side="left")
        ttk.Label(buttons, textvariable=self.status, style="Hint.TLabel", wraplength=490).pack(
            side="left", padx=(14, 0)
        )

        self.log = tk.Text(
            frame,
            height=18,
            bg="#171a1e",
            fg="#d9dde2",
            insertbackground="#ffffff",
            relief="flat",
            padx=12,
            pady=10,
            font=("Cascadia Mono", 9),
            state="disabled",
        )
        self.log.grid(row=7, column=0, columnspan=3, sticky="nsew")
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(7, weight=1)

    def _choose_firmware(self) -> None:
        selected = filedialog.askopenfilename(
            title="Choose MackoDash full firmware ZIP",
            filetypes=[("MackoDash firmware ZIP", "*.zip"), ("All files", "*.*")],
        )
        if not selected:
            return
        self._load_firmware(Path(selected), show_error=True)

    def _load_adjacent_firmware(self) -> None:
        if not getattr(sys, "frozen", False):
            return
        candidate = Path(sys.executable).with_name("MackoDash-Firmware.zip")
        if candidate.is_file():
            self._load_firmware(candidate, show_error=False)

    def _load_firmware(self, selected: Path, show_error: bool) -> None:
        try:
            info = validate_firmware(selected)
        except (FirmwareValidationError, OSError) as error:
            self.firmware_info = None
            self.firmware_path.set("")
            self.metadata.configure(text="No firmware validated.", style="Hint.TLabel")
            self._update_flash_state()
            if show_error:
                messagebox.showerror("Firmware rejected", str(error))
            else:
                self.status.set(f"Bundled firmware was rejected: {error}")
                self._append_log(f"ERROR: Bundled firmware was rejected: {error}")
            return

        self.firmware_info = info
        self.firmware_path.set(str(info.path))
        image_summary = "    ".join(
            f"{image.definition.name}: {image.size:,} B" for image in info.images
        )
        self.metadata.configure(
            text=(
                f"Validated complete ESP32-P4 firmware bundle\n"
                f"Project: {info.project_name}    Version: {info.version}    Built: {info.compile_date} {info.compile_time}\n"
                f"Payload: {info.size:,} bytes    Bundle SHA-256: {info.sha256}\n"
                f"{image_summary}"
            ),
            style="Good.TLabel",
        )
        self.status.set("Full firmware ZIP validated. Connect the dashboard and select its USB port.")
        self._append_log(f"Validated {info.path.name}")
        self._append_log(f"SHA-256 {info.sha256}")
        self._update_flash_state()

    def _refresh_ports(self) -> None:
        ports = sorted(list_ports.comports(), key=lambda item: item.device)
        labels = [f"{item.device} - {item.description}" for item in ports]
        current_device = self._selected_device()
        self.port_combo.configure(values=labels)

        preferred = next(
            (label for label, item in zip(labels, ports) if item.vid == 0x1A86 and item.pid == 0x7522),
            None,
        )
        retained = next((label for label in labels if label.split(" - ", 1)[0] == current_device), None)
        self.port.set(retained or preferred or (labels[0] if len(labels) == 1 else ""))
        self.status.set(
            f"Found {len(labels)} serial port{'s' if len(labels) != 1 else ''}."
            if labels else "No USB serial ports found. Connect the dashboard and press Refresh."
        )
        self._update_flash_state()

    def _selected_device(self) -> str:
        return self.port.get().split(" - ", 1)[0].strip()

    def _update_flash_state(self) -> None:
        ready = self.firmware_info is not None and bool(self._selected_device()) and not self._busy
        self.flash_button.configure(state="normal" if ready else "disabled")

    def _confirm_flash(self) -> None:
        if not self.firmware_info:
            return
        port = self._selected_device()
        if not port:
            messagebox.showerror("No USB port", "Connect the dashboard and select its COM port.")
            return
        confirmed = messagebox.askyesno(
            "Flash complete MackoDash firmware?",
            f"Firmware ZIP: {self.firmware_info.path.name}\n"
            f"Version: {self.firmware_info.version}\n"
            f"USB port: {port}\n\n"
            "This replaces the ESP32-P4 bootloader, partition table, OTA metadata, application, and onboard SPIFFS.\n\n"
            "NVS settings, odometer data, SD-card themes, and ESP32-C6 firmware are preserved.\n\n"
            "Keep power and USB connected until verification finishes.",
            icon="warning",
        )
        if not confirmed:
            return

        self._busy = True
        self.browse_button.configure(state="disabled")
        self.refresh_button.configure(state="disabled")
        self.port_combo.configure(state="disabled")
        self._update_flash_state()
        self.status.set("Connecting and flashing. Do not disconnect USB or power.")
        self._append_log("")
        self._append_log(f"Starting complete ESP32-P4 firmware flash on {port}...")
        threading.Thread(target=self._flash_worker, args=(port, self.firmware_info), daemon=True).start()

    def _flash_worker(self, port: str, info: FirmwareInfo) -> None:
        try:
            flash_firmware(port, info, lambda line: self._events.put(("log", line)))
        except BaseException as error:
            self._events.put(("failed", str(error) or type(error).__name__))
            return
        self._events.put(("done", info))

    def _process_events(self) -> None:
        try:
            while True:
                event, payload = self._events.get_nowait()
                if event == "log":
                    self._append_log(str(payload))
                elif event == "failed":
                    self._finish_flash(False, str(payload))
                elif event == "done":
                    self._finish_flash(True, "")
        except queue.Empty:
            pass
        self.after(75, self._process_events)

    def _finish_flash(self, success: bool, detail: str) -> None:
        self._busy = False
        self.browse_button.configure(state="normal")
        self.refresh_button.configure(state="normal")
        self.port_combo.configure(state="readonly")
        self._update_flash_state()
        if success:
            self.status.set("Update complete and verified. MackoDash has restarted.")
            self._append_log("Update complete. Flash verification passed and the dashboard was reset.")
            messagebox.showinfo("Update complete", "MackoDash was updated, verified, and restarted successfully.")
        else:
            self.status.set("Update failed. Leave USB and power connected, review the log, and retry.")
            self._append_log(f"ERROR: {detail}")
            messagebox.showerror("Update failed", detail)

    def _append_log(self, line: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")


class UpdateFlasher(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("MackoDash USB Update Flasher")
        self.geometry("820x650")
        self.minsize(720, 580)
        self.configure(background="#101214")
        configure_update_flasher_style(self)
        UpdateFlasherFrame(self).pack(fill="both", expand=True)


if __name__ == "__main__":
    UpdateFlasher().mainloop()