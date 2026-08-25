from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
import zipfile
from io import BytesIO
from pathlib import Path
from unittest.mock import patch

from firmware import (
    BUNDLE_MANIFEST,
    IMAGE_DEFINITIONS,
    FirmwareValidationError,
    create_firmware_bundle,
    download_latest_firmware,
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

    def test_latest_download_is_validated_before_replacing_destination(self) -> None:
        destination = Path(self.directory.name) / "downloaded.zip"

        class Response(BytesIO):
            headers = {"Content-Length": str(self.bundle.stat().st_size)}

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                self.close()

        with patch("firmware.urlopen", return_value=Response(self.bundle.read_bytes())):
            downloaded = download_latest_firmware(destination)
        self.assertEqual(downloaded, destination)
        self.assertEqual(validate_firmware(downloaded).version, validate_firmware(self.bundle).version)

    def test_invalid_download_does_not_replace_existing_bundle(self) -> None:
        destination = Path(self.directory.name) / "downloaded.zip"
        destination.write_bytes(self.bundle.read_bytes())
        original_digest = hashlib.sha256(destination.read_bytes()).hexdigest()

        class Response(BytesIO):
            headers = {"Content-Length": "9"}

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                self.close()

        with patch("firmware.urlopen", return_value=Response(b"not a zip")):
            with self.assertRaises(FirmwareValidationError):
                download_latest_firmware(destination)
        self.assertEqual(hashlib.sha256(destination.read_bytes()).hexdigest(), original_digest)

    def test_single_github_wrapper_folder_is_accepted_and_flashable(self) -> None:
        wrapped = Path(self.directory.name) / "github-download.zip"
        with zipfile.ZipFile(self.bundle) as source, zipfile.ZipFile(wrapped, "w", zipfile.ZIP_DEFLATED) as output:
            output.writestr("MackoDash-main/", b"")
            for name in source.namelist():
                output.writestr(f"MackoDash-main/{name}", source.read(name))

        info = validate_firmware(wrapped)
        self.assertTrue(all(image.archive_name.startswith("MackoDash-main/") for image in info.images))
        with patch("firmware.esptool.main") as main:
            flash_firmware("COM10", info, lambda _line: None)
        self.assertEqual(main.call_count, 1)

    def test_multiple_wrapper_folders_are_rejected(self) -> None:
        wrapped = Path(self.directory.name) / "ambiguous.zip"
        with zipfile.ZipFile(self.bundle) as source, zipfile.ZipFile(wrapped, "w", zipfile.ZIP_DEFLATED) as output:
            for name in source.namelist():
                output.writestr(f"first/{name}", source.read(name))
            output.writestr("second/readme.txt", "unexpected")
        with self.assertRaisesRegex(FirmwareValidationError, "inside one folder"):
            validate_firmware(wrapped)

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