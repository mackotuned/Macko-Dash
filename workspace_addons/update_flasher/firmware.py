from __future__ import annotations

import contextlib
import hashlib
import io
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import esptool
from esptool.bin_image import LoadFirmwareImage


APP_OFFSET = 0x20000
APP_PARTITION_SIZE = 0x600000
ESP32_P4_CHIP_ID = 18
APP_DESC_MAGIC = 0xABCD5432
EXPECTED_PROJECT = "mackodash"


class FirmwareValidationError(ValueError):
    pass


@dataclass(frozen=True)
class FirmwareInfo:
    path: Path
    size: int
    sha256: str
    project_name: str
    version: str
    compile_date: str
    compile_time: str
    idf_version: str


def _decode_field(data: bytes, offset: int, length: int) -> str:
    return data[offset:offset + length].split(b"\0", 1)[0].decode("utf-8", errors="replace").strip()


def _read_app_description(data: bytes) -> tuple[str, str, str, str, str]:
    marker = struct.pack("<I", APP_DESC_MAGIC)
    offset = data.find(marker)
    if offset < 0 or offset + 144 > len(data):
        raise FirmwareValidationError("This is not an ESP-IDF application image (application metadata is missing).")

    version = _decode_field(data, offset + 16, 32)
    project_name = _decode_field(data, offset + 48, 32)
    compile_time = _decode_field(data, offset + 80, 16)
    compile_date = _decode_field(data, offset + 96, 16)
    idf_version = _decode_field(data, offset + 112, 32)
    return project_name, version, compile_date, compile_time, idf_version


def validate_firmware(path: str | Path) -> FirmwareInfo:
    firmware_path = Path(path).expanduser().resolve()
    if firmware_path.suffix.lower() != ".bin":
        raise FirmwareValidationError("Choose the MackoDash .bin application file.")
    if not firmware_path.is_file():
        raise FirmwareValidationError("The selected firmware file does not exist.")

    size = firmware_path.stat().st_size
    if size < 64 * 1024:
        raise FirmwareValidationError("The selected file is too small to be MackoDash firmware.")
    if size > APP_PARTITION_SIZE:
        raise FirmwareValidationError(
            f"Firmware is {size:,} bytes, larger than the {APP_PARTITION_SIZE:,}-byte application partition."
        )

    data = firmware_path.read_bytes()
    try:
        image = LoadFirmwareImage("esp32p4", data)
        image.verify()
    except Exception as error:
        raise FirmwareValidationError(f"Invalid or damaged ESP32-P4 firmware: {error}") from error

    if image.chip_id != ESP32_P4_CHIP_ID:
        raise FirmwareValidationError(
            f"Wrong processor image (chip ID {image.chip_id}); MackoDash requires ESP32-P4 chip ID {ESP32_P4_CHIP_ID}."
        )
    if not image.append_digest or image.stored_digest != image.calc_digest:
        raise FirmwareValidationError("The firmware SHA-256 validation digest is missing or invalid.")

    project_name, version, compile_date, compile_time, idf_version = _read_app_description(data)
    if project_name != EXPECTED_PROJECT:
        raise FirmwareValidationError(
            f"Wrong application project '{project_name or 'unknown'}'; expected '{EXPECTED_PROJECT}'."
        )

    return FirmwareInfo(
        path=firmware_path,
        size=size,
        sha256=hashlib.sha256(data).hexdigest(),
        project_name=project_name,
        version=version or "unknown",
        compile_date=compile_date or "unknown",
        compile_time=compile_time or "unknown",
        idf_version=idf_version or "unknown",
    )


class _LogWriter(io.TextIOBase):
    def __init__(self, callback: Callable[[str], None]) -> None:
        self._callback = callback
        self._pending = ""
        self._callback_stdout = sys.stdout
        self._callback_stderr = sys.stderr

    def _emit(self, line: str) -> None:
        with contextlib.redirect_stdout(self._callback_stdout), contextlib.redirect_stderr(self._callback_stderr):
            self._callback(line)

    def write(self, value: str) -> int:
        self._pending += value
        while "\n" in self._pending:
            line, self._pending = self._pending.split("\n", 1)
            if line.strip():
                self._emit(line.rstrip())
        return len(value)

    def flush(self) -> None:
        if self._pending.strip():
            self._emit(self._pending.rstrip())
        self._pending = ""


def flash_firmware(port: str, firmware: FirmwareInfo, log: Callable[[str], None]) -> None:
    if not port.upper().startswith("COM"):
        raise ValueError("Select a valid Windows COM port.")

    writer = _LogWriter(log)
    arguments = [
        "--chip", "esp32p4",
        "--port", port,
        "--baud", "460800",
        "--before", "default-reset",
        "--after", "hard-reset",
        "write-flash",
        "--flash-mode", "dio",
        "--flash-freq", "40m",
        "--flash-size", "16MB",
        hex(APP_OFFSET), str(firmware.path),
    ]
    with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
        try:
            esptool.main(arguments)
        finally:
            writer.flush()