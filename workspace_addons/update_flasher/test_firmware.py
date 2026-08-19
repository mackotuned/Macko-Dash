from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from firmware import APP_OFFSET, FirmwareValidationError, flash_firmware, validate_firmware


ROOT = Path(__file__).resolve().parents[2]
REFERENCE_FIRMWARE = ROOT / "build" / "mackodash.bin"


class FirmwareValidationTests(unittest.TestCase):
    def test_reference_firmware_is_accepted(self) -> None:
        info = validate_firmware(REFERENCE_FIRMWARE)
        self.assertEqual(info.project_name, "mackodash")
        self.assertEqual(info.size, REFERENCE_FIRMWARE.stat().st_size)
        self.assertEqual(len(info.sha256), 64)

    def test_corrupted_firmware_is_rejected(self) -> None:
        data = bytearray(REFERENCE_FIRMWARE.read_bytes())
        data[1000] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "corrupt.bin"
            path.write_bytes(data)
            with self.assertRaises(FirmwareValidationError):
                validate_firmware(path)

    def test_wrong_chip_is_rejected(self) -> None:
        data = bytearray(REFERENCE_FIRMWARE.read_bytes())
        data[12:14] = (0).to_bytes(2, "little")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "wrong-chip.bin"
            path.write_bytes(data)
            with self.assertRaisesRegex(FirmwareValidationError, "Wrong processor"):
                validate_firmware(path)

    def test_flash_command_writes_only_app_offset(self) -> None:
        info = validate_firmware(REFERENCE_FIRMWARE)
        captured: list[str] = []
        with patch("firmware.esptool.main", side_effect=lambda arguments: print("esptool test output")) as main:
            flash_firmware("COM10", info, captured.append)
        arguments = main.call_args.args[0]
        self.assertEqual(captured, ["esptool test output"])
        self.assertIn("--chip", arguments)
        self.assertEqual(arguments[arguments.index("--chip") + 1], "esp32p4")
        self.assertEqual(arguments[-2], hex(APP_OFFSET))
        self.assertEqual(arguments[-1], str(info.path))
        self.assertNotIn("erase-flash", arguments)


if __name__ == "__main__":
    unittest.main()