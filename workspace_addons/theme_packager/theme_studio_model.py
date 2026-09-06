from __future__ import annotations

import json
import re
import zipfile
from base64 import b64decode, b64encode
from copy import deepcopy
from dataclasses import dataclass, field
from pathlib import Path


PROJECT_SCHEMA = 1
CANVAS_WIDTH = 1024
CANVAS_HEIGHT = 600
MAX_OBJECTS = 96
SUPPORTED_TYPES = {"label", "bar", "arc", "path_gauge", "button", "indicator", "object", "image", "needle", "analog_tach", "analog_speedo"}
THEME_ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]{0,62}$")

DEFAULTS = {
    "label": {"width": 180, "height": 52, "text": "Label", "font_size": 28, "color": "#FFFFFF", "color_opa": 255, "align": "center"},
        "bar": {"width": 260, "height": 28, "min": 0, "max": 100, "preview_value": 68,
            "color": "#E4002B", "track_color": "#25282D", "track_opa": 255,
            "indicator_opa": 255, "radius": 0, "transform_angle": 0},
        "path_gauge": {"width": 700, "height": 180, "min": 0, "max": 8000, "preview_value": 5200,
            "points": [[0, 160], [90, 45], [180, 10], [560, 10], [700, 10]],
            "point_values": [0, 1000, 2000, 6000, 8000],
            "segment_count": 36, "segment_width": 18, "segment_height": 10,
            "color": "#FFFFFF", "alert_color": "#E4002B", "alert_start": 6500,
            "track_color": "#25282D", "track_opa": 80, "indicator_opa": 255},
            "arc": {"width": 180, "height": 180, "min": 0, "max": 100, "preview_value": 68,
                "color": "#E4002B", "color_opa": 255,
                "track_color": "#25282D", "track_opa": 255, "rotation": 135, "sweep": 270},
        "needle": {"width": 220, "height": 220, "min": 0, "max": 9000, "rotation": 135, "sweep": 270,
                   "needle_color": "#E4002B", "needle_opa": 255, "needle_width": 5, "needle_offset": -12},
        "analog_tach": {"width": 340, "height": 340, "min": 0, "max": 9000, "rotation": 135, "sweep": 270,
                        "background": "#000000", "background_opa": 0, "border_color": "#30343D", "border_opa": 255, "border_width": 0,
                        "track_color": "#30343D", "track_opa": 255, "needle_color": "#E4002B", "needle_opa": 255,
                        "tick_color": "#A1A6B0", "tick_opa": 255, "major_tick_color": "#FFFFFF", "major_tick_opa": 255,
                        "value_color": "#FFFFFF", "value_opa": 255, "tick_count": 19,
                        "major_tick_every": 2, "tick_width": 2, "tick_length": 12, "major_tick_width": 4,
                        "major_tick_length": 20, "label_gap": 10, "tick_label_font_size": 14,
                        "arc_width": 8, "needle_width": 5,
                        "needle_offset": -12, "show_value": True, "value_font_size": 28, "value_y": 45},
        "analog_speedo": {"width": 340, "height": 340, "min": 0, "max": 160, "rotation": 135, "sweep": 270,
                           "background": "#000000", "background_opa": 0, "border_color": "#30343D", "border_opa": 255, "border_width": 0,
                           "track_color": "#30343D", "track_opa": 255, "needle_color": "#E4002B", "needle_opa": 255,
                           "tick_color": "#A1A6B0", "tick_opa": 255, "major_tick_color": "#FFFFFF", "major_tick_opa": 255,
                           "value_color": "#FFFFFF", "value_opa": 255, "tick_count": 17, "major_tick_every": 2,
                           "tick_width": 2, "tick_length": 12, "major_tick_width": 4, "major_tick_length": 20,
                           "label_gap": 10, "tick_label_font_size": 14, "arc_width": 8, "needle_width": 5,
                           "needle_offset": -12, "show_value": True, "value_font_size": 28, "value_y": 45},
        "button": {"width": 120, "height": 52, "text": "Button", "background": "#151619", "background_opa": 255, "color": "#FFFFFF", "color_opa": 255},
        "indicator": {"width": 90, "height": 36, "color": "#E4002B", "color_opa": 255},
        "object": {"width": 180, "height": 100, "background": "#151619", "background_opa": 255, "color": "#FFFFFF", "color_opa": 255},
}


class StudioProjectError(ValueError):
    pass


@dataclass
class ThemeStudioProject:
    theme_id: str = "customer.new-theme"
    name: str = "New Theme"
    background: str = "#000000"
    background_opa: int = 255
    objects: list[dict] = field(default_factory=list)

    def add_object(self, object_type: str, x: int = 40, y: int = 40) -> dict:
        if object_type not in SUPPORTED_TYPES:
            raise StudioProjectError(f"Unsupported object type: {object_type}")
        if len(self.objects) >= MAX_OBJECTS:
            raise StudioProjectError(f"Themes support up to {MAX_OBJECTS} objects")
        count = sum(item.get("type") == object_type for item in self.objects) + 1
        default_name = {"needle": "dash_rpm_bar", "analog_tach": "dash_rpm_bar",
            "path_gauge": "dash_rpm_path_gauge",
                "analog_speedo": "dash_speed_bar"}.get(object_type, f"{object_type}_{count}")
        item = {"type": object_type, "name": self.unique_name(default_name), "x": x, "y": y}
        item.update(deepcopy(DEFAULTS[object_type]))
        self.objects.append(item)
        self.constrain(item)
        return item

    def add_image(self, name: str, width: int, height: int, data: bytes,
                  x: int = 40, y: int = 40) -> dict:
        if len(data) != width * height * 3:
            raise StudioProjectError("Imported image data does not match its dimensions")
        if sum(item.get("type") == "image" or bool(item.get("indicator_asset")) for item in self.objects) >= 12:
            raise StudioProjectError("Themes support up to 12 images")
        item = {
            "type": "image", "name": self.unique_name(name), "x": x, "y": y,
            "width": width, "height": height, "source_width": width,
            "source_height": height, "format": "rgb565a8",
            "asset": f"assets/{self.unique_name(name)}.rgb565a8",
            "asset_data": b64encode(data).decode("ascii"),
        }
        self.objects.append(item)
        self.constrain(item)
        return item

    def set_bar_image(self, item: dict, name: str, width: int, height: int, data: bytes) -> None:
        if item.get("type") != "bar":
            raise StudioProjectError("Indicator images can only be assigned to bars")
        if len(data) != width * height * 3:
            raise StudioProjectError("Imported image data does not match its dimensions")
        image_count = sum(candidate.get("type") == "image" or bool(candidate.get("indicator_asset"))
                          for candidate in self.objects)
        if not item.get("indicator_asset") and image_count >= 12:
            raise StudioProjectError("Themes support up to 12 images")
        asset_name = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_") or "bar_fill"
        existing_assets = {candidate.get("asset") or candidate.get("indicator_asset") for candidate in self.objects
                           if candidate is not item}
        base_name = asset_name
        suffix = 2
        while f"assets/{asset_name}.rgb565a8" in existing_assets:
            asset_name = f"{base_name}_{suffix}"
            suffix += 1
        item.update({
            "indicator_asset": f"assets/{asset_name}.rgb565a8",
            "indicator_format": "rgb565a8",
            "indicator_source_width": width,
            "indicator_source_height": height,
            "indicator_asset_data": b64encode(data).decode("ascii"),
        })

    def duplicate(self, index: int) -> dict:
        if len(self.objects) >= MAX_OBJECTS:
            raise StudioProjectError(f"Themes support up to {MAX_OBJECTS} objects")
        source = deepcopy(self.objects[index])
        source["name"] = self.unique_name(f"{source.get('name', 'object')}_copy")
        source["x"] = int(source.get("x", 0)) + 12
        source["y"] = int(source.get("y", 0)) + 12
        self.objects.append(source)
        self.constrain(source)
        return source

    def move_layer(self, index: int, direction: int) -> int:
        destination = min(max(0, index + direction), len(self.objects) - 1)
        if destination != index:
            self.objects[index], self.objects[destination] = self.objects[destination], self.objects[index]
        return destination

    @staticmethod
    def snap(value: int, grid: int) -> int:
        return round(value / grid) * grid if grid > 1 else value

    @classmethod
    def from_template(cls, template: str) -> ThemeStudioProject:
        project = cls()
        if template == "Performance":
            definitions = (
                ("bar", "dash_rpm_bar", 40, 34, 944, 34),
                ("label", "dash_rpm_value", 50, 90, 260, 100),
                ("label", "dash_gear_value", 412, 122, 200, 210),
                ("label", "dash_speed_value", 730, 106, 240, 120),
                ("label", "dash_map_value", 80, 400, 180, 70),
                ("label", "dash_ect_value", 330, 400, 180, 70),
                ("label", "dash_afr_value", 580, 400, 180, 70),
                ("button", "dash_record_button", 820, 520, 80, 54),
                ("button", "dash_settings_button", 912, 520, 92, 54),
            )
        elif template == "Street":
            definitions = (
                ("arc", "dash_rpm_arc", 72, 100, 360, 360),
                ("label", "dash_rpm_value", 142, 220, 220, 80),
                ("label", "dash_speed_value", 574, 126, 300, 150),
                ("label", "dash_gear_value", 650, 300, 150, 150),
                ("label", "dash_ect_value", 160, 500, 160, 54),
                ("label", "dash_fuel_value", 430, 500, 160, 54),
                ("button", "dash_record_button", 820, 520, 80, 54),
                ("button", "dash_settings_button", 912, 520, 92, 54),
            )
        else:
            return project
        for object_type, name, x, y, width, height in definitions:
            item = project.add_object(object_type, x, y)
            item.update({"name": name, "width": width, "height": height})
            if object_type == "label":
                item["font_size"] = 44 if height >= 80 else 28
            if object_type == "button":
                item["text"] = "REC" if "record" in name else "Settings"
        return project

    def unique_name(self, requested: str, ignored: dict | None = None) -> str:
        existing = {str(item.get("name", "")) for item in self.objects if item is not ignored}
        if requested not in existing:
            return requested
        suffix = 2
        while f"{requested}_{suffix}" in existing:
            suffix += 1
        return f"{requested}_{suffix}"

    def constrain(self, item: dict) -> None:
        width = max(1, int(item.get("width", 1)))
        height = max(1, int(item.get("height", 1)))
        item["width"] = min(width, CANVAS_WIDTH)
        item["height"] = min(height, CANVAS_HEIGHT)
        item["x"] = min(max(0, int(item.get("x", 0))), CANVAS_WIDTH - item["width"])
        item["y"] = min(max(0, int(item.get("y", 0))), CANVAS_HEIGHT - item["height"])

    def validate(self) -> None:
        if not THEME_ID_PATTERN.fullmatch(self.theme_id):
            raise StudioProjectError("Theme ID must use lowercase letters, numbers, '.', '_' or '-'")
        if not self.name.strip() or len(self.name) > 95:
            raise StudioProjectError("Theme name must be between 1 and 95 characters")
        if not 0 <= int(self.background_opa) <= 255:
            raise StudioProjectError("Theme background opacity must be between 0 and 255")
        if len(self.objects) > MAX_OBJECTS:
            raise StudioProjectError(f"Themes support up to {MAX_OBJECTS} objects")
        names: set[str] = set()
        image_count = 0
        for item in self.objects:
            if item.get("type") not in SUPPORTED_TYPES:
                raise StudioProjectError(f"Unsupported object type: {item.get('type')}")
            name = str(item.get("name", "")).strip()
            if not name or name in names:
                raise StudioProjectError("Every object must have a unique name")
            names.add(name)
            self.constrain(item)
            if item.get("type") == "image":
                image_count += 1
                try:
                    data = b64decode(str(item.get("asset_data", "")), validate=True)
                except ValueError as error:
                    raise StudioProjectError(f"Image {name} has invalid embedded data") from error
                if len(data) != int(item["source_width"]) * int(item["source_height"]) * 3:
                    raise StudioProjectError(f"Image {name} has invalid embedded data")
            if item.get("type") == "bar" and item.get("indicator_asset"):
                image_count += 1
                try:
                    data = b64decode(str(item.get("indicator_asset_data", "")), validate=True)
                except ValueError as error:
                    raise StudioProjectError(f"Bar {name} has invalid indicator image data") from error
                expected = int(item.get("indicator_source_width", 0)) * int(item.get("indicator_source_height", 0)) * 3
                if not expected or len(data) != expected:
                    raise StudioProjectError(f"Bar {name} has invalid indicator image data")
            if item.get("type") == "path_gauge":
                points = item.get("points")
                if not isinstance(points, list) or not 2 <= len(points) <= 24:
                    raise StudioProjectError(f"{name}: path must contain between 2 and 24 points")
                for point in points:
                    if (not isinstance(point, list) or len(point) != 2 or
                            not all(isinstance(value, (int, float)) for value in point)):
                        raise StudioProjectError(f"{name}: path points must be [x, y] pairs")
                    if not 0 <= point[0] <= item["width"] or not 0 <= point[1] <= item["height"]:
                        raise StudioProjectError(f"{name}: path points must stay inside the gauge bounds")
                point_values = item.get("point_values")
                if point_values is not None:
                    if (not isinstance(point_values, list) or len(point_values) != len(points) or
                            not all(isinstance(value, (int, float)) for value in point_values)):
                        raise StudioProjectError(f"{name}: every path point must have an RPM value")
                    if any(right <= left for left, right in zip(point_values, point_values[1:])):
                        raise StudioProjectError(f"{name}: path point values must increase")
            ranges = {
                "rotation": (0, 359), "sweep": (1, 360), "track_opa": (0, 255),
                "indicator_opa": (0, 255), "radius": (0, 1024),
                "transform_angle": (-360, 360), "zoom": (1, 768),
                "needle_width": (1, 40), "needle_offset": (-200, 200), "tick_count": (2, 101),
                "major_tick_every": (1, 101), "tick_width": (1, 20), "tick_length": (1, 80),
                "major_tick_width": (1, 30), "major_tick_length": (1, 100), "label_gap": (0, 80),
                "arc_width": (1, 80), "font_size": (8, 200), "value_font_size": (8, 200), "value_y": (-200, 200),
                "background_opa": (0, 255), "border_width": (0, 40), "tick_label_font_size": (8, 200),
                "color_opa": (0, 255), "needle_opa": (0, 255), "tick_opa": (0, 255),
                "major_tick_opa": (0, 255), "value_opa": (0, 255), "border_opa": (0, 255),
                "segment_count": (2, 96), "segment_width": (1, 100), "segment_height": (1, 100),
            }
            for key, (minimum, maximum) in ranges.items():
                if key in item and not minimum <= int(item[key]) <= maximum:
                    raise StudioProjectError(f"{name}: {key} must be between {minimum} and {maximum}")
            if "preview_value" in item:
                minimum = float(item.get("min", 0))
                maximum = float(item.get("max", 100))
                preview_value = float(item["preview_value"])
                if not minimum <= preview_value <= maximum:
                    raise StudioProjectError(f"{name}: preview value must be between {minimum:g} and {maximum:g}")
            if item.get("type") == "path_gauge":
                minimum = int(item.get("min", 0))
                maximum = int(item.get("max", 100))
                alert_start = int(item.get("alert_start", maximum))
                if not minimum <= alert_start <= maximum:
                    raise StudioProjectError(f"{name}: alert threshold must be between {minimum} and {maximum}")
                point_values = item.get("point_values")
                if point_values is not None and (point_values[0] != minimum or point_values[-1] != maximum):
                    raise StudioProjectError(f"{name}: first and last path points must match minimum and maximum")
        if image_count > 12:
            raise StudioProjectError("Themes support up to 12 images")

    def document(self) -> dict:
        self.validate()
        return self.snapshot()

    def snapshot(self) -> dict:
        return {
            "schema": PROJECT_SCHEMA,
            "theme_id": self.theme_id,
            "name": self.name,
            "resolution": [CANVAS_WIDTH, CANVAS_HEIGHT],
            "background": self.background,
            "background_opa": self.background_opa,
            "objects": deepcopy(self.objects),
        }

    def save(self, path: Path) -> Path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.document(), indent=2) + "\n", encoding="utf-8")
        return path

    @classmethod
    def load(cls, path: Path) -> ThemeStudioProject:
        document = json.loads(path.read_text(encoding="utf-8"))
        if document.get("schema") != PROJECT_SCHEMA or document.get("resolution") != [CANVAS_WIDTH, CANVAS_HEIGHT]:
            raise StudioProjectError("Unsupported Theme Studio project")
        project = cls(
            theme_id=str(document.get("theme_id", "")),
            name=str(document.get("name", "")),
            background=str(document.get("background", "#000000")),
            background_opa=int(document.get("background_opa", 255)),
            objects=deepcopy(document.get("objects", [])),
        )
        project.validate()
        return project

    def export(self, path: Path) -> Path:
        document = self.document()
        manifest = {
            "schema": 1,
            "id": self.theme_id,
            "name": self.name,
            "resolution": [CANVAS_WIDTH, CANVAS_HEIGHT],
            "lvgl": "8.4",
            "layout": "layout.json",
        }
        assets: dict[str, bytes] = {}
        objects = document["objects"]
        for item in objects:
            item.pop("preview_value", None)
            encoded = item.pop("asset_data", None)
            if item.get("type") == "image" and isinstance(encoded, str):
                assets[str(item["asset"])] = b64decode(encoded)
            indicator_encoded = item.pop("indicator_asset_data", None)
            if item.get("type") == "bar" and isinstance(indicator_encoded, str):
                assets[str(item["indicator_asset"])] = b64decode(indicator_encoded)
        layout = {"background": self.background, "background_opa": self.background_opa, "objects": objects}
        path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as package:
            package.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
            package.writestr("layout.json", json.dumps(layout, indent=2) + "\n")
            for asset, data in assets.items():
                package.writestr(asset, data)
        return path