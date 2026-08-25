from __future__ import annotations

import math
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from log_data import LogFormatError, LogSession, SIGNALS, find_logs, load_log, nearest_row_index, sampled_row_indices
from utility_ui import AMBER, GREEN, LABEL, LINE, PANEL, RED, VOID, WHITE, build_brand_header


TRACE_COLORS = (RED, GREEN, "#00c8ff", AMBER, "#ff5cf4", "#a6ff4d", "#ff784d", "#a78bfa")
PRESETS = {
    "AFR / Timing / Boost": ("afr", "timing_deg", "map_psi"),
    "Boost / Timing": ("map_psi", "timing_deg"),
    "Temps / RPM / Boost": ("coolant_f", "intake_f", "rpm", "map_psi"),
    "Fuel & Ignition": ("rpm", "tps_pct", "afr", "timing_deg", "map_psi", "knock_deg", "duty_pct"),
    "Engine Health": ("rpm", "coolant_f", "intake_f", "oil_psi", "battery_v", "knock_deg"),
    "Driver Inputs": ("rpm", "speed_mph", "gear", "tps_pct", "map_psi"),
    "All Channels": tuple(SIGNALS),
}


def format_duration(seconds: float) -> str:
    minutes, whole_seconds = divmod(round(seconds), 60)
    hours, minutes = divmod(minutes, 60)
    return f"{hours:d}:{minutes:02d}:{whole_seconds:02d}" if hours else f"{minutes:d}:{whole_seconds:02d}"


def format_value(value: float | int | None, unit: str = "") -> str:
    if value is None:
        return "--"
    number = f"{float(value):,.0f}" if unit == "RPM" or float(value).is_integer() else f"{float(value):,.1f}"
    return f"{number} {unit}".rstrip()


class LogViewerFrame(ttk.Frame):
    LEFT = 80
    RIGHT = 28
    TOP = 22
    LANE_HEIGHT = 94
    LANE_GAP = 14

    def __init__(self, parent: tk.Misc) -> None:
        super().__init__(parent)
        self.session: LogSession | None = None
        self.log_paths: list[Path] = []
        self.log_choice = tk.StringVar()
        self.status = tk.StringVar(value="Open a MackoDash CSV log or select the SD card.")
        self.pixels_per_second = tk.IntVar(value=35)
        self.preset = tk.StringVar(value="AFR / Timing / Boost")
        self.signal_enabled = {signal: tk.BooleanVar(value=signal in PRESETS[self.preset.get()]) for signal in SIGNALS}
        self.summary_values: dict[str, tk.StringVar] = {}
        self._redraw_job: str | None = None
        self._ranges: dict[str, tuple[float, float]] = {}
        self._elapsed_times: tuple[int, ...] = ()
        self._build_ui()

    def _build_ui(self) -> None:
        frame = ttk.Frame(self, padding=(26, 18))
        frame.pack(fill="both", expand=True)
        build_brand_header(
            frame, "Driving Log Viewer",
            "Review recorded drives as synchronized dyno-style traces. Hover over the graph for exact values.",
        ).pack(fill="x")

        controls = ttk.Frame(frame, style="Tile.TFrame", padding=14)
        controls.pack(fill="x", pady=(0, 10))
        ttk.Button(controls, text="Open CSV", style="Accent.TButton", command=self._choose_file).grid(row=0, column=0)
        ttk.Button(controls, text="Find on SD Card", command=self._choose_sd).grid(row=0, column=1, padx=(8, 0))
        self.log_combo = ttk.Combobox(controls, textvariable=self.log_choice, state="readonly", width=24)
        self.log_combo.grid(row=0, column=2, sticky="ew", padx=(12, 0))
        self.log_combo.bind("<<ComboboxSelected>>", self._select_discovered_log)
        ttk.Label(controls, textvariable=self.status, style="TileHint.TLabel", wraplength=650).grid(
            row=1, column=0, columnspan=3, sticky="w", pady=(9, 0)
        )
        controls.columnconfigure(2, weight=1)

        summary = ttk.Frame(frame)
        summary.pack(fill="x", pady=(0, 10))
        summaries = (("duration", "DURATION"), ("distance", "DISTANCE"), ("rpm", "MAX RPM"),
                     ("speed", "MAX SPEED"), ("boost", "MAX BOOST"), ("afr", "MIN AFR"))
        for column, (key, title) in enumerate(summaries):
            summary.columnconfigure(column, weight=1, uniform="summary")
            tile = tk.Frame(summary, background=PANEL, highlightbackground=LINE, highlightthickness=1, padx=10, pady=8)
            tile.grid(row=0, column=column, sticky="nsew", padx=(0, 5) if column < len(summaries) - 1 else 0)
            tk.Label(tile, text=title, background=PANEL, foreground=LABEL,
                     font=("Segoe UI Semibold", 8)).pack(anchor="w")
            value = tk.StringVar(value="--")
            self.summary_values[key] = value
            tk.Label(tile, textvariable=value, background=PANEL, foreground=WHITE,
                     font=("Segoe UI Semibold", 13)).pack(anchor="w", pady=(2, 0))

        notebook = ttk.Notebook(frame)
        notebook.pack(fill="both", expand=True)
        graph_tab = ttk.Frame(notebook)
        selection_tab = ttk.Frame(notebook, padding=18)
        notebook.add(graph_tab, text="Graph")
        notebook.add(selection_tab, text="Select Channels")
        self._build_graph_tab(graph_tab)
        self._build_selection_tab(selection_tab)

    def _build_graph_tab(self, parent: ttk.Frame) -> None:
        toolbar = ttk.Frame(parent, padding=(10, 8))
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text="TIME SCALE", style="Subtitle.TLabel").pack(side="left")
        ttk.Scale(toolbar, from_=10, to=120, variable=self.pixels_per_second,
                  command=lambda _value: self._schedule_redraw()).pack(side="left", fill="x", expand=True, padx=10)
        ttk.Label(toolbar, text="Shift + wheel scrolls horizontally", style="Subtitle.TLabel").pack(side="right")

        chart = tk.Frame(parent, background=VOID)
        chart.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(chart, background="#0b0c0e", highlightbackground=LINE,
                                highlightthickness=1, height=500)
        horizontal = ttk.Scrollbar(chart, orient="horizontal", command=self.canvas.xview)
        vertical = ttk.Scrollbar(chart, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=horizontal.set, yscrollcommand=vertical.set)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        vertical.grid(row=0, column=1, sticky="ns")
        horizontal.grid(row=1, column=0, sticky="ew", pady=(5, 0))
        chart.columnconfigure(0, weight=1)
        chart.rowconfigure(0, weight=1)
        self.canvas.bind("<Configure>", lambda _event: self._schedule_redraw())
        self.canvas.bind("<Motion>", self._hover)
        self.canvas.bind("<Leave>", lambda _event: self.canvas.delete("hover"))
        self.canvas.bind("<Shift-MouseWheel>", lambda event: self.canvas.xview_scroll(-int(event.delta / 120), "units"))

    def _build_selection_tab(self, parent: ttk.Frame) -> None:
        preset_row = ttk.Frame(parent, style="Tile.TFrame", padding=16)
        preset_row.pack(fill="x", pady=(0, 12))
        ttk.Label(preset_row, text="VIEW PRESET", style="TileTitle.TLabel").pack(side="left")
        preset_combo = ttk.Combobox(preset_row, textvariable=self.preset, values=list(PRESETS) + ["Custom"],
                                    state="readonly", width=26)
        preset_combo.pack(side="left", padx=(16, 0))
        preset_combo.bind("<<ComboboxSelected>>", self._apply_preset)

        grid = ttk.Frame(parent, style="Tile.TFrame", padding=16)
        grid.pack(fill="both", expand=True)
        ttk.Label(grid, text="CUSTOM CHANNELS", style="TileTitle.TLabel").grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 10)
        )
        for index, (signal, (label, unit)) in enumerate(SIGNALS.items()):
            row, column = divmod(index, 3)
            ttk.Checkbutton(
                grid, text=f"{label} ({unit})" if unit else label, variable=self.signal_enabled[signal],
                command=self._custom_selection,
            ).grid(row=row + 1, column=column, sticky="w", padx=(0, 24), pady=7)
            grid.columnconfigure(column, weight=1)

    def _selected_signals(self) -> list[str]:
        selected = [signal for signal, enabled in self.signal_enabled.items() if enabled.get()]
        return selected or ["rpm"]

    def _apply_preset(self, _event: tk.Event | None = None) -> None:
        if self.preset.get() == "Custom":
            return
        selected = set(PRESETS[self.preset.get()])
        for signal, enabled in self.signal_enabled.items():
            enabled.set(signal in selected)
        self._draw_chart()

    def _custom_selection(self) -> None:
        self.preset.set("Custom")
        self._draw_chart()

    def _choose_file(self) -> None:
        selected = filedialog.askopenfilename(
            title="Open MackoDash driving log",
            filetypes=[("MackoDash logs", "LOG*.CSV"), ("CSV files", "*.csv"), ("All files", "*.*")],
        )
        if selected:
            self._load(Path(selected))

    def _choose_sd(self) -> None:
        selected = filedialog.askdirectory(title="Select the SD card or MACKODASH folder")
        if not selected:
            return
        self.log_paths = find_logs(selected)
        if not self.log_paths:
            messagebox.showwarning("No logs found", "No LOG####.CSV files were found under MACKODASH\\LOGS.")
            return
        self.log_combo.configure(values=[path.name for path in self.log_paths])
        self.log_choice.set(self.log_paths[0].name)
        self._load(self.log_paths[0])

    def _select_discovered_log(self, _event: tk.Event) -> None:
        selected = next((path for path in self.log_paths if path.name == self.log_choice.get()), None)
        if selected:
            self._load(selected)

    def _load(self, path: Path) -> None:
        try:
            self.session = load_log(path)
        except LogFormatError as error:
            messagebox.showerror("Cannot open log", str(error))
            return
        self.status.set(f"{path.name}  |  {len(self.session.rows):,} samples  |  {self.session.sample_rate_hz:.1f} Hz")
        self._elapsed_times = tuple(int(row["elapsed_ms"]) for row in self.session.rows)
        self._refresh_summary()
        self._draw_chart()

    def _refresh_summary(self) -> None:
        if not self.session:
            return
        self.summary_values["duration"].set(format_duration(self.session.duration_seconds))
        self.summary_values["distance"].set(format_value(self.session.distance_miles, "mi"))
        self.summary_values["rpm"].set(format_value(self.session.maximum("rpm"), "RPM"))
        self.summary_values["speed"].set(format_value(self.session.maximum("speed_mph"), "mph"))
        self.summary_values["boost"].set(format_value(self.session.maximum("map_psi"), "psi"))
        self.summary_values["afr"].set(format_value(self.session.minimum("afr"), "AFR"))

    def _schedule_redraw(self) -> None:
        if self._redraw_job:
            self.after_cancel(self._redraw_job)
        self._redraw_job = self.after(80, self._draw_chart)

    def _draw_chart(self) -> None:
        self._redraw_job = None
        self.canvas.delete("all")
        if not self.session:
            self.canvas.create_text(36, 42, anchor="nw", text="OPEN A LOG TO VIEW THE DRIVE",
                                    fill=LABEL, font=("Segoe UI Semibold", 12))
            return
        selected = self._selected_signals()
        seconds = max(self.session.duration_seconds, 1.0)
        pixels_per_second = max(10, int(self.pixels_per_second.get()))
        viewport_width = max(760, self.canvas.winfo_width())
        chart_width = max(viewport_width, self.LEFT + self.RIGHT + seconds * pixels_per_second)
        chart_height = self.TOP + len(selected) * (self.LANE_HEIGHT + self.LANE_GAP) + 28
        self.canvas.configure(scrollregion=(0, 0, chart_width, chart_height))
        self.canvas.create_rectangle(0, 0, chart_width, chart_height, fill="#0b0c0e", outline="")
        plot_right = chart_width - self.RIGHT
        tick_seconds = next(value for value in (1, 2, 5, 10, 30, 60, 120, 300, 600) if value * pixels_per_second >= 72)
        for tick in range(0, math.ceil(seconds) + tick_seconds, tick_seconds):
            x = self.LEFT + tick * pixels_per_second
            if x > plot_right:
                break
            self.canvas.create_line(x, self.TOP, x, chart_height - 24, fill="#24262b")
            self.canvas.create_text(x + 4, chart_height - 13, anchor="w", text=format_duration(tick),
                                    fill=LABEL, font=("Cascadia Mono", 8))
        indices = sampled_row_indices(len(self.session.rows), max(500, int((plot_right - self.LEFT) * 1.5)))
        self._ranges.clear()
        for lane, signal in enumerate(selected):
            label, unit = SIGNALS[signal]
            top = self.TOP + lane * (self.LANE_HEIGHT + self.LANE_GAP)
            bottom = top + self.LANE_HEIGHT
            values = self.session.values(signal)
            minimum, maximum = (min(values), max(values)) if values else (0.0, 1.0)
            padding = max((maximum - minimum) * 0.08, 1.0 if math.isclose(minimum, maximum) else 0.0)
            minimum -= padding
            maximum += padding
            self._ranges[signal] = (minimum, maximum)
            self.canvas.create_rectangle(self.LEFT, top, plot_right, bottom, fill="#101216", outline="#30333a")
            for division in range(1, 4):
                y = top + self.LANE_HEIGHT * division / 4
                self.canvas.create_line(self.LEFT, y, plot_right, y, fill="#1f2227")
            color = TRACE_COLORS[lane % len(TRACE_COLORS)]
            self.canvas.create_text(8, top + 6, anchor="nw", text=label.upper(), fill=color,
                                    font=("Segoe UI Semibold", 8), width=self.LEFT - 12)
            self.canvas.create_text(self.LEFT - 6, top + 24, anchor="ne", text=format_value(maximum, unit),
                                    fill=LABEL, font=("Cascadia Mono", 7))
            self.canvas.create_text(self.LEFT - 6, bottom - 3, anchor="se", text=format_value(minimum, unit),
                                    fill=LABEL, font=("Cascadia Mono", 7))
            segments: list[list[float]] = [[]]
            for index in indices:
                row = self.session.rows[index]
                value = row[signal]
                if value is None:
                    if segments[-1]:
                        segments.append([])
                    continue
                x = self.LEFT + float(row["elapsed_ms"]) / 1000.0 * pixels_per_second
                y = bottom - (float(value) - minimum) / (maximum - minimum) * self.LANE_HEIGHT
                segments[-1].extend((x, y))
            for points in segments:
                if len(points) >= 4:
                    self.canvas.create_line(*points, fill=color, width=2)

    def _hover(self, event: tk.Event) -> None:
        if not self.session:
            return
        selected = self._selected_signals()
        pixels_per_second = max(10, int(self.pixels_per_second.get()))
        elapsed_ms = max(0.0, (self.canvas.canvasx(event.x) - self.LEFT) / pixels_per_second * 1000.0)
        row = self.session.rows[nearest_row_index(self.session.rows, elapsed_ms, self._elapsed_times)]
        marker_x = self.LEFT + float(row["elapsed_ms"]) / 1000.0 * pixels_per_second
        self.canvas.delete("hover")
        chart_bottom = self.TOP + len(selected) * (self.LANE_HEIGHT + self.LANE_GAP) - self.LANE_GAP
        self.canvas.create_line(marker_x, self.TOP, marker_x, chart_bottom, fill=WHITE, dash=(4, 4), tags="hover")
        lines = [f"TIME  {format_duration(float(row['elapsed_ms']) / 1000.0)}"]
        for lane, signal in enumerate(selected):
            label, unit = SIGNALS[signal]
            lines.append(f"{label.upper()}  {format_value(row[signal], unit)}")
            if row[signal] is not None and signal in self._ranges:
                minimum, maximum = self._ranges[signal]
                top = self.TOP + lane * (self.LANE_HEIGHT + self.LANE_GAP)
                y = top + self.LANE_HEIGHT - (float(row[signal]) - minimum) / (maximum - minimum) * self.LANE_HEIGHT
                color = TRACE_COLORS[lane % len(TRACE_COLORS)]
                self.canvas.create_oval(marker_x - 4, y - 4, marker_x + 4, y + 4,
                                        fill=color, outline=WHITE, tags="hover")
        tooltip_height = 24 + len(lines) * 17
        tooltip_x = marker_x + 12
        visible_right = self.canvas.canvasx(self.canvas.winfo_width())
        if tooltip_x + 245 > visible_right:
            tooltip_x = marker_x - 257
        tooltip_y = self.canvas.canvasy(event.y) + 10
        visible_bottom = self.canvas.canvasy(self.canvas.winfo_height())
        if tooltip_y + tooltip_height > visible_bottom:
            tooltip_y = visible_bottom - tooltip_height - 8
        self.canvas.create_rectangle(tooltip_x, tooltip_y, tooltip_x + 245, tooltip_y + tooltip_height,
                                     fill=PANEL, outline="#5b5f68", tags="hover")
        self.canvas.create_text(tooltip_x + 10, tooltip_y + 8, anchor="nw", text="\n".join(lines),
                                fill=WHITE, font=("Cascadia Mono", 9), tags="hover")