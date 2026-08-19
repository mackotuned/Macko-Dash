import argparse
import json
import re
import zipfile
from pathlib import Path

IMAGE_SOURCES = {
    "assets/background.rgb565a8": "ft450/images/ui_img_bg_png.c",
    "assets/overlay.rgb565a8": "ft450/images/ui_img_overlay_png.c",
    "assets/gyro.rgb565": "ft450/images/ui_img_gyro_png.c",
}


def extract_image(source: str) -> bytes:
    match = re.search(r"uint8_t\s+\w+_data\[\]\s*=\s*\{(.*?)\};", source, re.S)
    if not match:
        raise ValueError("Generated image byte array was not found")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(1)))


def build_layout() -> dict:
    return {
        "background": "#000000",
        "objects": [
            {"type": "image", "name": "background", "asset": "assets/background.rgb565a8",
             "format": "rgb565a8", "source_width": 800, "source_height": 480,
             "x": 112, "y": 60, "width": 800, "height": 480},
            {"type": "bar", "name": "ui_rpm_bar", "x": 122, "y": 70,
             "width": 780, "height": 97, "min": 0, "max": 9000,
             "color": "#6DFF0C", "track_color": "#000000"},
            {"type": "image", "name": "rpm_overlay", "asset": "assets/overlay.rgb565a8",
             "format": "rgb565a8", "source_width": 800, "source_height": 480,
             "x": 112, "y": 60, "width": 800, "height": 480},
            {"type": "label", "name": "ui_gear_value", "x": 414, "y": 205,
             "width": 180, "height": 190, "font_size": 96, "align": "center",
             "color": "#FFFFFF", "text": "N"},
            {"type": "label", "name": "ui_mph_value", "x": 700, "y": 210,
             "width": 190, "height": 100, "font_size": 96, "align": "center",
             "color": "#FFFFFF", "text": "0"},
            {"type": "label", "name": "ui_mph_label", "x": 800, "y": 315,
             "width": 100, "height": 36, "font_size": 30, "align": "center",
             "color": "#FFFFFF", "text": "MPH"},
            {"type": "label", "name": "ui_rpm_value", "x": 138, "y": 64,
             "width": 220, "height": 75, "font_size": 85, "align": "center",
             "color": "#FFFFFF", "text": "0"},
            {"type": "label", "name": "rpm_label", "x": 302, "y": 92,
             "width": 100, "height": 40, "font_size": 30, "align": "center",
             "color": "#FFFFFF", "text": "RPM"},
            {"type": "label", "name": "ui_boost_value", "x": 130, "y": 454,
             "width": 120, "height": 60, "font_size": 60, "align": "center",
             "color": "#FFFFFF", "text": "0.0"},
            {"type": "label", "name": "ui_voltage_value", "x": 266, "y": 454,
             "width": 120, "height": 60, "font_size": 60, "align": "center",
             "color": "#FFFFFF", "text": "0.0"},
            {"type": "bar", "name": "ui_tps_bar", "x": 382, "y": 469,
             "width": 243, "height": 28, "min": 0, "max": 100,
             "color": "#6DFF0C", "track_color": "#000000"},
            {"type": "bar", "name": "ui_water_temp_bar", "x": 643, "y": 518,
             "width": 137, "height": 24, "min": 0, "max": 400,
             "color": "#6DFF0C", "track_color": "#000000"},
            {"type": "bar", "name": "ui_air_temp_bar", "x": 780, "y": 518,
             "width": 137, "height": 24, "min": 0, "max": 200,
             "color": "#6DFF0C", "track_color": "#000000"},
            {"type": "image", "name": "gyro", "asset": "assets/gyro.rgb565",
             "format": "rgb565", "source_width": 473, "source_height": 503,
             "x": 142, "y": 204, "width": 203, "height": 216},
            {"type": "button", "name": "dash_settings_button", "x": 944, "y": 532,
             "width": 64, "height": 52, "text": "Menu", "background": "#151619"},
        ],
    }


def convert(source_path: Path, output_path: Path) -> None:
    manifest = {
        "schema": 1,
        "id": "mackodash.ft450-legacy",
        "name": "FT450 Legacy",
        "resolution": [1024, 600],
        "lvgl": "8.4",
        "layout": "layout.json",
    }
    assets = {}
    with zipfile.ZipFile(source_path, "r") as source_zip:
        for output_name, source_name in IMAGE_SOURCES.items():
            source = source_zip.read(source_name).decode("utf-8")
            assets[output_name] = extract_image(source)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output_zip:
        output_zip.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        output_zip.writestr("layout.json", json.dumps(build_layout(), indent=2) + "\n")
        for name, data in assets.items():
            output_zip.writestr(name, data)

    print(f"Created {output_path} ({output_path.stat().st_size} bytes)")
    for name, data in assets.items():
        print(f"  {name}: {len(data)} bytes")


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert the FT450 SquareLine C export to a MackoDash theme")
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    convert(args.source, args.output)


if __name__ == "__main__":
    main()
