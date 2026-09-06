from __future__ import annotations

import re
import time
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import serial


BAUD_RATE = 115200
MAX_THEME_SIZE = 8 * 1024 * 1024
BOOT_LOGO_SIZE = 80 + 1024 * 600 * 2
LOG_NAME = re.compile(r"LOG\d{4}\.CSV")


class DeviceTransferError(RuntimeError):
    pass


@dataclass(frozen=True)
class DeviceLog:
    name: str
    size: int


class DashboardClient:
    def __init__(self, connection, response_timeout: float = 10.0) -> None:
        self.connection = connection
        self.response_timeout = response_timeout

    @classmethod
    def connect(cls, port: str, timeout: float = 10.0) -> DashboardClient:
        connection = serial.Serial(port, BAUD_RATE, timeout=0.25, write_timeout=10)
        connection.dtr = False
        connection.rts = False
        client = cls(connection, timeout)
        try:
            connection.reset_input_buffer()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                client._send_line("MDP1 HELLO")
                try:
                    if client._read_protocol_line(1.0) == "MDP1 HELLO 1":
                        return client
                except DeviceTransferError:
                    pass
            raise DeviceTransferError("The selected port did not respond as a MackoDash dashboard.")
        except Exception:
            connection.close()
            raise

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> DashboardClient:
        return self

    def __exit__(self, _type, _value, _traceback) -> None:
        self.close()

    def list_logs(self) -> list[DeviceLog]:
        self._send_line("MDP1 LIST")
        header = self._read_protocol_line()
        self._raise_for_error(header)
        parts = header.split()
        if len(parts) != 3 or parts[:2] != ["MDP1", "LIST"]:
            raise DeviceTransferError(f"Unexpected list response: {header}")
        expected = self._parse_size(parts[2])
        logs: list[DeviceLog] = []
        while True:
            line = self._read_protocol_line()
            self._raise_for_error(line)
            if line == "MDP1 END":
                break
            parts = line.split()
            if len(parts) != 4 or parts[:2] != ["MDP1", "FILE"] or not LOG_NAME.fullmatch(parts[2]):
                raise DeviceTransferError(f"Unexpected file response: {line}")
            logs.append(DeviceLog(parts[2], self._parse_size(parts[3])))
        if len(logs) != expected:
            raise DeviceTransferError("The dashboard returned an incomplete log list.")
        return logs

    def download_log(
        self,
        log: DeviceLog,
        destination: Path,
        progress: Callable[[int, int], None] | None = None,
    ) -> Path:
        if not LOG_NAME.fullmatch(log.name):
            raise DeviceTransferError("Invalid driving log name.")
        self._send_line(f"MDP1 GET {log.name}")
        header = self._read_protocol_line()
        self._raise_for_error(header)
        parts = header.split()
        if len(parts) != 4 or parts[:2] != ["MDP1", "DATA"] or parts[2] != log.name:
            raise DeviceTransferError(f"Unexpected download response: {header}")
        size = self._parse_size(parts[3])
        temporary = destination.with_name(destination.name + ".part")
        received = 0
        checksum = 0
        try:
            with temporary.open("wb") as output:
                while received < size:
                    block = self.connection.read(min(4096, size - received))
                    if not block:
                        raise DeviceTransferError("The dashboard stopped responding during download.")
                    output.write(block)
                    checksum = zlib.crc32(block, checksum)
                    received += len(block)
                    if progress:
                        progress(received, size)
            completion = self._read_protocol_line()
            self._raise_for_error(completion)
            parts = completion.split()
            if len(parts) != 3 or parts[:2] != ["MDP1", "DONE"]:
                raise DeviceTransferError(f"Unexpected download completion: {completion}")
            if checksum != int(parts[2], 16):
                raise DeviceTransferError("The downloaded log failed its CRC check.")
            temporary.replace(destination)
            return destination
        except Exception:
            temporary.unlink(missing_ok=True)
            raise

    def upload_theme(
        self,
        package: Path,
        progress: Callable[[int, int], None] | None = None,
    ) -> None:
        if not package.name.lower().endswith(".mdtheme.zip") or not zipfile.is_zipfile(package):
            raise DeviceTransferError("Choose a valid .mdtheme.zip package.")
        size = package.stat().st_size
        if not 0 < size <= MAX_THEME_SIZE:
            raise DeviceTransferError("Theme packages must be between 1 byte and 8 MiB.")
        checksum = 0
        with package.open("rb") as source:
            while block := source.read(65536):
                checksum = zlib.crc32(block, checksum)
        self._send_line(f"MDP1 PUTTHEME {package.name} {size} {checksum:08X}")
        ready = self._read_protocol_line()
        self._raise_for_error(ready)
        if ready != "MDP1 READY":
            raise DeviceTransferError(f"Unexpected upload response: {ready}")
        sent = 0
        with package.open("rb") as source:
            while block := source.read(4096):
                self.connection.write(block)
                sent += len(block)
                if progress:
                    progress(sent, size)
        self.connection.flush()
        completion = self._read_protocol_line(15.0)
        self._raise_for_error(completion)
        if completion != "MDP1 DONE REBOOTING":
            raise DeviceTransferError(f"Unexpected upload completion: {completion}")

    def upload_boot_logo(
        self,
        package: Path,
        progress: Callable[[int, int], None] | None = None,
    ) -> None:
        if package.suffix.lower() != ".mdlogo" or package.stat().st_size != BOOT_LOGO_SIZE:
            raise DeviceTransferError("Choose a valid MackoDash .mdlogo file.")
        with package.open("rb") as source:
            if source.read(4) != b"MDL1":
                raise DeviceTransferError("The boot logo header is invalid.")
            source.seek(0)
            checksum = 0
            while block := source.read(65536):
                checksum = zlib.crc32(block, checksum)
        self._send_line(f"MDP1 PUTLOGO {package.name} {BOOT_LOGO_SIZE} {checksum:08X}")
        ready = self._read_protocol_line()
        self._raise_for_error(ready)
        if ready != "MDP1 READY":
            raise DeviceTransferError(f"Unexpected upload response: {ready}")
        sent = 0
        with package.open("rb") as source:
            while block := source.read(4096):
                self.connection.write(block)
                sent += len(block)
                if progress:
                    progress(sent, BOOT_LOGO_SIZE)
        self.connection.flush()
        completion = self._read_protocol_line(15.0)
        self._raise_for_error(completion)
        if completion != "MDP1 DONE REBOOTING":
            raise DeviceTransferError(f"Unexpected upload completion: {completion}")

    def _send_line(self, line: str) -> None:
        self.connection.write((line + "\n").encode("ascii"))
        self.connection.flush()

    def _read_protocol_line(self, timeout: float | None = None) -> str:
        deadline = time.monotonic() + (self.response_timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            raw = self.connection.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            marker = line.find("MDP1 ")
            if marker >= 0:
                return line[marker:]
        raise DeviceTransferError("Timed out waiting for the dashboard.")

    @staticmethod
    def _parse_size(value: str) -> int:
        try:
            size = int(value)
        except ValueError as error:
            raise DeviceTransferError(f"Invalid size from dashboard: {value}") from error
        if size < 0:
            raise DeviceTransferError(f"Invalid size from dashboard: {value}")
        return size

    @staticmethod
    def _raise_for_error(line: str) -> None:
        if line.startswith("MDP1 ERROR "):
            reason = line.removeprefix("MDP1 ERROR ").replace("_", " ").lower()
            raise DeviceTransferError(f"Dashboard reported: {reason}.")