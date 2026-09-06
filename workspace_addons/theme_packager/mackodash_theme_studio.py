from __future__ import annotations

import json
import math
import re
import tkinter as tk
from base64 import b64decode
from collections import OrderedDict
from copy import deepcopy
from pathlib import Path
from tkinter import colorchooser, filedialog, messagebox, simpledialog, ttk

from device_transfer_ui import open_theme_upload
from theme_font import PREVIEW_FONT, register_preview_font
from theme_preview import SIMULATED_VALUES, display_text, raw_image_png, resolve_binding
from theme_studio_model import CANVAS_HEIGHT, CANVAS_WIDTH, StudioProjectError, ThemeStudioProject
from utility_ui import LABEL, LINE, PANEL, RED, VOID, WHITE, VerticalScrollFrame


DEFAULT_CANVAS_SCALE = 0.68
CANVAS_MARGIN = 240
IMAGE_PREVIEW_CACHE_MAX = 24
register_preview_font()
BINDINGS = (
    "None", "RPM", "Speed", "Gear", "Coolant", "Intake Air", "AFR", "Timing",
    "Boost / MAP", "Battery", "Throttle", "Oil Pressure", "Injector Duty", "Knock",
    "Fuel", "Odometer", "Speed Unit", "Coolant Unit", "Intake Unit", "Boost Unit",
    "Fuel Unit", "Throttle Unit", "Oil Unit", "CEL", "VTEC", "Shift", "Oil Warning",
    "Knock Warning", "SIM", "Settings", "Record",
)
BINDING_KEYS = {
    "RPM": "rpm", "Speed": "speed", "Gear": "gear", "Coolant": "ect", "Intake Air": "iat",
    "AFR": "afr", "Timing": "timing", "Boost / MAP": "map", "Battery": "batt",
    "Throttle": "tps", "Oil Pressure": "oil", "Injector Duty": "duty", "Knock": "knock",
    "Fuel": "fuel", "Odometer": "odo", "Speed Unit": "speed_unit", "Coolant Unit": "ect_unit",
    "Intake Unit": "iat_unit", "Boost Unit": "map_unit", "Fuel Unit": "fuel_unit",
    "Throttle Unit": "tps_unit", "Oil Unit": "oil_unit", "CEL": "cel_indicator",
    "VTEC": "vtec_indicator", "Shift": "shift_indicator", "Oil Warning": "oil_indicator",
    "Knock Warning": "knock_indicator", "SIM": "sim", "Settings": "settings", "Record": "record",
}
KEY_LABELS = {value: key for key, value in BINDING_KEYS.items()}


def blend_color(foreground: str, background: str, opacity: int) -> str:
    try:
        ratio = max(0, min(255, opacity)) / 255
        foreground_rgb = tuple(int(foreground[index:index + 2], 16) for index in (1, 3, 5))
        background_rgb = tuple(int(background[index:index + 2], 16) for index in (1, 3, 5))
        values = tuple(round(front * ratio + back * (1 - ratio)) for front, back in zip(foreground_rgb, background_rgb))
        return "#" + "".join(f"{value:02X}" for value in values)
    except (ValueError, IndexError):
        return foreground


def rounded_rotated_polygon(x: float, y: float, width: float, height: float,
                            radius: float, angle: float, pivot: tuple[float, float] | None = None) -> list[float]:
    radius = max(0.0, min(radius, width / 2, height / 2))
    points: list[tuple[float, float]] = []
    for center_x, center_y, start in (
        (x + width - radius, y + radius, -90), (x + width - radius, y + height - radius, 0),
        (x + radius, y + height - radius, 90), (x + radius, y + radius, 180),
    ):
        for step in range(5):
            radians = math.radians(start + step * 22.5)
            points.append((center_x + radius * math.cos(radians), center_y + radius * math.sin(radians)))
    center_x, center_y = pivot or (x + width / 2, y + height / 2)
    radians = math.radians(angle)
    cosine, sine = math.cos(radians), math.sin(radians)
    flattened: list[float] = []
    for point_x, point_y in points:
        offset_x, offset_y = point_x - center_x, point_y - center_y
        flattened.extend((center_x + offset_x * cosine - offset_y * sine,
                          center_y + offset_x * sine + offset_y * cosine))
    return flattened

def sample_path(points: list[list[float]], count: int, point_values: list[float] | None = None,
                minimum: float = 0, maximum: float = 1) -> list[tuple[float, float, float]]:
    if len(points) < 2 or count < 1:
        return []
    if point_values and len(point_values) == len(points):
        samples = []
        for index in range(count):
            value = minimum + (maximum - minimum) * (index + 0.5) / count
            segment = next((position for position in range(len(point_values) - 1)
                            if value <= point_values[position + 1]), len(point_values) - 2)
            value_span = point_values[segment + 1] - point_values[segment]
            ratio = min(1.0, max(0.0, (value - point_values[segment]) / max(value_span, 0.001)))
            start, end = points[segment], points[segment + 1]
            samples.append((start[0] + (end[0] - start[0]) * ratio,
                            start[1] + (end[1] - start[1]) * ratio,
                            math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))))
        return samples
    lengths = [math.hypot(end[0] - start[0], end[1] - start[1])
               for start, end in zip(points, points[1:])]
    total = sum(lengths)
    if total <= 0:
        return []
    samples = []
    for index in range(count):
        distance = total * (index + 0.5) / count
        traversed = 0.0
        for segment, length in enumerate(lengths):
            if distance <= traversed + length or segment == len(lengths) - 1:
                ratio = min(1.0, max(0.0, (distance - traversed) / max(length, 0.001)))
                start, end = points[segment], points[segment + 1]
                samples.append((start[0] + (end[0] - start[0]) * ratio,
                                start[1] + (end[1] - start[1]) * ratio,
                                math.degrees(math.atan2(end[1] - start[1], end[0] - start[0]))))
                break
            traversed += length
    return samples


def binding_name(binding: str, object_type: str) -> str:
    key = BINDING_KEYS[binding]
    if key == "settings":
        return "dash_settings_button"
    if key == "record":
        return "dash_record_button"
    if key == "sim":
        return "dash_sim_button"
    if key.endswith("indicator"):
        return f"dash_{key}"
    suffix = "value" if object_type in {"label", "button"} else "bar" if object_type in {"needle", "analog_tach", "analog_speedo"} else object_type
    return f"dash_{key}_{suffix}"


class ThemeStudioFrame(ttk.Frame):
    def __init__(self, parent: tk.Misc) -> None:
        super().__init__(parent)
        self.project = ThemeStudioProject()
        self.project_path: Path | None = None
        self.last_export: Path | None = None
        self.selected_index: int | None = None
        self.drag_origin: tuple[str, int, int, int, int, int, int] | None = None
        self.path_editing = False
        self._canvas_initialized = False
        self._canvas_images: list[tk.PhotoImage] = []
        self._image_preview_cache: OrderedDict[tuple[object, ...], tk.PhotoImage] = OrderedDict()
        self.undo_stack: list[dict] = []
        self.redo_stack: list[dict] = []
        self.scenario = tk.StringVar(value="typical")
        self.zoom = tk.StringVar(value="100%")
        self.snap_enabled = tk.BooleanVar(value=True)
        self.grid_size = tk.IntVar(value=10)
        self.template = tk.StringVar(value="Performance")
        self.status = tk.StringVar(value="Create a widget, then drag it into place.")
        self.name_var = tk.StringVar(value=self.project.name)
        self.id_var = tk.StringVar(value=self.project.theme_id)
        self.background_var = tk.StringVar(value=self.project.background)
        self.background_opa_var = tk.IntVar(value=self.project.background_opa)
        self.property_vars = {key: tk.StringVar() for key in (
            "name", "binding", "x", "y", "width", "height", "text", "font_size",
            "align", "color", "background", "track_color", "min", "max", "preview_value", "rotation", "sweep", "end_angle",
            "alert_color", "alert_start", "segment_count", "segment_width", "segment_height",
            "track_opa", "indicator_opa", "radius", "transform_angle", "zoom",
            "needle_color", "needle_width", "needle_offset", "tick_color", "major_tick_color",
            "tick_count", "major_tick_every", "tick_width", "tick_length", "major_tick_width",
            "major_tick_length", "label_gap", "arc_width", "show_value", "value_color",
            "value_font_size", "value_y",
            "background_opa", "border_color", "border_width", "tick_label_font_size",
            "color_opa", "needle_opa", "tick_opa", "major_tick_opa", "value_opa", "border_opa",
        )}
        self._build_ui()
        self._bind_shortcuts()
        self._redraw()
        self.after_idle(self._center_canvas)

    def _build_ui(self) -> None:
        toolbar = ttk.Frame(self, padding=(16, 10))
        toolbar.pack(fill="x")
        ttk.Button(toolbar, text="New", style="Quiet.TButton", command=self._new).pack(side="left")
        ttk.Button(toolbar, text="Open", style="Quiet.TButton", command=self._open).pack(side="left", padx=(4, 0))
        ttk.Button(toolbar, text="Save", style="Quiet.TButton", command=self._save).pack(side="left", padx=(4, 0))
        ttk.Separator(toolbar, orient="vertical").pack(side="left", fill="y", padx=12)
        ttk.Button(toolbar, text="Undo", style="Quiet.TButton", command=self._undo).pack(side="left")
        ttk.Button(toolbar, text="Redo", style="Quiet.TButton", command=self._redo).pack(side="left", padx=(4, 0))
        ttk.Button(toolbar, text="Export Theme", style="Accent.TButton", command=self._export).pack(side="right")
        ttk.Button(toolbar, text="Send over USB", command=self._send).pack(side="right", padx=(0, 8))

        workspace = ttk.Panedwindow(self, orient="horizontal")
        workspace.pack(fill="both", expand=True)

        library = ttk.Frame(workspace, style="Tile.TFrame", padding=14, width=210)
        workspace.add(library, weight=0)
        ttk.Label(library, text="WIDGETS", style="TileTitle.TLabel").pack(anchor="w")
        ttk.Label(library, text="Add an element to the canvas", style="TileHint.TLabel").pack(anchor="w", pady=(2, 12))
        widget_grid = ttk.Frame(library, style="Tile.TFrame")
        widget_grid.pack(fill="x")
        for index, object_type in enumerate(("label", "bar", "arc", "path_gauge", "needle", "analog_tach", "analog_speedo",
                             "button", "indicator", "object")):
            ttk.Button(widget_grid, text=object_type.title(),
                       command=lambda kind=object_type: self._add(kind)).grid(
                           row=index // 2, column=index % 2, sticky="ew", padx=(0 if index % 2 == 0 else 4, 0), pady=2)
        widget_grid.columnconfigure((0, 1), weight=1)
        ttk.Button(library, text="Import Image", command=self._import_image).pack(fill="x", pady=(6, 18))
        ttk.Separator(library).pack(fill="x", pady=(0, 16))
        ttk.Label(library, text="STARTING POINT", style="Section.TLabel").pack(anchor="w", pady=(0, 6))
        ttk.Combobox(library, textvariable=self.template, values=("Performance", "Street"),
                     state="readonly").pack(fill="x")
        ttk.Button(library, text="Use Template", command=self._use_template).pack(fill="x", pady=(6, 18))
        ttk.Separator(library).pack(fill="x", pady=(0, 16))
        ttk.Label(library, text="PROJECT", style="Section.TLabel").pack(anchor="w", pady=(0, 8))
        for label, variable in (("Theme name", self.name_var), ("Theme ID", self.id_var)):
            ttk.Label(library, text=label, style="TileHint.TLabel").pack(anchor="w", pady=(5, 2))
            ttk.Entry(library, textvariable=variable).pack(fill="x")
        ttk.Label(library, text="Background", style="TileHint.TLabel").pack(anchor="w", pady=(7, 2))
        background_row = ttk.Frame(library, style="Tile.TFrame")
        background_row.pack(fill="x")
        ttk.Entry(background_row, textvariable=self.background_var, width=10).pack(side="left", fill="x", expand=True)
        ttk.Button(background_row, text="Color",
                   command=self._pick_background_color).pack(side="right", padx=(4, 0))
        ttk.Button(library, text="Transparent Background", style="Quiet.TButton",
               command=self._make_background_transparent).pack(fill="x", pady=(5, 0))
        for variable in (self.name_var, self.id_var, self.background_var):
            variable.trace_add("write", lambda *_args: self._apply_identity())

        workbench = ttk.Frame(workspace, style="Surface.TFrame", padding=(10, 8))
        workspace.add(workbench, weight=1)
        canvas_toolbar = ttk.Frame(workbench, style="Surface.TFrame")
        canvas_toolbar.pack(fill="x", pady=(0, 8))
        ttk.Label(canvas_toolbar, text="CANVAS", style="Surface.TLabel").pack(side="left")
        ttk.Button(canvas_toolbar, text="Duplicate", style="Quiet.TButton", command=self._duplicate).pack(side="left", padx=(12, 4))
        ttk.Button(canvas_toolbar, text="Delete", style="Quiet.TButton", command=self._delete).pack(side="left")
        ttk.Combobox(canvas_toolbar, textvariable=self.grid_size, values=(5, 10, 20), state="readonly", width=3).pack(side="right")
        ttk.Checkbutton(canvas_toolbar, text="Snap", variable=self.snap_enabled, command=self._redraw).pack(side="right", padx=(8, 4))
        ttk.Button(canvas_toolbar, text="+", style="Quiet.TButton", width=3,
               command=lambda: self._step_zoom(25)).pack(side="right", padx=(3, 0))
        ttk.Button(canvas_toolbar, text="-", style="Quiet.TButton", width=3,
               command=lambda: self._step_zoom(-25)).pack(side="right", padx=(6, 0))
        ttk.Button(canvas_toolbar, text="Fit", style="Quiet.TButton",
               command=self._fit_zoom).pack(side="right", padx=(6, 0))
        zoom = ttk.Combobox(canvas_toolbar, textvariable=self.zoom,
                    values=("50%", "75%", "100%", "125%", "150%", "175%", "200%"),
                    state="readonly", width=6)
        zoom.pack(side="right")
        zoom.bind("<<ComboboxSelected>>", lambda _event: self._redraw())
        ttk.Label(canvas_toolbar, text="Zoom", style="Surface.TLabel").pack(side="right", padx=(0, 6))
        scenario = ttk.Combobox(canvas_toolbar, textvariable=self.scenario, values=("typical", "longest"), state="readonly", width=9)
        scenario.pack(side="right", padx=(0, 12))
        scenario.bind("<<ComboboxSelected>>", lambda _event: self._redraw())
        ttk.Label(canvas_toolbar, text="Data", style="Surface.TLabel").pack(side="right", padx=(0, 6))
        canvas_shell = tk.Frame(workbench, background="#08090b", highlightbackground=LINE, highlightthickness=1)
        canvas_shell.pack(fill="both", expand=True)
        canvas_shell.columnconfigure(0, weight=1)
        canvas_shell.rowconfigure(0, weight=1)
        self.canvas = tk.Canvas(canvas_shell, width=round(CANVAS_WIDTH * DEFAULT_CANVAS_SCALE),
                                height=round(CANVAS_HEIGHT * DEFAULT_CANVAS_SCALE), background="#000000",
                                highlightthickness=0, cursor="crosshair")
        horizontal_scroll = ttk.Scrollbar(canvas_shell, orient="horizontal", command=self.canvas.xview)
        vertical_scroll = ttk.Scrollbar(canvas_shell, orient="vertical", command=self.canvas.yview)
        self.canvas.configure(xscrollcommand=horizontal_scroll.set, yscrollcommand=vertical_scroll.set)
        self.canvas.grid(row=0, column=0, sticky="nsew", padx=(16, 0), pady=(16, 0))
        vertical_scroll.grid(row=0, column=1, sticky="ns", padx=(0, 4), pady=(16, 0))
        horizontal_scroll.grid(row=1, column=0, sticky="ew", padx=(16, 0), pady=(0, 4))
        self.canvas.bind("<Button-1>", self._canvas_press)
        self.canvas.bind("<B1-Motion>", self._canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self._canvas_release)
        self.canvas.bind("<Control-MouseWheel>", self._canvas_zoom_wheel)
        self.canvas.bind("<MouseWheel>", self._canvas_scroll_wheel)
        self.canvas.bind("<Shift-MouseWheel>", self._canvas_scroll_horizontal)
        self.canvas.bind("<ButtonPress-2>", self._canvas_pan_start)
        self.canvas.bind("<B2-Motion>", self._canvas_pan_move)
        self.canvas.bind("<ButtonRelease-2>", lambda _event: self.canvas.configure(cursor="crosshair"))

        self.inspector = ttk.Notebook(workspace, width=410)
        workspace.add(self.inspector, weight=0)
        layers_tab = ttk.Frame(self.inspector, style="Tile.TFrame", padding=12)
        self.properties_tab = ttk.Frame(self.inspector, style="Tile.TFrame", padding=12)
        self.inspector.add(layers_tab, text="Layers")
        self.inspector.add(self.properties_tab, text="Properties")
        ttk.Label(layers_tab, text="OBJECT HIERARCHY", style="Section.TLabel").pack(anchor="w", pady=(0, 8))
        self.layers = tk.Listbox(layers_tab, background=VOID, foreground=WHITE,
                                 selectbackground=RED, selectforeground=WHITE, relief="flat",
                                 highlightbackground=LINE, highlightthickness=1, exportselection=False)
        self.layers.pack(fill="both", expand=True, pady=(0, 10))
        self.layers.bind("<<ListboxSelect>>", self._layer_selected)
        layer_controls = ttk.Frame(layers_tab, style="Tile.TFrame")
        layer_controls.pack(fill="x")
        ttk.Button(layer_controls, text="Forward", command=lambda: self._move_layer(1)).pack(side="left", fill="x", expand=True)
        ttk.Button(layer_controls, text="Backward", command=lambda: self._move_layer(-1)).pack(side="left", fill="x", expand=True, padx=(6, 0))
        ttk.Label(self.properties_tab, text="SELECTED OBJECT", style="Section.TLabel").pack(anchor="w", pady=(0, 8))
        properties_scroll = VerticalScrollFrame(self.properties_tab)
        properties_scroll.pack(fill="both", expand=True)
        properties_scroll.content.configure(style="Tile.TFrame")
        properties = ttk.Frame(properties_scroll.content, style="Tile.TFrame")
        properties.pack(fill="both", expand=True)
        rows = (
            ("Name", "name"), ("Data", "binding"), ("X", "x"), ("Y", "y"),
            ("Width", "width"), ("Height", "height"), ("Text", "text"),
            ("Font size", "font_size"), ("Text align", "align"), ("Color", "color"),
            ("Background", "background"), ("Track", "track_color"),
            ("Minimum", "min"), ("Maximum", "max"), ("Preview value", "preview_value"),
            ("Alert above", "alert_start"), ("Alert color", "alert_color"),
            ("Segments", "segment_count"), ("Segment width", "segment_width"),
            ("Segment height", "segment_height"),
            ("Start angle", "rotation"),
            ("End angle", "end_angle"), ("Track opacity", "track_opa"),
            ("Fill opacity", "indicator_opa"), ("Corner radius", "radius"),
            ("Rotation", "transform_angle"), ("Image zoom", "zoom"),
            ("Needle color", "needle_color"), ("Needle width", "needle_width"),
            ("Needle length", "needle_offset"), ("Tick color", "tick_color"),
            ("Major tick color", "major_tick_color"), ("Tick count", "tick_count"),
            ("Major every", "major_tick_every"), ("Tick width", "tick_width"),
            ("Tick length", "tick_length"), ("Major width", "major_tick_width"),
            ("Major length", "major_tick_length"), ("Label gap", "label_gap"),
            ("Arc width", "arc_width"), ("Show value", "show_value"),
            ("Value color", "value_color"), ("Value font", "value_font_size"),
            ("Value Y", "value_y"),
            ("Face opacity", "background_opa"), ("Border color", "border_color"),
            ("Border width", "border_width"), ("Tick label font", "tick_label_font_size"),
            ("Color opacity", "color_opa"), ("Needle opacity", "needle_opa"),
            ("Tick opacity", "tick_opa"), ("Major tick opacity", "major_tick_opa"),
            ("Value opacity", "value_opa"), ("Border opacity", "border_opa"),
        )
        self.property_rows: dict[str, list[tk.Widget]] = {}
        for row, (label, key) in enumerate(rows):
            label_widget = ttk.Label(properties, text=label, style="Tile.TLabel")
            label_widget.grid(row=row, column=0, sticky="w", pady=2)
            if key == "binding":
                control = ttk.Combobox(properties, textvariable=self.property_vars[key], values=BINDINGS, state="readonly", width=17)
                control.bind("<<ComboboxSelected>>", lambda _event: self._apply_properties())
            elif key == "align":
                control = ttk.Combobox(properties, textvariable=self.property_vars[key],
                                       values=("left", "center", "right"), state="readonly", width=17)
                control.bind("<<ComboboxSelected>>", lambda _event: self._apply_properties())
            elif key == "show_value":
                control = ttk.Combobox(properties, textvariable=self.property_vars[key],
                                       values=("Yes", "No"), state="readonly", width=17)
                control.bind("<<ComboboxSelected>>", lambda _event: self._apply_properties())
            else:
                control = ttk.Entry(properties, textvariable=self.property_vars[key], width=19)
                control.bind("<Return>", lambda _event: self._apply_properties())
                control.bind("<FocusOut>", lambda _event: self._apply_properties())
            control.grid(row=row, column=1, sticky="ew", padx=(8, 0), pady=2)
            widgets: list[tk.Widget] = [label_widget, control]
            if key in {"color", "background", "track_color", "alert_color", "needle_color", "tick_color",
                       "major_tick_color", "value_color", "border_color"}:
                color_button = ttk.Button(properties, text="...", width=3,
                                          command=lambda color_key=key: self._pick_property_color(color_key))
                color_button.grid(row=row, column=2, padx=(4, 0), pady=2)
                widgets.append(color_button)
                transparent_button = ttk.Button(properties, text="Transparent",
                                                command=lambda color_key=key: self._make_property_transparent(color_key))
                transparent_button.grid(row=row, column=3, padx=(4, 0), pady=2)
                widgets.append(transparent_button)
            self.property_rows[key] = widgets
        properties.columnconfigure(1, weight=1)
        self.bar_image_actions = ttk.Frame(properties_scroll.content, style="Tile.TFrame")
        ttk.Button(self.bar_image_actions, text="Set Fill Image", command=self._import_bar_image).pack(
            side="left", fill="x", expand=True)
        ttk.Button(self.bar_image_actions, text="Clear", command=self._clear_bar_image).pack(side="left", padx=(6, 0))
        self.path_actions = ttk.Frame(properties_scroll.content, style="Tile.TFrame")
        ttk.Button(self.path_actions, text="Draw Path", command=self._start_path_edit).pack(
            side="left", fill="x", expand=True)
        ttk.Button(self.path_actions, text="Undo Dot", command=self._undo_path_point).pack(side="left", padx=(6, 0))
        ttk.Button(self.path_actions, text="Finish", command=self._finish_path_edit).pack(side="left", padx=(6, 0))
        ttk.Button(self.properties_tab, text="Apply Properties", style="Accent.TButton",
               command=self._apply_properties).pack(fill="x", pady=(10, 0))

        status_bar = ttk.Frame(self, style="Surface.TFrame", padding=(14, 7))
        status_bar.pack(fill="x")
        ttk.Label(status_bar, textvariable=self.status, style="Surface.TLabel").pack(side="left")
        ttk.Label(status_bar, text="1024 x 600", style="Surface.TLabel").pack(side="right")

    def _bind_shortcuts(self) -> None:
        root = self.winfo_toplevel()
        root.bind("<Control-z>", lambda _event: self._undo())
        root.bind("<Control-y>", lambda _event: self._redo())
        root.bind("<Control-plus>", lambda _event: self._step_zoom(25))
        root.bind("<Control-minus>", lambda _event: self._step_zoom(-25))
        root.bind("<Control-0>", lambda _event: self._fit_zoom())
        root.bind("<Delete>", lambda _event: self._delete())
        for key, delta in (("Left", (-1, 0)), ("Right", (1, 0)), ("Up", (0, -1)), ("Down", (0, 1))):
            root.bind(f"<{key}>", lambda event, move=delta: self._nudge(event, *move))

    def _snapshot(self) -> dict:
        return self.project.snapshot()

    def _restore(self, document: dict) -> None:
        self.project = ThemeStudioProject(
            theme_id=document["theme_id"], name=document["name"],
            background=document["background"], objects=deepcopy(document["objects"]),
            background_opa=int(document.get("background_opa", 255)),
        )
        self.name_var.set(self.project.name)
        self.id_var.set(self.project.theme_id)
        self.background_var.set(self.project.background)
        self.background_opa_var.set(self.project.background_opa)
        self.selected_index = None
        self._redraw()

    def _record_change(self) -> None:
        self.undo_stack.append(self._snapshot())
        self.undo_stack = self.undo_stack[-80:]
        self.redo_stack.clear()

    def _undo(self) -> None:
        if not self.undo_stack:
            return
        self.redo_stack.append(self._snapshot())
        self._restore(self.undo_stack.pop())

    def _redo(self) -> None:
        if not self.redo_stack:
            return
        self.undo_stack.append(self._snapshot())
        self._restore(self.redo_stack.pop())

    def _add(self, object_type: str) -> None:
        self._record_change()
        offset = 36 + len(self.project.objects) * 10
        try:
            item = self.project.add_object(object_type, offset % 700, offset % 380)
        except StudioProjectError as error:
            self.undo_stack.pop()
            messagebox.showerror("Cannot add object", str(error), parent=self)
            return
        self.selected_index = self.project.objects.index(item)
        self._redraw()

    def _duplicate(self) -> None:
        if self.selected_index is None:
            return
        self._record_change()
        try:
            item = self.project.duplicate(self.selected_index)
        except StudioProjectError as error:
            self.undo_stack.pop()
            messagebox.showerror("Cannot duplicate object", str(error), parent=self)
            return
        self.selected_index = self.project.objects.index(item)
        self._redraw()

    def _delete(self) -> None:
        if self.selected_index is None:
            return
        self._record_change()
        del self.project.objects[self.selected_index]
        self.selected_index = None
        self._redraw()

    def _canvas_press(self, event: tk.Event) -> None:
        pointer_x = int(self.canvas.canvasx(event.x))
        pointer_y = int(self.canvas.canvasy(event.y))
        if self.path_editing and self.selected_index is not None:
            item = self.project.objects[self.selected_index]
            local_x = round((pointer_x - CANVAS_MARGIN) / self.scale) - int(item["x"])
            local_y = round((pointer_y - CANVAS_MARGIN) / self.scale) - int(item["y"])
            if 0 <= local_x <= int(item["width"]) and 0 <= local_y <= int(item["height"]):
                if len(item["points"]) < 24:
                    minimum = int(item.get("min", 0))
                    maximum = int(item.get("max", 8000))
                    values = item.setdefault("point_values", [])
                    if values and values[-1] >= maximum:
                        self.status.set("Maximum RPM dot is placed. Choose Finish or Undo Dot.")
                        return
                    suggested = minimum if not values else min(maximum, int(values[-1]) + 1000)
                    rpm = simpledialog.askinteger(
                        "Calibrate path point", "RPM value for this dot:", parent=self,
                        initialvalue=suggested,
                        minvalue=minimum if not values else int(values[-1]) + 1,
                        maxvalue=maximum,
                    )
                    if rpm is None:
                        return
                    item["points"].append([local_x, local_y])
                    values.append(rpm)
                    self._redraw()
                else:
                    self.status.set("Path Gauge supports up to 24 control points.")
            return
        found = self.canvas.find_overlapping(pointer_x, pointer_y, pointer_x, pointer_y)
        resizing = any("resize" in self.canvas.gettags(item) for item in found)
        index = next((int(self.canvas.gettags(item)[1]) for item in reversed(found)
                      if len(self.canvas.gettags(item)) > 1 and self.canvas.gettags(item)[0] == "object"), None)
        if resizing:
            index = self.selected_index
        self.selected_index = index
        if index is not None:
            self.inspector.select(self.properties_tab)
            item = self.project.objects[index]
            self.drag_origin = ("resize" if resizing else "move", pointer_x, pointer_y, int(item["x"]),
                                int(item["y"]), int(item["width"]), int(item["height"]))
        self._redraw()

    def _canvas_drag(self, event: tk.Event) -> None:
        if self.selected_index is None or self.drag_origin is None:
            return
        mode, start_x, start_y, object_x, object_y, object_width, object_height = self.drag_origin
        pointer_x = int(self.canvas.canvasx(event.x))
        pointer_y = int(self.canvas.canvasy(event.y))
        item = self.project.objects[self.selected_index]
        if mode == "resize":
            item["width"] = object_width + round((pointer_x - start_x) / self.scale)
            item["height"] = object_height + round((pointer_y - start_y) / self.scale)
        else:
            item["x"] = object_x + round((pointer_x - start_x) / self.scale)
            item["y"] = object_y + round((pointer_y - start_y) / self.scale)
            if self.snap_enabled.get():
                item["x"] = self.project.snap(int(item["x"]), self.grid_size.get())
                item["y"] = self.project.snap(int(item["y"]), self.grid_size.get())
        self.project.constrain(item)
        self._redraw()

    def _canvas_release(self, event: tk.Event) -> None:
        if self.drag_origin is not None and self.selected_index is not None:
            mode, start_x, start_y, object_x, object_y, object_width, object_height = self.drag_origin
            pointer_x = int(self.canvas.canvasx(event.x))
            pointer_y = int(self.canvas.canvasy(event.y))
            if pointer_x != start_x or pointer_y != start_y:
                item = self.project.objects[self.selected_index]
                current = (item["x"], item["y"], item["width"], item["height"])
                item.update({"x": object_x, "y": object_y, "width": object_width, "height": object_height})
                self._record_change()
                item.update(dict(zip(("x", "y", "width", "height"), current)))
        self.drag_origin = None
        self._load_properties()
        self._redraw()

    def _start_path_edit(self) -> None:
        if self.selected_index is None or self.project.objects[self.selected_index].get("type") != "path_gauge":
            return
        self._record_change()
        self.project.objects[self.selected_index]["points"] = []
        self.project.objects[self.selected_index]["point_values"] = []
        self.path_editing = True
        self.status.set("Click each printed RPM mark in order and enter its value, then choose Finish.")
        self._redraw()

    def _undo_path_point(self) -> None:
        if not self.path_editing or self.selected_index is None:
            return
        item = self.project.objects[self.selected_index]
        if item.get("points"):
            item["points"].pop()
        if item.get("point_values"):
            item["point_values"].pop()
        self._redraw()

    def _finish_path_edit(self) -> None:
        if not self.path_editing or self.selected_index is None:
            return
        item = self.project.objects[self.selected_index]
        if len(item.get("points", [])) < 2:
            messagebox.showerror("Path incomplete", "Add at least two points before finishing.", parent=self)
            return
        values = item.get("point_values", [])
        minimum, maximum = int(item.get("min", 0)), int(item.get("max", 8000))
        if not values or values[0] != minimum or values[-1] != maximum:
            messagebox.showerror("Calibration incomplete",
                                 f"The first dot must be {minimum} RPM and the last dot must be {maximum} RPM.",
                                 parent=self)
            return
        self.path_editing = False
        self._redraw()

    def _nudge(self, event: tk.Event, dx: int, dy: int) -> None:
        if self.selected_index is None or isinstance(event.widget, (tk.Entry, ttk.Entry, ttk.Combobox)):
            return
        self._record_change()
        item = self.project.objects[self.selected_index]
        amount = 10 if event.state & 0x0001 else 1
        item["x"] = int(item["x"]) + dx * amount
        item["y"] = int(item["y"]) + dy * amount
        self.project.constrain(item)
        self._redraw()

    def _layer_selected(self, _event: tk.Event) -> None:
        selection = self.layers.curselection()
        if selection:
            self.selected_index = len(self.project.objects) - 1 - selection[0]
            self.inspector.select(self.properties_tab)
            self._redraw()

    def _move_layer(self, direction: int) -> None:
        if self.selected_index is None:
            return
        self._record_change()
        self.selected_index = self.project.move_layer(self.selected_index, direction)
        self._redraw()

    def _use_template(self) -> None:
        if self.project.objects and not messagebox.askyesno("Use template", "Replace the current canvas with this template?", parent=self):
            return
        self._record_change()
        template = ThemeStudioProject.from_template(self.template.get())
        template.theme_id = self.project.theme_id
        template.name = self.project.name
        template.background = self.project.background
        template.background_opa = self.project.background_opa
        self.project = template
        self.selected_index = None
        self._redraw()

    def _import_image(self) -> None:
        selected = filedialog.askopenfilename(parent=self, title="Import image",
                                              filetypes=[("Supported images", "*.png *.gif"), ("PNG images", "*.png"), ("GIF images", "*.gif")])
        if not selected:
            return
        try:
            width, height, data = self._decode_image(selected)
            self._record_change()
            name = re.sub(r"[^a-z0-9]+", "_", Path(selected).stem.lower()).strip("_") or "image"
            item = self.project.add_image(name, width, height, data, 40, 40)
        except (tk.TclError, OSError, StudioProjectError, ValueError) as error:
            messagebox.showerror("Cannot import image", str(error), parent=self)
            return
        self.selected_index = self.project.objects.index(item)
        self._redraw()

    def _decode_image(self, selected: str) -> tuple[int, int, bytes]:
        image = tk.PhotoImage(file=selected)
        width, height = image.width(), image.height()
        if width < 1 or height < 1 or width > 1024 or height > 600 or width * height * 3 > 2 * 1024 * 1024:
            raise StudioProjectError("Images must fit within 1024x600 and use no more than 2 MiB")
        data = bytearray()
        transparency = getattr(image, "transparency_get", None)
        for y in range(height):
            for x in range(width):
                pixel = image.get(x, y)
                values = tuple(int(value) for value in re.findall(r"\d+", pixel)) if isinstance(pixel, str) else tuple(pixel)
                red, green, blue = values[:3]
                rgb565 = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
                alpha = 0 if transparency and transparency(x, y) else 255
                data.extend((rgb565 & 0xff, rgb565 >> 8, alpha))
        return width, height, bytes(data)

    def _import_bar_image(self) -> None:
        if self.selected_index is None or self.project.objects[self.selected_index].get("type") != "bar":
            return
        selected = filedialog.askopenfilename(parent=self, title="Choose bar fill image",
                                              filetypes=[("Supported images", "*.png *.gif"), ("PNG images", "*.png"), ("GIF images", "*.gif")])
        if not selected:
            return
        try:
            width, height, data = self._decode_image(selected)
            self._record_change()
            self.project.set_bar_image(self.project.objects[self.selected_index], Path(selected).stem, width, height, data)
        except (tk.TclError, OSError, StudioProjectError, ValueError) as error:
            messagebox.showerror("Cannot set bar image", str(error), parent=self)
            return
        self._redraw()

    def _clear_bar_image(self) -> None:
        if self.selected_index is None:
            return
        item = self.project.objects[self.selected_index]
        keys = ("indicator_asset", "indicator_format", "indicator_source_width",
                "indicator_source_height", "indicator_asset_data")
        if not any(key in item for key in keys):
            return
        self._record_change()
        for key in keys:
            item.pop(key, None)
        self._redraw()

    def _apply_identity(self) -> None:
        self.project.name = self.name_var.get()
        self.project.theme_id = self.id_var.get()
        self.project.background = self.background_var.get()
        self.project.background_opa = self.background_opa_var.get()

    def _make_background_transparent(self) -> None:
        self.background_opa_var.set(0)
        self._apply_identity()
        self._redraw()

    def _pick_background_color(self) -> None:
        selected = colorchooser.askcolor(self.background_var.get(), parent=self)[1]
        if selected:
            self.background_var.set(selected.upper())
            self.background_opa_var.set(255)
            self._apply_identity()
            self._redraw()

    def _load_properties(self) -> None:
        item = self.project.objects[self.selected_index] if self.selected_index is not None else None
        defaults = {"align": "left", "rotation": 135, "sweep": 270, "preview_value": 68, "track_opa": 255,
                    "alert_color": "#E4002B", "alert_start": 6500, "segment_count": 36,
                    "segment_width": 18, "segment_height": 10,
                    "indicator_opa": 255, "radius": 0, "transform_angle": 0, "zoom": 256,
                    "needle_color": "#E4002B", "needle_width": 5, "needle_offset": -12,
                    "tick_color": "#A1A6B0", "major_tick_color": "#FFFFFF", "tick_count": 19,
                    "major_tick_every": 2, "tick_width": 2, "tick_length": 12,
                    "major_tick_width": 4, "major_tick_length": 20, "label_gap": 10,
                    "arc_width": 8, "show_value": True, "value_color": "#FFFFFF",
                    "value_font_size": 28, "value_y": 45, "background_opa": 0,
                    "border_color": "#30343D", "border_width": 0, "tick_label_font_size": 14,
                    "color_opa": 255, "needle_opa": 255, "tick_opa": 255,
                    "major_tick_opa": 255, "value_opa": 255, "border_opa": 255}
        for key, variable in self.property_vars.items():
            if key == "binding":
                variable.set(KEY_LABELS.get(resolve_binding(str(item.get("name", ""))) if item else None, "None"))
            elif key == "end_angle":
                variable.set(str((int(item.get("rotation", 135)) + int(item.get("sweep", 270))) % 360) if item else "")
            elif key == "show_value":
                variable.set("Yes" if item and bool(item.get("show_value", True)) else "No")
            else:
                variable.set(str(item.get(key, defaults.get(key, ""))) if item else "")
        self._update_property_visibility(str(item.get("type")) if item else "")

    def _update_property_visibility(self, object_type: str) -> None:
        common = {"name", "x", "y", "width", "height"}
        relevant = {
            "bar": common | {"binding", "color", "track_color", "min", "max", "preview_value", "track_opa",
                             "indicator_opa", "radius", "transform_angle"},
            "path_gauge": common | {"binding", "color", "alert_color", "alert_start", "track_color",
                                    "min", "max", "preview_value", "track_opa", "indicator_opa",
                                    "segment_count", "segment_width", "segment_height"},
              "label": common | {"binding", "text", "font_size", "align", "color", "color_opa"},
              "arc": common | {"binding", "color", "color_opa", "track_color", "track_opa", "min", "max", "preview_value", "rotation", "end_angle"},
            "needle": common | {"binding", "min", "max", "rotation", "end_angle", "needle_color",
                              "needle_opa", "needle_width", "needle_offset"},
            "analog_tach": common | {"binding", "min", "max", "rotation", "end_angle", "track_color",
                                  "track_opa", "background", "background_opa", "border_color", "border_opa", "border_width",
                                  "needle_color", "needle_opa", "needle_width", "needle_offset", "tick_color", "tick_opa",
                                  "major_tick_color", "major_tick_opa", "tick_count", "major_tick_every", "tick_width",
                                     "tick_length", "major_tick_width", "major_tick_length", "label_gap",
                                  "arc_width", "tick_label_font_size", "show_value", "value_color", "value_opa",
                                     "value_font_size", "value_y"},
              "analog_speedo": common | {"binding", "min", "max", "rotation", "end_angle", "track_color",
                                    "track_opa", "background", "background_opa", "border_color", "border_opa", "border_width",
                                    "needle_color", "needle_opa", "needle_width", "needle_offset", "tick_color", "tick_opa",
                                    "major_tick_color", "major_tick_opa", "tick_count", "major_tick_every", "tick_width",
                                    "tick_length", "major_tick_width", "major_tick_length", "label_gap", "arc_width",
                                    "tick_label_font_size", "show_value", "value_color", "value_opa", "value_font_size", "value_y"},
              "button": common | {"binding", "text", "color", "color_opa", "background", "background_opa"},
              "indicator": common | {"binding", "color", "color_opa"},
              "object": common | {"color", "color_opa", "background", "background_opa"},
            "image": common | {"zoom"},
        }.get(object_type, set())
        self.relevant_property_keys = relevant
        for key, widgets in self.property_rows.items():
            for widget in widgets:
                widget.grid() if key in relevant else widget.grid_remove()
        if object_type == "bar":
            self.bar_image_actions.pack(fill="x", pady=(10, 0))
        else:
            self.bar_image_actions.pack_forget()
        if object_type == "path_gauge":
            self.path_actions.pack(fill="x", pady=(10, 0))
        else:
            self.path_actions.pack_forget()

    def _apply_properties(self) -> None:
        if self.selected_index is None:
            return
        item = self.project.objects[self.selected_index]
        try:
            updates: dict[str, object] = {}
            for key in ("x", "y", "width", "height", "font_size", "min", "max", "rotation", "sweep",
                        "alert_start", "segment_count", "segment_width", "segment_height",
                        "track_opa", "indicator_opa", "radius", "transform_angle", "zoom", "needle_width",
                        "needle_offset", "tick_count", "major_tick_every", "tick_width", "tick_length",
                        "major_tick_width", "major_tick_length", "label_gap", "arc_width", "value_font_size",
                        "value_y", "background_opa", "border_width", "tick_label_font_size", "color_opa",
                        "needle_opa", "tick_opa", "major_tick_opa", "value_opa", "border_opa"):
                if key not in self.relevant_property_keys:
                    continue
                value = self.property_vars[key].get().strip()
                if value:
                    updates[key] = int(value)
            if "preview_value" in self.relevant_property_keys:
                value = self.property_vars["preview_value"].get().strip()
                if value:
                    updates["preview_value"] = float(value)
            for key in ("name", "text", "align", "color", "background", "track_color", "alert_color", "needle_color",
                        "tick_color", "major_tick_color", "value_color", "border_color"):
                if key not in self.relevant_property_keys:
                    continue
                value = self.property_vars[key].get().strip()
                if value:
                    updates[key] = value
            binding = self.property_vars["binding"].get()
            if binding != "None":
                updates["name"] = binding_name(binding, str(item["type"]))
            if "end_angle" in self.relevant_property_keys:
                start = int(updates.get("rotation", item.get("rotation", 135)))
                end = int(self.property_vars["end_angle"].get())
                if not 0 <= end <= 359:
                    raise StudioProjectError("End angle must be between 0 and 359")
                updates["sweep"] = (end - start) % 360 or 360
            if "show_value" in self.relevant_property_keys:
                updates["show_value"] = self.property_vars["show_value"].get() == "Yes"
            candidate = deepcopy(item)
            candidate.update(updates)
            candidate["name"] = self.project.unique_name(str(candidate["name"]), item)
            self.project.constrain(candidate)
            validation_project = ThemeStudioProject(theme_id="customer.validation", name="Validation", objects=[candidate])
            validation_project.validate()
        except (ValueError, StudioProjectError) as error:
            messagebox.showerror("Invalid property", str(error), parent=self)
            self._load_properties()
            return
        if candidate != item:
            self._record_change()
            item.clear()
            item.update(candidate)
            self._redraw()

    def _pick_color(self, variable: tk.StringVar, callback=None) -> None:
        selected = colorchooser.askcolor(variable.get(), parent=self)[1]
        if selected:
            variable.set(selected.upper())
            if callback:
                callback()

    def _opacity_key_for_color(self, color_key: str) -> str:
        if color_key in {"color", "alert_color"} and self.selected_index is not None:
            object_type = self.project.objects[self.selected_index].get("type")
            return "indicator_opa" if object_type in {"bar", "path_gauge"} else "color_opa"
        return {
            "background": "background_opa", "track_color": "track_opa", "needle_color": "needle_opa",
            "tick_color": "tick_opa", "major_tick_color": "major_tick_opa",
            "value_color": "value_opa", "border_color": "border_opa",
        }.get(color_key, "color_opa")

    def _pick_property_color(self, color_key: str) -> None:
        selected = colorchooser.askcolor(self.property_vars[color_key].get(), parent=self)[1]
        if not selected:
            return
        self.property_vars[color_key].set(selected.upper())
        opacity_key = self._opacity_key_for_color(color_key)
        if opacity_key in self.property_vars:
            self.property_vars[opacity_key].set("255")
        self._apply_properties()

    def _make_property_transparent(self, color_key: str) -> None:
        opacity_key = self._opacity_key_for_color(color_key)
        if opacity_key in self.property_vars:
            self.property_vars[opacity_key].set("0")
            self._apply_properties()

    @staticmethod
    def _valid_color(value: str) -> bool:
        return len(value) == 7 and value.startswith("#") and all(character in "0123456789abcdefABCDEF" for character in value[1:])

    @property
    def scale(self) -> float:
        try:
            return int(self.zoom.get().rstrip("%")) / 100
        except ValueError:
            return DEFAULT_CANVAS_SCALE

    def _step_zoom(self, amount: int, anchor: tuple[int, int] | None = None) -> None:
        anchor_x, anchor_y = anchor or (self.canvas.winfo_width() // 2, self.canvas.winfo_height() // 2)
        design_x = (self.canvas.canvasx(anchor_x) - CANVAS_MARGIN) / self.scale
        design_y = (self.canvas.canvasy(anchor_y) - CANVAS_MARGIN) / self.scale
        percent = max(25, min(200, round(self.scale * 100) + amount))
        self.zoom.set(f"{percent}%")
        self._redraw()
        region_width = CANVAS_WIDTH * self.scale + CANVAS_MARGIN * 2
        region_height = CANVAS_HEIGHT * self.scale + CANVAS_MARGIN * 2
        self.canvas.xview_moveto(max(0.0, (CANVAS_MARGIN + design_x * self.scale - anchor_x) / region_width))
        self.canvas.yview_moveto(max(0.0, (CANVAS_MARGIN + design_y * self.scale - anchor_y) / region_height))

    def _fit_zoom(self) -> None:
        width = max(1, self.canvas.winfo_width() - 20)
        height = max(1, self.canvas.winfo_height() - 20)
        percent = max(25, min(200, int(min(width / CANVAS_WIDTH, height / CANVAS_HEIGHT) * 100 / 5) * 5))
        self.zoom.set(f"{percent}%")
        self._redraw()
        self._center_canvas()

    def _canvas_zoom_wheel(self, event: tk.Event) -> str:
        self._step_zoom(25 if event.delta > 0 else -25, (event.x, event.y))
        return "break"

    def _canvas_scroll_wheel(self, event: tk.Event) -> str:
        self.canvas.yview_scroll(-int(event.delta / 120) * 3, "units")
        return "break"

    def _canvas_scroll_horizontal(self, event: tk.Event) -> str:
        self.canvas.xview_scroll(-int(event.delta / 120) * 3, "units")
        return "break"

    def _canvas_pan_start(self, event: tk.Event) -> str:
        self.canvas.scan_mark(event.x, event.y)
        self.canvas.configure(cursor="fleur")
        return "break"

    def _canvas_pan_move(self, event: tk.Event) -> str:
        self.canvas.scan_dragto(event.x, event.y, gain=1)
        return "break"

    def _center_canvas(self) -> None:
        self.update_idletasks()
        region_width = CANVAS_WIDTH * self.scale + CANVAS_MARGIN * 2
        region_height = CANVAS_HEIGHT * self.scale + CANVAS_MARGIN * 2
        left = CANVAS_MARGIN + CANVAS_WIDTH * self.scale / 2 - self.canvas.winfo_width() / 2
        top = CANVAS_MARGIN + CANVAS_HEIGHT * self.scale / 2 - self.canvas.winfo_height() / 2
        self.canvas.xview_moveto(max(0.0, left / region_width))
        self.canvas.yview_moveto(max(0.0, top / region_height))
        self._canvas_initialized = True
        return "break"

    def _image_preview(self, item: dict, width: int, height: int) -> tk.PhotoImage:
        encoded = str(item.get("asset_data", ""))
        source_width = int(item.get("source_width", item.get("width", 0)))
        source_height = int(item.get("source_height", item.get("height", 0)))
        image_format = str(item.get("format", "rgb565a8"))
        key = (encoded, source_width, source_height, image_format, width, height)
        cached = self._image_preview_cache.get(key)
        if cached is not None:
            self._image_preview_cache.move_to_end(key)
            return cached
        data = b64decode(encoded, validate=True)
        image = tk.PhotoImage(data=raw_image_png(data, source_width, source_height,
                                                 image_format, width, height))
        self._image_preview_cache[key] = image
        while len(self._image_preview_cache) > IMAGE_PREVIEW_CACHE_MAX:
            self._image_preview_cache.popitem(last=False)
        return image

    def _redraw(self) -> None:
        self.canvas.delete("all")
        self._canvas_images.clear()
        background = self.project.background if self._valid_color(self.project.background) else "#000000"
        self.canvas.configure(background="#08090B")
        canvas_width = round(CANVAS_WIDTH * self.scale)
        canvas_height = round(CANVAS_HEIGHT * self.scale)
        origin = CANVAS_MARGIN
        self.canvas.configure(scrollregion=(0, 0, canvas_width + origin * 2, canvas_height + origin * 2))
        self.canvas.create_rectangle(origin - 2, origin - 2, origin + canvas_width + 2, origin + canvas_height + 2,
                                     fill=background, outline="#50545E", width=2, tags=("canvas_boundary",))
        if self.snap_enabled.get():
            step = max(1, round(self.grid_size.get() * self.scale))
            for x in range(step, round(CANVAS_WIDTH * self.scale), step):
                self.canvas.create_line(origin + x, origin, origin + x, origin + canvas_height, fill="#17191d", tags=("grid",))
            for y in range(step, round(CANVAS_HEIGHT * self.scale), step):
                self.canvas.create_line(origin, origin + y, origin + canvas_width, origin + y, fill="#17191d", tags=("grid",))
        self.canvas.create_line(origin + CANVAS_WIDTH * self.scale / 2, origin,
                                origin + CANVAS_WIDTH * self.scale / 2, origin + canvas_height,
                                fill="#343840", dash=(3, 5), tags=("guide",))
        self.canvas.create_line(origin, origin + CANVAS_HEIGHT * self.scale / 2, origin + canvas_width,
                                origin + CANVAS_HEIGHT * self.scale / 2, fill="#343840", dash=(3, 5), tags=("guide",))
        for index, item in enumerate(self.project.objects):
            x = origin + int(item.get("x", 0) * self.scale)
            y = origin + int(item.get("y", 0) * self.scale)
            width = max(2, int(item.get("width", 1) * self.scale))
            height = max(2, int(item.get("height", 1) * self.scale))
            tags = ("object", str(index))
            color = str(item.get("color", RED))
            object_type = item.get("type")
            if object_type == "label":
                text = display_text(item, self.scenario.get())
                font_size = max(7, int(item.get("font_size", 14) * self.scale))
                text_color = blend_color(color, background, int(item.get("color_opa", 255)))
                self.canvas.create_text(x + width / 2, y + height / 2, text=text, fill=text_color,
                                        font=(PREVIEW_FONT, font_size), width=width, tags=tags)
            elif object_type == "bar":
                track = str(item.get("track_color", "#25282D"))
                radius = max(0, round(float(item.get("radius", 0)) * self.scale))
                angle = float(item.get("transform_angle", 0))
                background = self.project.background if self._valid_color(self.project.background) else "#000000"
                track = blend_color(track, background, int(item.get("track_opa", 255)))
                fill = blend_color(color, background, int(item.get("indicator_opa", 255)))
                self.canvas.create_polygon(rounded_rotated_polygon(x, y, width, height, radius, angle),
                                           fill=track, outline="", smooth=True, tags=tags)
                minimum = float(item.get("min", 0))
                maximum = float(item.get("max", 100))
                preview_value = float(item.get("preview_value", minimum))
                fraction = max(0.0, min(1.0, (preview_value - minimum) / max(1.0, maximum - minimum)))
                fill_width = width * fraction
                self.canvas.create_polygon(rounded_rotated_polygon(x, y, fill_width, height,
                                                                   min(radius, fill_width / 2), angle,
                                                                   (x + width / 2, y + height / 2)),
                                           fill=fill, outline="", smooth=True, tags=tags)
                if item.get("indicator_asset"):
                    self.canvas.create_text(x + width * 0.34, y + height / 2, text="IMAGE FILL", fill=WHITE,
                                            font=("Segoe UI Variable Text Semibold", max(7, min(10, height // 2))), tags=tags)
            elif object_type == "path_gauge":
                self._draw_path_gauge(item, x, y, tags, background)
            elif object_type == "arc":
                track = blend_color(str(item.get("track_color", "#25282D")), background,
                                    int(item.get("track_opa", 255)))
                arc_color = blend_color(color, background, int(item.get("color_opa", 255)))
                line_width = max(3, round(min(width, height) * 0.09))
                start = 90 - float(item.get("rotation", 135))
                sweep = -float(item.get("sweep", 270))
                minimum = float(item.get("min", 0))
                maximum = float(item.get("max", 100))
                preview_value = float(item.get("preview_value", minimum))
                fraction = max(0.0, min(1.0, (preview_value - minimum) / max(1.0, maximum - minimum)))
                self.canvas.create_arc(x, y, x + width, y + height, start=start, extent=sweep, style="arc",
                                       outline=track, width=line_width, tags=tags)
                self.canvas.create_arc(x, y, x + width, y + height, start=start, extent=sweep * fraction, style="arc",
                                       outline=arc_color, width=line_width, tags=tags)
            elif object_type in {"needle", "analog_tach", "analog_speedo"}:
                self._draw_meter(item, x, y, width, height, tags, object_type != "needle")
            elif object_type == "image":
                try:
                    render_width, render_height = width, height
                    if (self.selected_index == index and self.drag_origin is not None
                            and self.drag_origin[0] == "resize"):
                        render_width = max(2, int(self.drag_origin[5] * self.scale))
                        render_height = max(2, int(self.drag_origin[6] * self.scale))
                    image = self._image_preview(item, render_width, render_height)
                except (tk.TclError, ValueError, TypeError):
                    self.canvas.create_rectangle(x, y, x + width, y + height, fill="#20242a",
                                                 outline="#5d6570", tags=tags)
                    self.canvas.create_text(x + width / 2, y + height / 2,
                                            text=f"IMAGE\n{item.get('name', '')}", fill=LABEL,
                                            justify="center", tags=tags)
                else:
                    self._canvas_images.append(image)
                    self.canvas.create_image(x, y, image=image, anchor="nw", tags=tags)
            else:
                fill = str(item.get("background", color if object_type == "indicator" else PANEL))
                opacity_key = "background_opa" if object_type in {"button", "object"} else "color_opa"
                fill = blend_color(fill, background, int(item.get(opacity_key, 255)))
                self.canvas.create_rectangle(x, y, x + width, y + height, fill=fill, outline=LINE, tags=tags)
                text = str(item.get("text", item.get("name", object_type)))
                if object_type == "button":
                    text_color = blend_color(color, fill, int(item.get("color_opa", 255)))
                    self.canvas.create_text(x + width / 2, y + height / 2, text=text, fill=text_color,
                                            font=(PREVIEW_FONT, 10), tags=tags)
            if index == self.selected_index:
                self.canvas.create_rectangle(x - 2, y - 2, x + width + 2, y + height + 2,
                                             outline="#FFFFFF", width=2, dash=(4, 2), tags=("selection",))
                self.canvas.create_rectangle(x + width - 5, y + height - 5, x + width + 5, y + height + 5,
                                             fill=WHITE, outline=RED, tags=("resize",))
        self.layers.delete(0, "end")
        for item in reversed(self.project.objects):
            self.layers.insert("end", f"{item.get('name', 'object')}   [{item.get('type', '?')}]")
        if self.selected_index is not None:
            self.layers.selection_set(len(self.project.objects) - 1 - self.selected_index)
            self.layers.see(len(self.project.objects) - 1 - self.selected_index)
        self._load_properties()
        if self.path_editing:
            self.status.set("Click each printed RPM mark in order and enter its value, then choose Finish.")
        else:
            self.status.set(f"{len(self.project.objects)} / 96 objects   |   Canvas 1024 x 600 at {self.zoom.get()}   |   LVGL 8.4 runtime")

    def _draw_path_gauge(self, item: dict, x: float, y: float, tags: tuple[str, str], background: str) -> None:
        points = [[x + float(point[0]) * self.scale, y + float(point[1]) * self.scale]
                  for point in item.get("points", [])]
        count = int(item.get("segment_count", 36))
        minimum, maximum = float(item.get("min", 0)), float(item.get("max", 8000))
        value = float(item.get("preview_value", minimum))
        fraction = max(0.0, min(1.0, (value - minimum) / max(1.0, maximum - minimum)))
        lit_count = round(fraction * count)
        normal = blend_color(str(item.get("color", "#FFFFFF")), background,
                             int(item.get("indicator_opa", 255)))
        alert = blend_color(str(item.get("alert_color", "#E4002B")), background,
                            int(item.get("indicator_opa", 255)))
        track = blend_color(str(item.get("track_color", "#25282D")), background,
                            int(item.get("track_opa", 80)))
        segment_width = float(item.get("segment_width", 18)) * self.scale
        segment_height = float(item.get("segment_height", 10)) * self.scale
        alert_start = float(item.get("alert_start", 6500))
        point_values = item.get("point_values")
        for index, (center_x, center_y, angle) in enumerate(
            sample_path(points, count, point_values, minimum, maximum)):
            segment_value = minimum + (maximum - minimum) * (index + 1) / count
            fill = (alert if segment_value >= alert_start else normal) if index < lit_count else track
            polygon = rounded_rotated_polygon(center_x - segment_width / 2, center_y - segment_height / 2,
                                              segment_width, segment_height, segment_height / 3, angle)
            self.canvas.create_polygon(polygon, fill=fill, outline="", tags=tags)
        if self.path_editing and self.selected_index is not None and self.project.objects[self.selected_index] is item:
            if len(points) >= 2:
                self.canvas.create_line(*(coordinate for point in points for coordinate in point),
                                        fill="#00D8FF", width=2, dash=(4, 3), tags=tags)
            for point_index, (point_x, point_y) in enumerate(points):
                self.canvas.create_oval(point_x - 4, point_y - 4, point_x + 4, point_y + 4,
                                        fill="#00D8FF", outline=WHITE, tags=tags)
                if point_values and point_index < len(point_values):
                    self.canvas.create_text(point_x, point_y - 12, text=str(point_values[point_index]),
                                            fill="#00D8FF", font=("Segoe UI", 8, "bold"), tags=tags)

    def _draw_meter(self, item: dict, x: float, y: float, width: float, height: float,
                    tags: tuple[str, str], analog: bool) -> None:
        center_x, center_y = x + width / 2, y + height / 2
        radius = max(2, min(width, height) / 2 - 4)
        rotation = float(item.get("rotation", 135))
        sweep = float(item.get("sweep", 270))
        minimum = float(item.get("min", 0))
        maximum = float(item.get("max", 9000))
        binding = resolve_binding(str(item.get("name", "")))
        value = float(SIMULATED_VALUES[self.scenario.get()].get(binding, minimum))
        fraction = max(0.0, min(1.0, (value - minimum) / max(1.0, maximum - minimum)))
        if analog:
            canvas_background = self.project.background if self._valid_color(self.project.background) else "#000000"
            face = blend_color(str(item.get("background", "#000000")), canvas_background,
                               int(item.get("background_opa", 0)))
            border_width = max(0, round(float(item.get("border_width", 0)) * self.scale))
            border = blend_color(str(item.get("border_color", "#30343D")), face,
                                 int(item.get("border_opa", 255)))
            self.canvas.create_oval(x, y, x + width, y + height, fill=face,
                                    outline=border if border_width else "",
                                    width=border_width, tags=tags)
            arc_width = max(1, round(float(item.get("arc_width", 8)) * self.scale))
            track = blend_color(str(item.get("track_color", "#30343D")), face,
                                int(item.get("track_opa", 255)))
            self.canvas.create_arc(x, y, x + width, y + height, start=90 - rotation, extent=-sweep,
                                   style="arc", outline=track,
                                   width=arc_width, tags=tags)
            tick_count = max(2, int(item.get("tick_count", 19)))
            major_every = max(1, int(item.get("major_tick_every", 2)))
            for index in range(tick_count):
                tick_fraction = index / (tick_count - 1)
                angle = math.radians(rotation + sweep * tick_fraction)
                major = index % major_every == 0
                length = float(item.get("major_tick_length" if major else "tick_length", 20 if major else 12)) * self.scale
                line_width = max(1, round(float(item.get("major_tick_width" if major else "tick_width", 4 if major else 2)) * self.scale))
                outer = radius - arc_width / 2
                inner = outer - length
                color_key = "major_tick_color" if major else "tick_color"
                opacity_key = "major_tick_opa" if major else "tick_opa"
                color = blend_color(str(item.get(color_key, "#FFFFFF" if major else "#A1A6B0")), face,
                                    int(item.get(opacity_key, 255)))
                self.canvas.create_line(center_x + math.sin(angle) * inner, center_y - math.cos(angle) * inner,
                                        center_x + math.sin(angle) * outer, center_y - math.cos(angle) * outer,
                                        fill=color, width=line_width, tags=tags)
                if major:
                    label_radius = max(1, inner - float(item.get("label_gap", 10)) * self.scale)
                    label_value = minimum + (maximum - minimum) * tick_fraction
                    label_size = max(7, round(float(item.get("tick_label_font_size", 14)) * self.scale))
                    self.canvas.create_text(center_x + math.sin(angle) * label_radius,
                                            center_y - math.cos(angle) * label_radius,
                                            text=f"{label_value:g}", fill=color,
                                            font=(PREVIEW_FONT, label_size), tags=tags)
        needle_angle = math.radians(rotation + sweep * fraction)
        needle_length = max(3, radius + float(item.get("needle_offset", -12)) * self.scale)
        needle_width = max(1, round(float(item.get("needle_width", 5)) * self.scale))
        needle_color = blend_color(str(item.get("needle_color", "#E4002B")),
                       self.project.background, int(item.get("needle_opa", 255)))
        self.canvas.create_line(center_x, center_y, center_x + math.sin(needle_angle) * needle_length,
                                center_y - math.cos(needle_angle) * needle_length,
                                fill=needle_color, width=needle_width, tags=tags)
        self.canvas.create_oval(center_x - needle_width, center_y - needle_width,
                                center_x + needle_width, center_y + needle_width,
                                fill=needle_color, outline="", tags=tags)
        if analog and bool(item.get("show_value", True)):
            value_y = center_y + float(item.get("value_y", 45)) * self.scale
            font_size = max(7, round(float(item.get("value_font_size", 28)) * self.scale))
            value_color = blend_color(str(item.get("value_color", "#FFFFFF")), face,
                                      int(item.get("value_opa", 255)))
            self.canvas.create_text(center_x, value_y, text=f"{value:.0f}", fill=value_color,
                                    font=(PREVIEW_FONT, font_size), tags=tags)

    def _new(self) -> None:
        if self.project.objects and not messagebox.askyesno("New theme", "Discard the current canvas and start a new theme?", parent=self):
            return
        self.project = ThemeStudioProject()
        self.project_path = None
        self.last_export = None
        self.undo_stack.clear()
        self.redo_stack.clear()
        self._restore(self.project.document())

    def _open(self) -> None:
        selected = filedialog.askopenfilename(parent=self, title="Open Theme Studio project",
                                              filetypes=[("Theme Studio projects", "*.mdstudio.json"), ("JSON files", "*.json")])
        if not selected:
            return
        try:
            self.project = ThemeStudioProject.load(Path(selected))
        except (OSError, json.JSONDecodeError, StudioProjectError) as error:
            messagebox.showerror("Cannot open project", str(error), parent=self)
            return
        self.project_path = Path(selected)
        self.last_export = None
        self.undo_stack.clear()
        self.redo_stack.clear()
        self._restore(self.project.document())

    def _save(self) -> None:
        self._apply_identity()
        if self.project_path is None:
            selected = filedialog.asksaveasfilename(parent=self, title="Save Theme Studio project",
                                                    defaultextension=".mdstudio.json",
                                                    filetypes=[("Theme Studio projects", "*.mdstudio.json")])
            if not selected:
                return
            self.project_path = Path(selected)
        try:
            self.project.save(self.project_path)
        except (OSError, StudioProjectError) as error:
            messagebox.showerror("Cannot save project", str(error), parent=self)
            return
        self.status.set(f"Saved {self.project_path.name}")

    def _export(self) -> None:
        self._apply_identity()
        suggested = f"{self.project.theme_id.rsplit('.', 1)[-1]}.mdtheme.zip"
        selected = filedialog.asksaveasfilename(parent=self, title="Export MackoDash theme",
                                                initialfile=suggested, defaultextension=".mdtheme.zip",
                                                filetypes=[("MackoDash themes", "*.mdtheme.zip")])
        if not selected:
            return
        try:
            self.last_export = self.project.export(Path(selected))
        except (OSError, StudioProjectError) as error:
            messagebox.showerror("Cannot export theme", str(error), parent=self)
            return
        self.status.set(f"Exported dashboard-ready theme: {self.last_export.name}")
        messagebox.showinfo("Theme exported", f"Created:\n{self.last_export}", parent=self)

    def _send(self) -> None:
        open_theme_upload(self, self.last_export if self.last_export and self.last_export.exists() else None)