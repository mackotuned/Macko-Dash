import argparse
import json
import re
import sys
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


SUPPORTED_WIDGETS = {
    "label": "label",
    "bar": "bar",
    "img": "image",
    "arc": "arc",
    "btn": "button",
}
THEME_ID_PATTERN = re.compile(r"^[a-z0-9._-]+$")
EXACT_BUILT_IN_FONTS = {"lv_font_montserrat_12", "lv_font_montserrat_14", "lv_font_montserrat_28", "lv_font_montserrat_44"}


class ConversionError(ValueError):
    pass


@dataclass
class SourceTree:
    path: Path

    def __post_init__(self) -> None:
        self._zip = zipfile.ZipFile(self.path) if self.path.is_file() else None
        if self._zip:
            self._names = [name.replace("\\", "/") for name in self._zip.namelist()]
        else:
            self._names = [file.relative_to(self.path).as_posix() for file in self.path.rglob("*") if file.is_file()]

    def close(self) -> None:
        if self._zip:
            self._zip.close()

    def names(self) -> list[str]:
        return self._names

    def read_bytes(self, name: str) -> bytes:
        normalized = name.replace("\\", "/")
        if self._zip:
            actual = next((item for item in self._zip.namelist() if item.replace("\\", "/") == normalized), None)
            if actual is None:
                raise ConversionError(f"Missing export file: {normalized}")
            return self._zip.read(actual)
        return (self.path / Path(*PurePosixPath(normalized).parts)).read_bytes()

    def read_text(self, name: str) -> str:
        return self.read_bytes(name).decode("utf-8-sig")


def find_export_root(source: SourceTree) -> str:
    project_files = [name for name in source.names() if name.endswith("project.info")]
    if len(project_files) != 1:
        raise ConversionError(f"Expected one project.info file, found {len(project_files)}")
    return project_files[0][: -len("project.info")]


def match_int(block: str, pattern: str, default: int = 0) -> int:
    match = re.search(pattern, block)
    return int(match.group(1)) if match else default


def match_symbol(block: str, pattern: str) -> str | None:
    match = re.search(pattern, block)
    return match.group(1) if match else None


def parse_dimension(block: str, variable: str, axis: str) -> int | str:
    match = re.search(rf"lv_obj_set_{axis}\({re.escape(variable)},\s*([^\)]+)\)", block)
    if not match:
        return "content"
    value = match.group(1).strip()
    return "content" if value == "LV_SIZE_CONTENT" else int(value)


def parse_color(block: str, variable: str, part: str, fallback: str) -> str:
    match = re.search(
        rf"lv_obj_set_style_(?:text_|bg_|arc_)?color\({re.escape(variable)},\s*"
        rf"lv_color_hex\(0x([0-9A-Fa-f]{{6}})\),\s*{part}",
        block,
    )
    return f"#{match.group(1).upper()}" if match else fallback


def parse_image(source: SourceTree, path: str) -> tuple[str, dict, bytes]:
    text = source.read_text(path)
    symbol_match = re.search(r"const\s+lv_img_dsc_t\s+(\w+)\s*=", text)
    width_match = re.search(r"\.header\.w\s*=\s*(\d+)", text)
    height_match = re.search(r"\.header\.h\s*=\s*(\d+)", text)
    format_match = re.search(r"\.header\.cf\s*=\s*(LV_IMG_CF_\w+)", text)
    data_match = re.search(r"uint8_t\s+\w+_data\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not all((symbol_match, width_match, height_match, format_match, data_match)):
        raise ConversionError(f"Cannot parse generated image: {path}")
    formats = {
        "LV_IMG_CF_TRUE_COLOR": ("rgb565", 2),
        "LV_IMG_CF_TRUE_COLOR_ALPHA": ("rgb565a8", 3),
    }
    if format_match.group(1) not in formats:
        raise ConversionError(f"Unsupported image format {format_match.group(1)} in {path}")
    image_format, bytes_per_pixel = formats[format_match.group(1)]
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{1,2})", data_match.group(1)))
    width = int(width_match.group(1))
    height = int(height_match.group(1))
    expected_size = width * height * bytes_per_pixel
    if len(data) != expected_size:
        raise ConversionError(f"Image {path} has {len(data)} bytes; expected {expected_size}")
    metadata = {"format": image_format, "source_width": width, "source_height": height}
    return symbol_match.group(1), metadata, data


def parse_screen(text: str, images: dict[str, dict], allow_font_substitution: bool) -> tuple[list[dict], list[str]]:
    creation = re.compile(r"^\s*(\w+)\s*=\s*lv_(label|bar|img|arc|btn)_create\([^;]+;", re.M)
    matches = list(creation.finditer(text))
    objects: list[dict] = []
    warnings: list[str] = []
    for index, match in enumerate(matches):
        variable, squareline_type = match.group(1), match.group(2)
        block_end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        block = text[match.start():block_end]
        item: dict = {
            "type": SUPPORTED_WIDGETS[squareline_type],
            "name": variable,
            "object_align": (match_symbol(block, rf"lv_obj_set_align\({variable},\s*(LV_ALIGN_\w+)\)") or "LV_ALIGN_TOP_LEFT").removeprefix("LV_ALIGN_").lower(),
            "x": match_int(block, rf"lv_obj_set_x\({variable},\s*(-?\d+)\)"),
            "y": match_int(block, rf"lv_obj_set_y\({variable},\s*(-?\d+)\)"),
            "width": parse_dimension(block, variable, "width"),
            "height": parse_dimension(block, variable, "height"),
        }

        if squareline_type == "label":
            text_match = re.search(rf'lv_label_set_text\({variable},\s*"((?:\\.|[^"\\])*)"\)', block)
            item["text"] = bytes(text_match.group(1), "utf-8").decode("unicode_escape") if text_match else ""
            item["color"] = parse_color(block, variable, "LV_PART_MAIN", "#FFFFFF")
            font = match_symbol(block, rf"lv_obj_set_style_text_font\({variable},\s*&?(\w+)")
            if font:
                size_match = re.search(r"_(\d+)$", font)
                item["font_size"] = int(size_match.group(1)) if size_match else 14
                item["font"] = font
                if font not in EXACT_BUILT_IN_FONTS:
                    message = f"{variable}: custom or unavailable font {font} requires substitution"
                    if not allow_font_substitution:
                        raise ConversionError(message)
                    warnings.append(message)
        elif squareline_type == "bar":
            range_match = re.search(rf"lv_bar_set_range\({variable},\s*(-?\d+),\s*(-?\d+)\)", block)
            item["min"] = int(range_match.group(1)) if range_match else 0
            item["max"] = int(range_match.group(2)) if range_match else 100
            item["track_color"] = parse_color(block, variable, "LV_PART_MAIN", "#25282D")
            item["color"] = parse_color(block, variable, "LV_PART_INDICATOR", "#E4002B")
            item["track_opa"] = match_int(block, rf"lv_obj_set_style_bg_opa\({variable},\s*(\d+),\s*LV_PART_MAIN", 255)
            item["indicator_opa"] = match_int(block, rf"lv_obj_set_style_bg_opa\({variable},\s*(\d+),\s*LV_PART_INDICATOR", 255)
            item["radius"] = match_int(block, rf"lv_obj_set_style_radius\({variable},\s*(-?\d+),\s*LV_PART_MAIN")
            angle = match_int(block, rf"lv_obj_set_style_transform_angle\({variable},\s*(-?\d+)")
            if angle:
                item["transform_angle"] = angle / 10
            indicator_image = match_symbol(block, rf"lv_obj_set_style_bg_img_src\({variable},\s*&?(\w+)")
            if indicator_image:
                if indicator_image not in images:
                    raise ConversionError(f"{variable}: missing indicator image {indicator_image}")
                item["indicator_asset"] = images[indicator_image]["asset"]
                item["indicator_format"] = images[indicator_image]["metadata"]["format"]
                item["indicator_source_width"] = images[indicator_image]["metadata"]["source_width"]
                item["indicator_source_height"] = images[indicator_image]["metadata"]["source_height"]
        elif squareline_type == "img":
            image_symbol = match_symbol(block, rf"lv_img_set_src\({variable},\s*&?(\w+)\)")
            if not image_symbol or image_symbol not in images:
                raise ConversionError(f"{variable}: missing generated image source")
            item.update(images[image_symbol]["metadata"])
            item["asset"] = images[image_symbol]["asset"]
            item["zoom"] = match_int(block, rf"lv_img_set_zoom\({variable},\s*(\d+)\)", 256)
        objects.append(item)
    if not objects:
        raise ConversionError("No supported objects found in the SquareLine screen")
    return objects, warnings


def convert(args: argparse.Namespace) -> dict:
    if not THEME_ID_PATTERN.fullmatch(args.theme_id) or len(args.theme_id) >= 64:
        raise ConversionError("Theme ID must be under 64 characters and use only lowercase letters, numbers, '.', '_' or '-'")
    if not args.name.strip() or len(args.name) >= 96:
        raise ConversionError("Theme name must be between 1 and 95 characters")
    if not 160 <= args.width <= 2048 or not 120 <= args.height <= 2048:
        raise ConversionError("Resolution must be between 160x120 and 2048x2048")
    source = SourceTree(args.source)
    try:
        root = find_export_root(source)
        screen_files = sorted(name for name in source.names() if name.startswith(f"{root}screens/") and name.endswith(".c"))
        if len(screen_files) != 1:
            raise ConversionError(f"Expected one screen source, found {len(screen_files)}")
        image_files = sorted(name for name in source.names() if name.startswith(f"{root}images/") and name.endswith(".c"))
        images: dict[str, dict] = {}
        assets: dict[str, bytes] = {}
        for image_path in image_files:
            symbol, metadata, data = parse_image(source, image_path)
            asset_name = f"assets/{Path(image_path).stem}.raw"
            images[symbol] = {"metadata": metadata, "asset": asset_name}
            assets[asset_name] = data
        objects, warnings = parse_screen(source.read_text(screen_files[0]), images, args.allow_font_substitution)
    finally:
        source.close()

    manifest = {
        "schema": 1,
        "id": args.theme_id,
        "name": args.name,
        "resolution": [args.width, args.height],
        "lvgl": "8.4",
        "layout": "layout.json",
    }
    layout = {"background": "#000000", "objects": objects}
    report = {
        "status": "warning" if warnings else "ready",
        "source": str(args.source),
        "screen": screen_files[0],
        "objects": len(objects),
        "images": len(assets),
        "warnings": warnings,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        output.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        output.writestr("layout.json", json.dumps(layout, indent=2) + "\n")
        output.writestr("conversion-report.json", json.dumps(report, indent=2) + "\n")
        for name, data in assets.items():
            output.writestr(name, data)
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert a SquareLine C export to a validated MackoDash theme")
    parser.add_argument("source", type=Path, help="SquareLine export directory or ZIP")
    parser.add_argument("output", type=Path, help="Output .mdtheme.zip")
    parser.add_argument("--id", dest="theme_id", required=True, help="Unique lowercase theme ID")
    parser.add_argument("--name", required=True, help="Theme display name")
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--allow-font-substitution", action="store_true")
    return parser.parse_args()


def main() -> int:
    try:
        report = convert(parse_args())
    except (ConversionError, OSError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())