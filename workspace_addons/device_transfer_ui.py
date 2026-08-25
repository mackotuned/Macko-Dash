from __future__ import annotations

import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from serial.tools import list_ports

LOG_VIEWER = Path(__file__).resolve().parent / "log_viewer"
if str(LOG_VIEWER) not in sys.path:
    sys.path.insert(0, str(LOG_VIEWER))

from device_transfer import DashboardClient, DeviceLog, DeviceTransferError


def _port_labels() -> list[str]:
    ports = sorted(list_ports.comports(), key=lambda item: item.device)
    return [f"{item.device} - {item.description}" for item in ports]


class DeviceTransferWindow(tk.Toplevel):
    def __init__(self, parent: tk.Misc, mode: str, callback=None, package: Path | None = None) -> None:
        super().__init__(parent)
        self.mode = mode
        self.callback = callback
        self.package = package
        self.logs: list[DeviceLog] = []
        self.port = tk.StringVar()
        self.log_name = tk.StringVar()
        self.status = tk.StringVar(value="Select the dashboard USB port.")
        self.progress = tk.DoubleVar()
        self.title("Dashboard USB Transfer")
        self.geometry("620x310")
        self.resizable(False, False)
        self.transient(parent.winfo_toplevel())
        self.grab_set()
        self._build_ui()
        self._refresh_ports()

    def _build_ui(self) -> None:
        body = ttk.Frame(self, padding=22)
        body.pack(fill="both", expand=True)
        title = "Download Driving Log" if self.mode == "logs" else "Send Theme to Dashboard"
        ttk.Label(body, text=title, style="Title.TLabel").grid(row=0, column=0, columnspan=3, sticky="w")
        ttk.Label(body, text="USB PORT", style="Subtitle.TLabel").grid(row=1, column=0, sticky="w", pady=(20, 5))
        self.port_combo = ttk.Combobox(body, textvariable=self.port, state="readonly")
        self.port_combo.grid(row=2, column=0, sticky="ew", padx=(0, 8))
        self.refresh_button = ttk.Button(body, text="Refresh", command=self._refresh_ports)
        self.refresh_button.grid(row=2, column=1, sticky="ew")
        if self.mode == "logs":
            self.connect_button = ttk.Button(body, text="Find Logs", command=self._find_logs)
            self.connect_button.grid(row=2, column=2, sticky="ew", padx=(8, 0))
            ttk.Label(body, text="DRIVING LOG", style="Subtitle.TLabel").grid(
                row=3, column=0, sticky="w", pady=(16, 5)
            )
            self.log_combo = ttk.Combobox(body, textvariable=self.log_name, state="disabled")
            self.log_combo.grid(row=4, column=0, columnspan=2, sticky="ew", padx=(0, 8))
            self.action_button = ttk.Button(
                body, text="Download and Open", style="Accent.TButton", command=self._download, state="disabled"
            )
        else:
            package_name = package.name if package else "Choose a .mdtheme.zip package"
            ttk.Label(body, text=package_name, style="TileHint.TLabel").grid(
                row=3, column=0, columnspan=3, sticky="w", pady=(18, 0)
            )
            self.action_button = ttk.Button(
                body, text="Send Theme", style="Accent.TButton", command=self._upload
            )
        self.action_button.grid(row=4 if self.mode == "logs" else 4, column=2, sticky="ew", padx=(8, 0))
        ttk.Progressbar(body, variable=self.progress, maximum=100).grid(
            row=5, column=0, columnspan=3, sticky="ew", pady=(20, 8)
        )
        ttk.Label(body, textvariable=self.status, style="TileHint.TLabel", wraplength=560).grid(
            row=6, column=0, columnspan=3, sticky="w"
        )
        body.columnconfigure(0, weight=1)

    def _refresh_ports(self) -> None:
        labels = _port_labels()
        retained = self.port.get() if self.port.get() in labels else ""
        preferred = next((label for label in labels if "CH340" in label.upper()), "")
        self.port_combo.configure(values=labels)
        self.port.set(retained or preferred or (labels[0] if len(labels) == 1 else ""))
        self.status.set("Select the dashboard USB port." if labels else "No USB serial ports were found.")

    def _device(self) -> str:
        return self.port.get().split(" - ", 1)[0].strip()

    def _find_logs(self) -> None:
        if not self._device():
            messagebox.showerror("No USB port", "Connect the dashboard and select its USB port.", parent=self)
            return
        self._set_busy(True, "Connecting to the dashboard and reading its SD log list...")
        threading.Thread(target=self._list_worker, daemon=True).start()

    def _list_worker(self) -> None:
        try:
            with DashboardClient.connect(self._device()) as client:
                logs = client.list_logs()
        except (DeviceTransferError, OSError) as error:
            self.after(0, self._failed, str(error))
            return
        self.after(0, self._logs_found, logs)

    def _logs_found(self, logs: list[DeviceLog]) -> None:
        self._set_busy(False)
        self.logs = logs
        labels = [f"{item.name}  ({item.size / 1024:.1f} KiB)" for item in logs]
        self.log_combo.configure(values=labels, state="readonly" if logs else "disabled")
        self.log_name.set(labels[0] if labels else "")
        self.action_button.configure(state="normal" if logs else "disabled")
        self.status.set(f"Found {len(logs)} driving log{'s' if len(logs) != 1 else ''}." if logs else "No driving logs are stored on the dashboard SD card.")

    def _selected_log(self) -> DeviceLog | None:
        index = self.log_combo.current()
        return self.logs[index] if 0 <= index < len(self.logs) else None

    def _download(self) -> None:
        log = self._selected_log()
        if not log:
            return
        destination = filedialog.asksaveasfilename(
            parent=self, title="Save driving log", initialfile=log.name,
            defaultextension=".CSV", filetypes=[("MackoDash logs", "*.CSV")],
        )
        if not destination:
            return
        self._set_busy(True, f"Downloading {log.name}...")
        threading.Thread(target=self._download_worker, args=(log, Path(destination)), daemon=True).start()

    def _download_worker(self, log: DeviceLog, destination: Path) -> None:
        try:
            with DashboardClient.connect(self._device()) as client:
                client.download_log(log, destination, self._queue_progress)
        except (DeviceTransferError, OSError) as error:
            self.after(0, self._failed, str(error))
            return
        self.after(0, self._download_done, destination)

    def _download_done(self, destination: Path) -> None:
        self._set_busy(False)
        self.status.set(f"Downloaded and verified {destination.name}.")
        if self.callback:
            self.callback(destination)
        messagebox.showinfo("Log downloaded", f"Downloaded and verified:\n{destination}", parent=self)

    def _upload(self) -> None:
        package = self.package
        if not package or not package.is_file():
            selected = filedialog.askopenfilename(
                parent=self, title="Choose MackoDash theme",
                filetypes=[("MackoDash themes", "*.mdtheme.zip")],
            )
            if not selected:
                return
            package = Path(selected)
            self.package = package
        if not self._device():
            messagebox.showerror("No USB port", "Connect the dashboard and select its USB port.", parent=self)
            return
        self._set_busy(True, f"Sending {package.name}. Keep dashboard power connected...")
        threading.Thread(target=self._upload_worker, args=(package,), daemon=True).start()

    def _upload_worker(self, package: Path) -> None:
        try:
            with DashboardClient.connect(self._device()) as client:
                client.upload_theme(package, self._queue_progress)
        except (DeviceTransferError, OSError) as error:
            self.after(0, self._failed, str(error))
            return
        self.after(0, self._upload_done, package)

    def _upload_done(self, package: Path) -> None:
        self._set_busy(False)
        self.status.set("Theme installed. The dashboard is restarting.")
        messagebox.showinfo(
            "Theme installed", f"{package.name} was verified and installed. The dashboard is restarting.", parent=self
        )

    def _queue_progress(self, completed: int, total: int) -> None:
        self.after(0, self.progress.set, completed * 100 / total if total else 0)

    def _failed(self, error: str) -> None:
        self._set_busy(False)
        self.status.set(error)
        messagebox.showerror("USB transfer failed", error, parent=self)

    def _set_busy(self, busy: bool, status: str | None = None) -> None:
        state = "disabled" if busy else "normal"
        self.refresh_button.configure(state=state)
        self.port_combo.configure(state="disabled" if busy else "readonly")
        self.action_button.configure(state=state)
        if self.mode == "logs":
            self.connect_button.configure(state=state)
        if busy:
            self.progress.set(0)
        if status:
            self.status.set(status)


def open_log_download(parent: tk.Misc, callback) -> DeviceTransferWindow:
    return DeviceTransferWindow(parent, "logs", callback=callback)


def open_theme_upload(parent: tk.Misc, package: Path | None = None) -> DeviceTransferWindow:
    return DeviceTransferWindow(parent, "theme", package=package)