from __future__ import annotations

import contextlib
import hashlib
import io
import json
import struct
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable
from urllib.request import Request, urlopen

import esptool
from esptool.bin_image import LoadFirmwareImage


ESP32_P4_CHIP_ID = 18
APP_DESC_MAGIC = 0xABCD5432
EXPECTED_PROJECT = "mackodash"
BUNDLE_SCHEMA = 1
BUNDLE_MANIFEST = "manifest.json"
MAX_BUNDLE_SIZE = 16 * 1024 * 1024
LATEST_FIRMWARE_URL = (
    "https://raw.githubusercontent.com/mackotuned/"
    "MackoDash-Flash-Tools/MackoDash/MackoDash-Firmware.zip"
)


@dataclass(frozen=True)
class ImageDefinition:
    name: str
    filename: str
    offset: int
    max_size: int
    exact_size: bool = False


IMAGE_DEFINITIONS = (
    ImageDefinition("bootloader", "bootloader.bin", 0x2000, 0x6000),
    ImageDefinition("partition_table", "partition-table.bin", 0x8000, 0xC00, True),
    ImageDefinition("ota_data", "ota_data_initial.bin", 0xF000, 0x2000, True),
    ImageDefinition("application", "mackodash.bin", 0x20000, 0x600000),
    ImageDefinition("storage", "storage.bin", 0xC20000, 0x3E0000, True),
)
APP_DEFINITION = next(image for image in IMAGE_DEFINITIONS if image.name == "application")


class FirmwareValidationError(ValueError):
    pass


@dataclass(frozen=True)
class BundleImage:
    definition: ImageDefinition
    path: Path
    archive_name: str
    size: int
    sha256: str


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
    images: tuple[BundleImage, ...]


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


def _validate_application(data: bytes) -> tuple[str, str, str, str, str]:
    size = len(data)
    if size < 64 * 1024:
        raise FirmwareValidationError("The application image is too small to be MackoDash firmware.")
    if size > APP_DEFINITION.max_size:
        raise FirmwareValidationError(
            f"Application is {size:,} bytes, larger than its {APP_DEFINITION.max_size:,}-byte partition."
        )

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
    return project_name, version, compile_date, compile_time, idf_version


def create_firmware_bundle(build_dir: str | Path, output_path: str | Path) -> Path:
    build_root = Path(build_dir).expanduser().resolve()
    output = Path(output_path).expanduser().resolve()
    source_paths = {
        "bootloader": build_root / "bootloader" / "bootloader.bin",
        "partition_table": build_root / "partition_table" / "partition-table.bin",
        "ota_data": build_root / "ota_data_initial.bin",
        "application": build_root / "mackodash.bin",
        "storage": build_root / "storage.bin",
    }
    missing = [str(path) for path in source_paths.values() if not path.is_file()]
    if missing:
        raise FirmwareValidationError("Build the complete firmware first; missing: " + ", ".join(missing))

    files: list[dict[str, object]] = []
    payloads: dict[str, bytes] = {}
    for definition in IMAGE_DEFINITIONS:
        data = source_paths[definition.name].read_bytes()
        payloads[definition.filename] = data
        files.append({
            "name": definition.name,
            "file": definition.filename,
            "offset": definition.offset,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })

    project_name, version, compile_date, compile_time, idf_version = _validate_application(
        payloads[APP_DEFINITION.filename]
    )
    manifest = {
        "schema": BUNDLE_SCHEMA,
        "project": project_name,
        "chip": "esp32p4",
        "version": version,
        "compile_date": compile_date,
        "compile_time": compile_time,
        "idf_version": idf_version,
        "files": files,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        def write_entry(filename: str, data: str | bytes) -> None:
            info = zipfile.ZipInfo(filename, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o600 << 16
            archive.writestr(info, data, compresslevel=9)

        write_entry(BUNDLE_MANIFEST, json.dumps(manifest, indent=2) + "\n")
        for definition in IMAGE_DEFINITIONS:
            write_entry(definition.filename, payloads[definition.filename])
    validate_firmware(output)
    return output


def download_latest_firmware(destination: str | Path) -> Path:
    output = Path(destination).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    request = Request(LATEST_FIRMWARE_URL, headers={"User-Agent": "MackoDashUtility"})
    temporary_path: Path | None = None
    try:
        with urlopen(request, timeout=30) as response:
            content_length = response.headers.get("Content-Length")
            if content_length and int(content_length) > MAX_BUNDLE_SIZE:
                raise FirmwareValidationError("The downloaded firmware ZIP is unexpectedly large.")
            with tempfile.NamedTemporaryFile(
                mode="wb", suffix=".zip", prefix="mackodash-download-",
                dir=output.parent, delete=False,
            ) as temporary:
                temporary_path = Path(temporary.name)
                total_size = 0
                while chunk := response.read(64 * 1024):
                    total_size += len(chunk)
                    if total_size > MAX_BUNDLE_SIZE:
                        raise FirmwareValidationError("The downloaded firmware ZIP is unexpectedly large.")
                    temporary.write(chunk)
        if not temporary_path or temporary_path.stat().st_size == 0:
            raise FirmwareValidationError("GitHub returned an empty firmware download.")
        validate_firmware(temporary_path)
        temporary_path.replace(output)
        temporary_path = None
        return output
    finally:
        if temporary_path:
            temporary_path.unlink(missing_ok=True)


def _validate_manifest(manifest: object) -> dict[str, dict[str, object]]:
    if not isinstance(manifest, dict) or manifest.get("schema") != BUNDLE_SCHEMA:
        raise FirmwareValidationError(f"Unsupported firmware bundle manifest; expected schema {BUNDLE_SCHEMA}.")
    if manifest.get("project") != EXPECTED_PROJECT or manifest.get("chip") != "esp32p4":
        raise FirmwareValidationError("This firmware bundle is not for MackoDash ESP32-P4.")
    files = manifest.get("files")
    if not isinstance(files, list) or len(files) != len(IMAGE_DEFINITIONS):
        raise FirmwareValidationError("Firmware bundle manifest does not contain the required five images.")

    by_name: dict[str, dict[str, object]] = {}
    for item in files:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str):
            raise FirmwareValidationError("Firmware bundle manifest contains an invalid image entry.")
        name = item["name"]
        if name in by_name:
            raise FirmwareValidationError(f"Firmware bundle manifest repeats image '{name}'.")
        by_name[name] = item
    if set(by_name) != {definition.name for definition in IMAGE_DEFINITIONS}:
        raise FirmwareValidationError("Firmware bundle manifest image names do not match the required layout.")
    return by_name


def validate_firmware(path: str | Path) -> FirmwareInfo:
    bundle_path = Path(path).expanduser().resolve()
    if bundle_path.suffix.lower() != ".zip":
        raise FirmwareValidationError("Choose the official MackoDash firmware .zip file.")
    if not bundle_path.is_file():
        raise FirmwareValidationError("The selected firmware ZIP does not exist.")
    if bundle_path.stat().st_size > MAX_BUNDLE_SIZE:
        raise FirmwareValidationError("The selected firmware ZIP is unexpectedly large.")

    try:
        archive = zipfile.ZipFile(bundle_path)
    except zipfile.BadZipFile as error:
        raise FirmwareValidationError(f"Invalid or damaged firmware ZIP: {error}") from error

    with archive:
        entries = [info for info in archive.infolist() if not info.is_dir()]
        names = [info.filename for info in entries]
        expected_names = {BUNDLE_MANIFEST, *(definition.filename for definition in IMAGE_DEFINITIONS)}
        prefixes = {name.removesuffix(BUNDLE_MANIFEST) for name in names if name.endswith(BUNDLE_MANIFEST)}
        prefix = prefixes.pop() if len(prefixes) == 1 else ""
        expected_archive_names = {f"{prefix}{name}" for name in expected_names}
        if (
            len(names) != len(set(names))
            or set(names) != expected_archive_names
            or (prefix and (not prefix.endswith("/") or "/" in prefix.rstrip("/")))
        ):
            raise FirmwareValidationError(
                "Firmware ZIP must contain only the manifest and five firmware images at its root "
                "or inside one folder."
            )
        if any(info.file_size > MAX_BUNDLE_SIZE for info in entries):
            raise FirmwareValidationError("Firmware ZIP contains an invalid or oversized entry.")
        try:
            manifest = json.loads(archive.read(f"{prefix}{BUNDLE_MANIFEST}").decode("utf-8"))
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise FirmwareValidationError(f"Firmware bundle manifest is missing or invalid: {error}") from error
        manifest_files = _validate_manifest(manifest)

        total_size = 0
        image_data: dict[str, bytes] = {}
        bundle_images: list[BundleImage] = []
        for definition in IMAGE_DEFINITIONS:
            item = manifest_files[definition.name]
            if item.get("file") != definition.filename or item.get("offset") != definition.offset:
                raise FirmwareValidationError(f"Invalid file or flash offset for '{definition.name}'.")
            archive_name = f"{prefix}{definition.filename}"
            data = archive.read(archive_name)
            size = len(data)
            total_size += size
            if size == 0 or size > definition.max_size or (definition.exact_size and size != definition.max_size):
                qualifier = "exactly" if definition.exact_size else "no more than"
                raise FirmwareValidationError(
                    f"{definition.filename} must be {qualifier} {definition.max_size:,} bytes."
                )
            digest = hashlib.sha256(data).hexdigest()
            if item.get("size") != size or item.get("sha256") != digest:
                raise FirmwareValidationError(f"Checksum or size mismatch for '{definition.filename}'.")
            image_data[definition.name] = data
            bundle_images.append(BundleImage(definition, bundle_path, archive_name, size, digest))

    project_name, version, compile_date, compile_time, idf_version = _validate_application(
        image_data["application"]
    )

    return FirmwareInfo(
        path=bundle_path,
        size=total_size,
        sha256=hashlib.sha256(bundle_path.read_bytes()).hexdigest(),
        project_name=project_name,
        version=version or "unknown",
        compile_date=compile_date or "unknown",
        compile_time=compile_time or "unknown",
        idf_version=idf_version or "unknown",
        images=tuple(bundle_images),
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

    firmware = validate_firmware(firmware.path)
    writer = _LogWriter(log)
    with tempfile.TemporaryDirectory(prefix="mackodash-firmware-") as directory:
        extract_root = Path(directory)
        with zipfile.ZipFile(firmware.path) as archive:
            for image in firmware.images:
                (extract_root / image.definition.filename).write_bytes(archive.read(image.archive_name))

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
        ]
        for image in firmware.images:
            arguments.extend((hex(image.definition.offset), str(extract_root / image.definition.filename)))

        with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
            try:
                esptool.main(arguments)
            finally:
                writer.flush()