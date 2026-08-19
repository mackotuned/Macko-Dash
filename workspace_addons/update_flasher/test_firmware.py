from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from firmware import (
    BUNDLE_MANIFEST,
    IMAGE_DEFINITIONS,
    FirmwareValidationError,
    create_firmware_bundle,
    flash_firmware,
    validate_firmware,
)


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "build"


class FirmwareValidationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.bundle = create_firmware_bundle(BUILD_DIR, Path(self.directory.name) / "MackoDash-Firmware.zip")

    def _rewrite_bundle(self, mutate) -> Path:
        path = Path(self.directory.name) / "modified.zip"
        with zipfile.ZipFile(self.bundle) as source:
            entries = {name: source.read(name) for name in source.namelist()}
        mutate(entries)
        with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as output:
            for name, data in entries.items():
                output.writestr(name, data)
        return path

    def test_reference_bundle_is_accepted(self) -> None:
        info = validate_firmware(self.bundle)
        self.assertEqual(info.project_name, "mackodash")
        self.assertEqual(len(info.images), 5)
        self.assertEqual(len(info.sha256), 64)
        self.assertEqual(
            [image.definition.offset for image in info.images],
            [definition.offset for definition in IMAGE_DEFINITIONS],
        )

    def test_corrupted_image_is_rejected(self) -> None:
        def corrupt(entries: dict[str, bytes]) -> None:
            data = bytearray(entries["storage.bin"])
            data[1000] ^= 1
            entries["storage.bin"] = bytes(data)

        with self.assertRaisesRegex(FirmwareValidationError, "Checksum"):
            validate_firmware(self._rewrite_bundle(corrupt))

    def test_wrong_offset_is_rejected(self) -> None:
        def change_offset(entries: dict[str, bytes]) -> None:
            manifest = json.loads(entries[BUNDLE_MANIFEST])
            manifest["files"][0]["offset"] = 0
            entries[BUNDLE_MANIFEST] = json.dumps(manifest).encode()

        with self.assertRaisesRegex(FirmwareValidationError, "flash offset"):
            validate_firmware(self._rewrite_bundle(change_offset))

    def test_missing_image_is_rejected(self) -> None:
        def remove_image(entries: dict[str, bytes]) -> None:
            del entries["storage.bin"]

        with self.assertRaisesRegex(FirmwareValidationError, "only the manifest"):
            validate_firmware(self._rewrite_bundle(remove_image))

    def test_wrong_chip_application_is_rejected(self) -> None:
        def change_chip(entries: dict[str, bytes]) -> None:
            data = bytearray(entries["mackodash.bin"])
            data[12:14] = (0).to_bytes(2, "little")
            entries["mackodash.bin"] = bytes(data)
            manifest = json.loads(entries[BUNDLE_MANIFEST])
            application = next(item for item in manifest["files"] if item["name"] == "application")
            application["sha256"] = hashlib.sha256(data).hexdigest()
            entries[BUNDLE_MANIFEST] = json.dumps(manifest).encode()

        with self.assertRaisesRegex(FirmwareValidationError, "Wrong processor"):
            validate_firmware(self._rewrite_bundle(change_chip))

    def test_flash_command_writes_all_full_bundle_offsets(self) -> None:
        info = validate_firmware(self.bundle)
        captured: list[str] = []
        with patch("firmware.esptool.main", side_effect=lambda arguments: print("esptool test output")) as main:
            flash_firmware("COM10", info, captured.append)
        arguments = main.call_args.args[0]
        self.assertEqual(captured, ["esptool test output"])
        self.assertEqual(arguments[arguments.index("--chip") + 1], "esp32p4")
        for definition in IMAGE_DEFINITIONS:
            offset_index = arguments.index(hex(definition.offset))
            self.assertEqual(Path(arguments[offset_index + 1]).name, definition.filename)
        self.assertNotIn("erase-flash", arguments)


if __name__ == "__main__":
    unittest.main()