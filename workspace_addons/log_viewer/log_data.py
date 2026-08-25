from __future__ import annotations

import csv
from bisect import bisect_left
from dataclasses import dataclass
from pathlib import Path


REQUIRED_COLUMNS = (
    "elapsed_ms", "rpm", "speed_mph", "gear", "coolant_f", "intake_f", "afr",
    "timing_deg", "map_psi", "battery_v", "tps_pct", "oil_psi", "oil_valid",
    "duty_pct", "duty_valid", "knock_deg", "knock_valid", "cel", "odometer_miles",
    "fuel_pct",
)
INTEGER_COLUMNS = {"elapsed_ms", "rpm", "gear", "oil_valid", "duty_valid", "knock_valid", "cel"}
VALIDITY_COLUMNS = {"oil_psi": "oil_valid", "duty_pct": "duty_valid", "knock_deg": "knock_valid"}

SIGNALS = {
    "rpm": ("Engine Speed", "RPM"),
    "speed_mph": ("Road Speed", "mph"),
    "gear": ("Gear", ""),
    "coolant_f": ("Coolant", "F"),
    "intake_f": ("Intake Air", "F"),
    "afr": ("Air/Fuel Ratio", "AFR"),
    "timing_deg": ("Ignition Timing", "deg"),
    "map_psi": ("Boost / MAP", "psi"),
    "battery_v": ("Battery", "V"),
    "tps_pct": ("Throttle", "%"),
    "oil_psi": ("Oil Pressure", "psi"),
    "duty_pct": ("Injector Duty", "%"),
    "knock_deg": ("Knock Retard", "deg"),
    "cel": ("Check Engine Light", "state"),
    "odometer_miles": ("Odometer", "mi"),
    "fuel_pct": ("Fuel Level", "%"),
}


class LogFormatError(ValueError):
    pass


@dataclass(frozen=True)
class LogSession:
    path: Path
    rows: tuple[dict[str, float | int | None], ...]

    @property
    def duration_seconds(self) -> float:
        return float(self.rows[-1]["elapsed_ms"]) / 1000.0 if self.rows else 0.0

    @property
    def distance_miles(self) -> float:
        if len(self.rows) < 2:
            return 0.0
        return max(0.0, float(self.rows[-1]["odometer_miles"]) - float(self.rows[0]["odometer_miles"]))

    @property
    def sample_rate_hz(self) -> float:
        if len(self.rows) < 2 or self.duration_seconds <= 0:
            return 0.0
        return (len(self.rows) - 1) / self.duration_seconds

    def values(self, column: str) -> list[float]:
        return [float(row[column]) for row in self.rows if row.get(column) is not None]

    def minimum(self, column: str) -> float | None:
        values = self.values(column)
        return min(values) if values else None

    def maximum(self, column: str) -> float | None:
        values = self.values(column)
        return max(values) if values else None

    def average(self, column: str) -> float | None:
        values = self.values(column)
        return sum(values) / len(values) if values else None

    @property
    def cel_events(self) -> int:
        events = 0
        active = False
        for row in self.rows:
            next_active = bool(row["cel"])
            if next_active and not active:
                events += 1
            active = next_active
        return events


def load_log(path: str | Path) -> LogSession:
    source = Path(path).expanduser().resolve()
    if not source.is_file():
        raise LogFormatError("The selected log file does not exist.")
    try:
        with source.open("r", encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames is None:
                raise LogFormatError("The selected file has no CSV header.")
            missing = [column for column in REQUIRED_COLUMNS if column not in reader.fieldnames]
            if missing:
                raise LogFormatError("This is not a MackoDash log; missing: " + ", ".join(missing))
            rows: list[dict[str, float | int | None]] = []
            previous_elapsed = -1
            for line_number, raw in enumerate(reader, start=2):
                try:
                    row: dict[str, float | int | None] = {
                        column: int(raw[column]) if column in INTEGER_COLUMNS else float(raw[column])
                        for column in REQUIRED_COLUMNS
                    }
                except (KeyError, TypeError, ValueError) as error:
                    raise LogFormatError(f"Invalid value on CSV line {line_number}.") from error
                elapsed = int(row["elapsed_ms"])
                if elapsed < previous_elapsed:
                    raise LogFormatError(f"Time moves backward on CSV line {line_number}.")
                previous_elapsed = elapsed
                for value_column, valid_column in VALIDITY_COLUMNS.items():
                    if not row[valid_column]:
                        row[value_column] = None
                rows.append(row)
    except OSError as error:
        raise LogFormatError(f"Could not read the selected log: {error}") from error
    if not rows:
        raise LogFormatError("The selected log contains no data rows.")
    return LogSession(source, tuple(rows))


def find_logs(root: str | Path) -> list[Path]:
    selected = Path(root).expanduser().resolve()
    for directory in (selected / "MACKODASH" / "LOGS", selected / "LOGS", selected):
        if directory.is_dir():
            logs = sorted(directory.glob("LOG*.CSV"), key=lambda path: path.stat().st_mtime, reverse=True)
            if logs:
                return logs
    return []


def nearest_row_index(
    rows: tuple[dict[str, float | int | None], ...],
    elapsed_ms: float,
    elapsed_times: tuple[int, ...] | None = None,
) -> int:
    if not rows:
        raise ValueError("Cannot search an empty log.")
    times = elapsed_times if elapsed_times is not None else tuple(int(row["elapsed_ms"]) for row in rows)
    index = bisect_left(times, elapsed_ms)
    if index <= 0:
        return 0
    if index >= len(times):
        return len(times) - 1
    return index if times[index] - elapsed_ms < elapsed_ms - times[index - 1] else index - 1


def sampled_row_indices(row_count: int, maximum_points: int) -> list[int]:
    if row_count <= 0:
        return []
    if maximum_points < 2 or row_count <= maximum_points:
        return list(range(row_count))
    step = (row_count - 1) / (maximum_points - 1)
    indices = [round(index * step) for index in range(maximum_points)]
    indices[-1] = row_count - 1
    return indices