from __future__ import annotations

import json
import re
import struct
import tkinter as tk
import zlib
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from tkinter import ttk


TARGET_WIDTH = 1024
TARGET_HEIGHT = 600
SUPPORTED_TYPES = {"label", "bar", "arc", "button", "indicator", "object", "image"}

BINDING_NAMES = {
    "rpm": ("dash_rpm_value", "rpm_value", "engine_rpm_value", "dash_rpm_bar", "rpm_bar", "dash_rpm_arc", "rpm_arc"),
    "speed": ("dash_speed_value", "speed_value", "mph_value", "kph_value", "dash_speed_bar", "speed_bar", "dash_speed_arc", "speed_arc"),
    "gear": ("dash_gear_value", "gear_value"),
    "ect": ("dash_ect_value", "coolant_value", "water_temp_value", "dash_ect_bar", "ect_bar", "coolant_bar", "dash_ect_arc", "ect_arc", "coolant_arc"),
    "iat": ("dash_iat_value", "intake_temp_value", "air_temp_value", "dash_iat_bar", "iat_bar", "air_temp_bar", "dash_iat_arc", "iat_arc", "air_temp_arc"),
    "afr": ("dash_afr_value", "afr_value", "lambda_value"),
    "timing": ("dash_timing_value", "timing_value", "ignition_value"),
    "map": ("dash_map_value", "map_value", "boost_value"),
    "batt": ("dash_batt_value", "battery_value", "voltage_value"),
    "tps": ("dash_tps_value", "tps_value", "throttle_value", "dash_tps_bar", "tps_bar", "dash_tps_arc", "tps_arc"),
    "oil": ("dash_oil_value", "oil_pressure_value", "dash_oil_bar", "oil_bar", "dash_oil_arc", "oil_arc"),
    "duty": ("dash_duty_value", "injector_duty_value"),
    "knock": ("dash_knock_value", "knock_value"),
    "fuel": ("dash_fuel_value", "fuel_level_value", "dash_fuel_bar", "fuel_bar", "dash_fuel_arc", "fuel_arc"),
    "odo": ("dash_odo_value", "odometer_value"),
    "cel_indicator": ("dash_cel_indicator", "cel_indicator"),
    "vtec_indicator": ("dash_vtec_indicator", "vtec_indicator"),
    "shift_indicator": ("dash_shift_indicator", "shift_indicator"),
    "oil_indicator": ("dash_oil_indicator", "oil_indicator"),
    "knock_indicator": ("dash_knock_indicator", "knock_indicator"),
    "speed_unit": ("dash_speed_unit", "speed_unit", "mph_label", "kph_label"),
    "ect_unit": ("dash_ect_unit", "ect_unit", "coolant_unit"),
    "iat_unit": ("dash_iat_unit", "iat_unit"),
    "map_unit": ("dash_map_unit", "map_unit", "boost_unit"),
    "fuel_unit": ("dash_fuel_unit", "fuel_unit"),
    "tps_unit": ("dash_tps_unit", "tps_unit"),
    "oil_unit": ("dash_oil_unit", "oil_unit"),
    "settings": ("dash_settings_button", "settings_button"),
    "record": ("dash_record_button", "record_button", "logging_button"),
}

SIMULATED_VALUES = {
    "typical": {
        "rpm": "6840", "speed": "128", "gear": "4", "ect": "194", "iat": "86",
        "afr": "11.8", "timing": "18", "map": "24.6", "batt": "13.9", "tps": "100",
        "oil": "62", "duty": "78", "knock": "0.8", "fuel": "64", "odo": "12874.6",
        "speed_unit": "MPH", "ect_unit": "F", "iat_unit": "F", "map_unit": "PSI",
        "fuel_unit": "%", "tps_unit": "%", "oil_unit": "PSI",
    },
    "longest": {
        "rpm": "12000", "speed": "322", "gear": "N", "ect": "300", "iat": "300",
        "afr": "25.0", "timing": "-99", "map": "-101.3", "batt": "20.0", "tps": "100",
        "oil": "999", "duty": "100", "knock": "-12.3", "fuel": "100", "odo": "999999.9",
        "speed_unit": "KPH", "ect_unit": "C", "iat_unit": "C", "map_unit": "kPa",
        "fuel_unit": "%", "tps_unit": "%", "oil_unit": "kPa",
    },
}


@dataclass(frozen=True)
class PreviewIssue:
    severity: str
    message: str
    object_index: int | None = None


@dataclass
class ThemePackage:
    path: Path
    name: str
    design_width: int
    design_height: int
    layout: dict
    assets: dict[str, bytes]


def _safe_entry(name: str) -> bool:
    path = PurePosixPath(name)
    return bool(name) and not path.is_absolute() and ".." not in path.parts and "\\" not in name


def load_theme_package(path: Path) -> ThemePackage:
    with zipfile.ZipFile(path) as package:
        manifest = json.loads(package.read("manifest.json"))
        layout_name = manifest.get("layout", "layout.json")
        if not isinstance(layout_name, str) or not _safe_entry(layout_name):
            raise ValueError("Theme manifest contains an unsafe layout path")
        layout = json.loads(package.read(layout_name))
        resolution = manifest.get("resolution", [])
        if not (isinstance(resolution, list) and len(resolution) == 2):
            raise ValueError("Theme manifest has no valid resolution")
        width, height = resolution
        if not (isinstance(width, int) and isinstance(height, int) and width > 0 and height > 0):
            raise ValueError("Theme resolution must contain positive whole numbers")
        assets = {}
        for item in layout.get("objects", []):
            for key in ("asset", "indicator_asset"):
                asset = item.get(key) if isinstance(item, dict) else None
                if isinstance(asset, str) and _safe_entry(asset) and asset not in assets:
                    assets[asset] = package.read(asset)
    return ThemePackage(path, str(manifest.get("name", path.stem)), width, height, layout, assets)


def normalize_name(name: str) -> str:
    normalized = re.sub(r"_+", "_", re.sub(r"[^a-z0-9]+", "_", name.lower())).strip("_")
    if normalized.startswith("ui_"):
        normalized = normalized[3:]
    return re.sub(r"_[2-9]$", "", normalized)


def resolve_binding(name: str) -> str | None:
    normalized = normalize_name(name)
    for binding, names in BINDING_NAMES.items():
        if normalized in names:
            return binding
    matches = {
        binding for binding, names in BINDING_NAMES.items()
        if any(_one_edit_apart(normalized, candidate) for candidate in names)
    }
    return matches.pop() if len(matches) == 1 else None


def _one_edit_apart(left: str, right: str) -> bool:
    if abs(len(left) - len(right)) > 1:
        return False
    if len(left) == len(right):
        mismatches = [index for index, pair in enumerate(zip(left, right)) if pair[0] != pair[1]]
        return len(mismatches) == 1 or (
            len(mismatches) == 2 and mismatches[1] == mismatches[0] + 1
            and left[mismatches[0]] == right[mismatches[1]]
            and left[mismatches[1]] == right[mismatches[0]]
        )
    shorter, longer = (left, right) if len(left) < len(right) else (right, left)
    short_position = long_position = 0
    skipped = False
    while short_position < len(shorter) and long_position < len(longer):
        if shorter[short_position] == longer[long_position]:
            short_position += 1
            long_position += 1
        elif skipped:
            return False
        else:
            skipped = True
            long_position += 1
    return True


def object_rect(item: dict, design_width: int, design_height: int) -> tuple[float, float, float, float]:
    width = item.get("width", 120)
    height = item.get("height", 40)
    width = 120 if width == "content" or not isinstance(width, (int, float)) else width
    height = 40 if height == "content" or not isinstance(height, (int, float)) else height
    x = item.get("x", 0) if isinstance(item.get("x", 0), (int, float)) else 0
    y = item.get("y", 0) if isinstance(item.get("y", 0), (int, float)) else 0
    align = item.get("object_align", "top_left")
    anchors = {
        "top_left": (0, 0), "top_mid": ((design_width - width) / 2, 0),
        "top_right": (design_width - width, 0), "left_mid": (0, (design_height - height) / 2),
        "center": ((design_width - width) / 2, (design_height - height) / 2),
        "right_mid": (design_width - width, (design_height - height) / 2),
        "bottom_left": (0, design_height - height),
        "bottom_mid": ((design_width - width) / 2, design_height - height),
        "bottom_right": (design_width - width, design_height - height),
    }
    anchor_x, anchor_y = anchors.get(align, (0, 0))
    return anchor_x + x, anchor_y + y, float(width), float(height)


def image_runtime_rect(item: dict, design_width: int, design_height: int,
                       preview_width: int = TARGET_WIDTH) -> tuple[float, float, float, float]:
    source_width = item.get("source_width")
    source_height = item.get("source_height")
    if not isinstance(source_width, int) or not isinstance(source_height, int):
        return object_rect(item, design_width, design_height)

    runtime_scale = min(TARGET_WIDTH / design_width, TARGET_HEIGHT / design_height)
    preview_scale = preview_width / TARGET_WIDTH
    width = item.get("width")
    height = item.get("height")
    zoom = item.get("zoom")
    if isinstance(zoom, (int, float)):
        runtime_zoom = max(1, min(768, round(zoom * runtime_scale)))
    elif isinstance(width, (int, float)) and isinstance(height, (int, float)):
        scaled_width = max(1, round(width * runtime_scale))
        scaled_height = max(1, round(height * runtime_scale))
        runtime_zoom = max(1, min(768, min(scaled_width * 256 // source_width,
                                           scaled_height * 256 // source_height)))
    else:
        runtime_zoom = max(1, min(768, round(256 * runtime_scale)))
    rendered_width = source_width * runtime_zoom / 256
    rendered_height = source_height * runtime_zoom / 256

    x = item.get("x", 0) if isinstance(item.get("x", 0), (int, float)) else 0
    y = item.get("y", 0) if isinstance(item.get("y", 0), (int, float)) else 0
    scaled_canvas_width = round(design_width * runtime_scale)
    scaled_canvas_height = round(design_height * runtime_scale)
    offset_x = (TARGET_WIDTH - scaled_canvas_width) / 2
    offset_y = (TARGET_HEIGHT - scaled_canvas_height) / 2
    align = item.get("object_align")
    anchors = {
        "top_left": (offset_x, offset_y),
        "top_mid": (TARGET_WIDTH / 2, offset_y),
        "top_right": (TARGET_WIDTH - offset_x, offset_y),
        "left_mid": (offset_x, TARGET_HEIGHT / 2),
        "center": (TARGET_WIDTH / 2, TARGET_HEIGHT / 2),
        "right_mid": (TARGET_WIDTH - offset_x, TARGET_HEIGHT / 2),
        "bottom_left": (offset_x, TARGET_HEIGHT - offset_y),
        "bottom_mid": (TARGET_WIDTH / 2, TARGET_HEIGHT - offset_y),
        "bottom_right": (TARGET_WIDTH - offset_x, TARGET_HEIGHT - offset_y),
    }
    if align in anchors:
        anchor_x, anchor_y = anchors[align]
        horizontal = "left" if align.endswith("left") else "right" if align.endswith("right") else "center"
        vertical = "top" if align.startswith("top") else "bottom" if align.startswith("bottom") else "center"
        runtime_x = anchor_x + x * runtime_scale
        runtime_y = anchor_y + y * runtime_scale
        if horizontal == "center":
            runtime_x -= rendered_width / 2
        elif horizontal == "right":
            runtime_x -= rendered_width
        if vertical == "center":
            runtime_y -= rendered_height / 2
        elif vertical == "bottom":
            runtime_y -= rendered_height
    else:
        runtime_x = offset_x + x * runtime_scale
        runtime_y = offset_y + y * runtime_scale
    return (runtime_x * preview_scale, runtime_y * preview_scale,
            rendered_width * preview_scale, rendered_height * preview_scale)


def display_text(item: dict, scenario: str) -> str:
    binding = resolve_binding(str(item.get("name", "")))
    return SIMULATED_VALUES.get(scenario, SIMULATED_VALUES["typical"]).get(binding, str(item.get("text", "--")))


def analyze_theme(theme: ThemePackage) -> list[PreviewIssue]:
    issues = []
    objects = theme.layout.get("objects")
    if not isinstance(objects, list):
        return [PreviewIssue("error", "layout.json has no objects array")]
    bindings = set()
    for index, item in enumerate(objects):
        if not isinstance(item, dict):
            issues.append(PreviewIssue("error", f"Object {index + 1} is not a JSON object", index))
            continue
        object_type = item.get("type", "label")
        name = str(item.get("name", f"object {index + 1}"))
        if object_type not in SUPPORTED_TYPES:
            issues.append(PreviewIssue("error", f"{name}: unsupported control type '{object_type}'", index))
            continue
        binding = resolve_binding(name)
        if binding:
            bindings.add(binding)
        elif normalize_name(name).startswith("dash"):
            issues.append(PreviewIssue("warning", f"{name}: live binding name is not recognized", index))
        x, y, width, height = object_rect(item, theme.design_width, theme.design_height)
        if x < 0 or y < 0 or x + width > theme.design_width or y + height > theme.design_height:
            issues.append(PreviewIssue("error", f"{name}: extends outside the {theme.design_width} x {theme.design_height} canvas", index))
        if object_type == "label":
            text = display_text(item, "longest")
            font_size = item.get("font_size", 14)
            font_size = font_size if isinstance(font_size, (int, float)) else 14
            estimated_width = len(text) * font_size * 0.62
            estimated_height = font_size * 1.25
            if isinstance(item.get("width"), (int, float)) and isinstance(item.get("height"), (int, float)) and (
                estimated_width > width or estimated_height > height
            ):
                issues.append(PreviewIssue("warning", f"{name}: longest value '{text}' may be clipped", index))
    if "settings" not in bindings:
        issues.append(PreviewIssue("error", "Missing dash_settings_button; firmware will add a fallback control"))
    if "record" not in bindings:
        issues.append(PreviewIssue("error", "Missing dash_record_button; firmware will add a fallback REC control"))
    return issues


def raw_image_png(data: bytes, width: int, height: int, image_format: str,
                  output_width: int | None = None, output_height: int | None = None) -> bytes:
    pixel_size = 3 if image_format == "rgb565a8" else 2
    if width <= 0 or height <= 0 or len(data) != width * height * pixel_size:
        raise ValueError("Raw image size does not match its metadata")
    output_width = output_width or width
    output_height = output_height or height
    if output_width <= 0 or output_height <= 0:
        raise ValueError("Preview image dimensions must be positive")
    rows = []
    for output_y in range(output_height):
        row = bytearray([0])
        source_y = min(height - 1, output_y * height // output_height)
        for output_x in range(output_width):
            source_x = min(width - 1, output_x * width // output_width)
            position = (source_y * width + source_x) * pixel_size
            value = data[position] | (data[position + 1] << 8)
            red = ((value >> 11) & 0x1f) * 255 // 31
            green = ((value >> 5) & 0x3f) * 255 // 63
            blue = (value & 0x1f) * 255 // 31
            alpha = data[position + 2] if pixel_size == 3 else 255
            row.extend((red, green, blue, alpha))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)

    header = struct.pack(">IIBBBBB", output_width, output_height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


class ThemePreviewWindow(tk.Toplevel):
    PREVIEW_WIDTH = 768
    PREVIEW_HEIGHT = 450

    def __init__(self, parent: tk.Misc, package_path: Path) -> None:
        super().__init__(parent)
        self.theme = load_theme_package(package_path)
        self.issues = analyze_theme(self.theme)
        self.scenario = tk.StringVar(value="typical")
        self._images: list[tk.PhotoImage] = []
        self.title(f"Theme Preview - {self.theme.name}")
        self.geometry("1040x650")
        self.minsize(900, 610)
        self.configure(background="#08090a")
        self.transient(parent.winfo_toplevel())
        self._build_ui()
        self._render()

    def _build_ui(self) -> None:
        toolbar = ttk.Frame(self, padding=(20, 14))
        toolbar.pack(fill="x")
        ttk.Label(toolbar, text=self.theme.name, style="Title.TLabel").pack(side="left")
        ttk.Button(toolbar, text="Typical Values", command=lambda: self._set_scenario("typical")).pack(side="right")
        ttk.Button(toolbar, text="Longest Values", command=lambda: self._set_scenario("longest")).pack(side="right", padx=8)

        body = ttk.Frame(self, padding=(20, 0, 20, 20))
        body.pack(fill="both", expand=True)
        self.canvas = tk.Canvas(body, width=self.PREVIEW_WIDTH, height=self.PREVIEW_HEIGHT,
                                background="#000000", highlightbackground="#2a2c31", highlightthickness=1)
        self.canvas.grid(row=0, column=0, sticky="n")
        side = ttk.Frame(body, style="Tile.TFrame", padding=14)
        side.grid(row=0, column=1, sticky="nsew", padx=(14, 0))
        ttk.Label(side, text="PREVIEW CHECKS", style="TileTitle.TLabel").pack(anchor="w")
        summary = "Ready to test on hardware" if not self.issues else f"{len(self.issues)} item(s) need attention"
        ttk.Label(side, text=summary, style="Good.Tile.TLabel" if not self.issues else "Warn.Tile.TLabel").pack(anchor="w", pady=(5, 10))
        report = tk.Text(side, width=30, height=24, wrap="word", background="#151619", foreground="#f4f3ef",
                         highlightthickness=0, borderwidth=0, font=("Segoe UI", 9), state="normal")
        report.pack(fill="both", expand=True)
        if self.issues:
            report.insert("1.0", "\n\n".join(f"{issue.severity.upper()}\n{issue.message}" for issue in self.issues))
        else:
            report.insert("1.0", "No clipping, unsupported controls, missing required buttons, or longest-value fit issues found.")
        report.configure(state="disabled")
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

    def _set_scenario(self, scenario: str) -> None:
        self.scenario.set(scenario)
        self._render()

    def _runtime_rect(self, item: dict) -> tuple[float, float, float, float]:
        x, y, width, height = object_rect(item, self.theme.design_width, self.theme.design_height)
        runtime_scale = min(TARGET_WIDTH / self.theme.design_width, TARGET_HEIGHT / self.theme.design_height)
        offset_x = (TARGET_WIDTH - self.theme.design_width * runtime_scale) / 2
        offset_y = (TARGET_HEIGHT - self.theme.design_height * runtime_scale) / 2
        preview_scale = self.PREVIEW_WIDTH / TARGET_WIDTH
        return ((offset_x + x * runtime_scale) * preview_scale,
                (offset_y + y * runtime_scale) * preview_scale,
                width * runtime_scale * preview_scale, height * runtime_scale * preview_scale)

    def _render(self) -> None:
        self.canvas.delete("all")
        self._images.clear()
        background = str(self.theme.layout.get("background", "#08090a"))
        self.canvas.configure(background=background)
        problematic = {issue.object_index for issue in self.issues if issue.object_index is not None}
        for index, item in enumerate(self.theme.layout.get("objects", [])):
            if not isinstance(item, dict):
                continue
            object_type = item.get("type", "label")
            if object_type == "image":
                x, y, width, height = image_runtime_rect(
                    item, self.theme.design_width, self.theme.design_height, self.PREVIEW_WIDTH)
            else:
                x, y, width, height = self._runtime_rect(item)
            color = str(item.get("color", "#ffffff"))
            if object_type not in SUPPORTED_TYPES:
                self.canvas.create_rectangle(x, y, x + width, y + height, fill="#4a0413", outline="#ffb020", width=3)
                self.canvas.create_line(x, y, x + width, y + height, fill="#ffb020", width=3)
                self.canvas.create_line(x + width, y, x, y + height, fill="#ffb020", width=3)
            elif object_type == "image":
                self._draw_image(item, x, y, width, height)
            elif object_type == "label":
                runtime_scale = min(TARGET_WIDTH / self.theme.design_width, TARGET_HEIGHT / self.theme.design_height)
                preview_scale = self.PREVIEW_WIDTH / TARGET_WIDTH
                font_size = max(6, round(float(item.get("font_size", 14)) * runtime_scale * preview_scale))
                anchor = {"left": "w", "right": "e"}.get(item.get("align"), "center")
                text_x = x + (0 if anchor == "w" else width if anchor == "e" else width / 2)
                self.canvas.create_text(text_x, y + height / 2, text=display_text(item, self.scenario.get()), fill=color,
                                        width=width, anchor=anchor, font=("Segoe UI", font_size, "bold"))
            elif object_type == "bar":
                self.canvas.create_rectangle(x, y, x + width, y + height, fill=str(item.get("track_color", "#25282d")), outline="")
                binding = resolve_binding(str(item.get("name", "")))
                value = float(SIMULATED_VALUES[self.scenario.get()].get(binding, item.get("min", 0)))
                minimum, maximum = float(item.get("min", 0)), float(item.get("max", 100))
                fraction = max(0.0, min(1.0, (value - minimum) / max(maximum - minimum, 1)))
                self.canvas.create_rectangle(x, y, x + width * fraction, y + height, fill=color, outline="")
            elif object_type == "arc":
                self.canvas.create_arc(x, y, x + width, y + height, start=90 - float(item.get("rotation", 135)),
                                       extent=-float(item.get("sweep", 270)), style="arc", outline=str(item.get("track_color", "#25282d")), width=max(2, round(min(width, height) * 0.08)))
                self.canvas.create_arc(x, y, x + width, y + height, start=90 - float(item.get("rotation", 135)),
                                       extent=-float(item.get("sweep", 270)) * 0.72, style="arc", outline=color, width=max(2, round(min(width, height) * 0.08)))
            else:
                fill = str(item.get("background", color if object_type != "button" else "#151619"))
                self.canvas.create_rectangle(x, y, x + width, y + height, fill=fill, outline="")
                if object_type == "button":
                    self.canvas.create_text(x + width / 2, y + height / 2, text=item.get("text", "Settings"), fill="#ffffff", font=("Segoe UI", max(7, round(height * 0.22)), "bold"))
            if index in problematic:
                self.canvas.create_rectangle(x, y, x + width, y + height, outline="#ffb020", width=3)

    def _draw_image(self, item: dict, x: float, y: float, width: float, height: float) -> None:
        asset_name = item.get("asset")
        data = self.theme.assets.get(asset_name)
        source_width = item.get("source_width")
        source_height = item.get("source_height")
        image_format = item.get("format")
        if not data or not isinstance(source_width, int) or not isinstance(source_height, int) or image_format not in {"rgb565", "rgb565a8"}:
            return
        try:
            output_width = max(1, round(width))
            output_height = max(1, round(height))
            image = tk.PhotoImage(data=raw_image_png(data, source_width, source_height, image_format,
                                                     output_width, output_height))
        except (tk.TclError, ValueError):
            return
        self._images.append(image)
        self.canvas.create_image(x, y, image=image, anchor="nw")
