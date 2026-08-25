from __future__ import annotations

import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

from device_transfer import DashboardClient, DeviceLog


class FakeSerial:
    def __init__(self, payload: bytes = b"test log\n") -> None:
        self.payload = payload
        self.input = bytearray()
        self.command = bytearray()
        self.upload_remaining = 0
        self.upload = bytearray()

    def write(self, data: bytes) -> int:
        if self.upload_remaining:
            taken = data[:self.upload_remaining]
            self.upload.extend(taken)
            self.upload_remaining -= len(taken)
            if self.upload_remaining == 0:
                self.input.extend(b"MDP1 DONE REBOOTING\n")
            return len(data)
        self.command.extend(data)
        if b"\n" not in self.command:
            return len(data)
        line = bytes(self.command).decode("ascii").strip()
        self.command.clear()
        if line == "MDP1 LIST":
            self.input.extend(f"boot text\nMDP1 LIST 1\nMDP1 FILE LOG0001.CSV {len(self.payload)}\nMDP1 END\n".encode())
        elif line == "MDP1 GET LOG0001.CSV":
            checksum = zlib.crc32(self.payload)
            self.input.extend(f"MDP1 DATA LOG0001.CSV {len(self.payload)}\n".encode())
            self.input.extend(self.payload)
            self.input.extend(f"\nMDP1 DONE {checksum:08X}\n".encode())
        elif line.startswith("MDP1 PUTTHEME "):
            self.upload_remaining = int(line.split()[3])
            self.input.extend(b"MDP1 READY\n")
        return len(data)

    def read(self, size: int) -> bytes:
        data = bytes(self.input[:size])
        del self.input[:size]
        return data

    def readline(self) -> bytes:
        try:
            end = self.input.index(ord("\n")) + 1
        except ValueError:
            return b""
        return self.read(end)

    def flush(self) -> None:
        pass


class DeviceTransferTests(unittest.TestCase):
    def test_lists_and_downloads_log_with_crc(self) -> None:
        connection = FakeSerial()
        client = DashboardClient(connection, response_timeout=0.1)
        logs = client.list_logs()
        self.assertEqual(logs, [DeviceLog("LOG0001.CSV", len(connection.payload))])
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / logs[0].name
            client.download_log(logs[0], destination)
            self.assertEqual(destination.read_bytes(), connection.payload)

    def test_uploads_valid_theme_package(self) -> None:
        connection = FakeSerial()
        client = DashboardClient(connection, response_timeout=0.1)
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "Track.mdtheme.zip"
            with zipfile.ZipFile(package, "w") as archive:
                archive.writestr("manifest.json", "{}")
            expected = package.read_bytes()
            client.upload_theme(package)
        self.assertEqual(bytes(connection.upload), expected)


if __name__ == "__main__":
    unittest.main()