from __future__ import annotations

import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from PIL import Image

from boot_logo_tool import (
    LOGO_FILE_SIZE,
    LOGO_HEADER_SIZE,
    LOGO_HEIGHT,
    LOGO_MAGIC,
    LOGO_PIXEL_SIZE,
    LOGO_WIDTH,
    BootLogoError,
    build_boot_logo,
    safe_logo_filename,
)


class BootLogoToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.source = Path(self.temporary.name) / "source.png"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_builds_firmware_compatible_rgb565_package(self) -> None:
        Image.new("RGB", (1, 1), (255, 0, 0)).save(self.source)

        package = build_boot_logo(self.source, "Creator Logo", background="#000000")

        self.assertEqual(len(package), LOGO_FILE_SIZE)
        self.assertEqual(package[:4], LOGO_MAGIC)
        self.assertEqual(struct.unpack_from("<HHH", package, 4), (1, LOGO_WIDTH, LOGO_HEIGHT))
        self.assertEqual(struct.unpack_from("<I", package, 12)[0], LOGO_PIXEL_SIZE)
        self.assertEqual(package[20:32], b"Creator Logo")
        pixels = package[LOGO_HEADER_SIZE:]
        self.assertEqual(struct.unpack_from("<I", package, 16)[0], zlib.crc32(pixels))
        center = ((LOGO_HEIGHT // 2) * LOGO_WIDTH + LOGO_WIDTH // 2) * 2
        self.assertEqual(pixels[center:center + 2], b"\x00\xF8")
        self.assertEqual(zlib.crc32(package), zlib.crc32(package, 0))

    def test_fill_crops_to_full_canvas(self) -> None:
        Image.new("RGBA", (10, 20), (0, 255, 0, 255)).save(self.source)
        package = build_boot_logo(self.source, "Fill", mode="fill")
        self.assertEqual(package[LOGO_HEADER_SIZE:LOGO_HEADER_SIZE + 2], b"\xE0\x07")

    def test_rejects_invalid_name_and_mode(self) -> None:
        Image.new("RGB", (2, 2), "white").save(self.source)
        with self.assertRaises(BootLogoError):
            build_boot_logo(self.source, "")
        with self.assertRaises(BootLogoError):
            build_boot_logo(self.source, "Logo", mode="stretch")

    def test_safe_filename_is_ascii(self) -> None:
        self.assertEqual(safe_logo_filename("Creator's Launch!"), "Creator_s_Launch.mdlogo")


if __name__ == "__main__":
    unittest.main()