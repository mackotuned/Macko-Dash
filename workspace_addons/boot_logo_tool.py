from __future__ import annotations

import re
import struct
import zlib
from pathlib import Path

from PIL import Image, ImageColor, ImageOps


LOGO_WIDTH = 1024
LOGO_HEIGHT = 600
LOGO_HEADER_SIZE = 80
LOGO_PIXEL_SIZE = LOGO_WIDTH * LOGO_HEIGHT * 2
LOGO_FILE_SIZE = LOGO_HEADER_SIZE + LOGO_PIXEL_SIZE
LOGO_MAGIC = b"MDL1"
LOGO_VERSION = 1


class BootLogoError(ValueError):
    pass


def safe_logo_filename(display_name: str) -> str:
    stem = re.sub(r"[^A-Za-z0-9_-]+", "_", display_name.strip()).strip("_")
    return f"{stem[:80] or 'custom_boot_logo'}.mdlogo"


def render_boot_logo(source: Path, mode: str = "fit", background: str = "#000000") -> Image.Image:
    if mode not in {"fit", "fill"}:
        raise BootLogoError("Image mode must be 'fit' or 'fill'.")
    try:
        background_rgb = ImageColor.getrgb(background)
    except ValueError as error:
        raise BootLogoError("Choose a valid background color.") from error
    try:
        with Image.open(source) as opened:
            image = ImageOps.exif_transpose(opened).convert("RGBA")
    except (OSError, ValueError) as error:
        raise BootLogoError("Choose a valid PNG or JPEG image.") from error

    canvas = Image.new("RGBA", (LOGO_WIDTH, LOGO_HEIGHT), (*background_rgb, 255))
    if mode == "fill":
        composed = ImageOps.fit(image, canvas.size, method=Image.Resampling.LANCZOS)
        canvas.alpha_composite(composed)
    else:
        contained = ImageOps.contain(image, canvas.size, method=Image.Resampling.LANCZOS)
        position = ((LOGO_WIDTH - contained.width) // 2, (LOGO_HEIGHT - contained.height) // 2)
        canvas.alpha_composite(contained, position)
    return canvas.convert("RGB")


def rgb565_bytes(image: Image.Image) -> bytes:
    if image.size != (LOGO_WIDTH, LOGO_HEIGHT):
        raise BootLogoError(f"Boot logo image must be {LOGO_WIDTH}x{LOGO_HEIGHT} pixels.")
    source = image.convert("RGB").tobytes()
    output = bytearray(LOGO_PIXEL_SIZE)
    destination_offset = 0
    for source_offset in range(0, len(source), 3):
        red, green, blue = source[source_offset:source_offset + 3]
        value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        output[destination_offset] = value & 0xFF
        output[destination_offset + 1] = value >> 8
        destination_offset += 2
    return bytes(output)


def build_boot_logo(source: Path, display_name: str, mode: str = "fit",
                    background: str = "#000000") -> bytes:
    name = display_name.strip()
    if not name:
        raise BootLogoError("Enter a boot logo name.")
    encoded_name = name.encode("utf-8")
    if len(encoded_name) > 59:
        raise BootLogoError("Boot logo names must be at most 59 UTF-8 bytes.")

    pixels = rgb565_bytes(render_boot_logo(source, mode, background))
    header = bytearray(LOGO_HEADER_SIZE)
    header[:4] = LOGO_MAGIC
    struct.pack_into("<HHH", header, 4, LOGO_VERSION, LOGO_WIDTH, LOGO_HEIGHT)
    struct.pack_into("<II", header, 12, len(pixels), zlib.crc32(pixels))
    header[20:20 + len(encoded_name)] = encoded_name
    return bytes(header) + pixels


def export_boot_logo(source: Path, destination: Path, display_name: str,
                     mode: str = "fit", background: str = "#000000") -> Path:
    package = build_boot_logo(source, display_name, mode, background)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(package)
    return destination