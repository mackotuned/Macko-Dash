from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from log_data import LogFormatError, find_logs, load_log, nearest_row_index, sampled_row_indices


HEADER = (
    "elapsed_ms,rpm,speed_mph,gear,coolant_f,intake_f,afr,timing_deg,map_psi,"
    "battery_v,tps_pct,oil_psi,oil_valid,duty_pct,duty_valid,knock_deg,knock_valid,"
    "cel,odometer_miles,fuel_pct\n"
)


class LogDataTests(unittest.TestCase):
    def _write_log(self, path: Path) -> None:
        path.write_text(
            HEADER
            + "0,1000,10.0,1,180,80,14.7,12,0,13.8,20,30,1,25,1,0.0,0,0,100.0,75\n"
            + "100,2000,20.0,2,190,85,12.5,18,5,14.0,50,40,0,55,1,1.5,1,1,100.1,74\n"
            + "200,1500,15.0,2,185,82,13.2,15,2,13.9,35,35,1,40,0,0.5,1,1,100.2,74\n",
            encoding="utf-8",
        )

    def test_loads_summary_and_validity_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "LOG0001.CSV"
            self._write_log(path)
            session = load_log(path)
        self.assertEqual(len(session.rows), 3)
        self.assertEqual(session.maximum("rpm"), 2000)
        self.assertEqual(session.minimum("oil_psi"), 30)
        self.assertEqual(session.maximum("duty_pct"), 55)
        self.assertIsNone(session.rows[1]["oil_psi"])
        self.assertIsNone(session.rows[2]["duty_pct"])
        self.assertAlmostEqual(session.duration_seconds, 0.2)
        self.assertAlmostEqual(session.distance_miles, 0.2)
        self.assertEqual(session.cel_events, 1)

    def test_rejects_non_mackodash_csv(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "other.csv"
            path.write_text("time,value\n0,1\n", encoding="utf-8")
            with self.assertRaisesRegex(LogFormatError, "not a MackoDash log"):
                load_log(path)

    def test_finds_logs_on_sd_layout(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            log_dir = Path(directory) / "MACKODASH" / "LOGS"
            log_dir.mkdir(parents=True)
            first = log_dir / "LOG0001.CSV"
            second = log_dir / "LOG0002.CSV"
            self._write_log(first)
            self._write_log(second)
            logs = find_logs(directory)
        self.assertEqual({path.name for path in logs}, {"LOG0001.CSV", "LOG0002.CSV"})

    def test_graph_helpers_preserve_endpoints_and_find_nearest_sample(self) -> None:
        indices = sampled_row_indices(1000, 100)
        self.assertEqual((indices[0], indices[-1], len(indices)), (0, 999, 100))
        rows = tuple({"elapsed_ms": value} for value in (0, 100, 200))
        self.assertEqual(nearest_row_index(rows, 149), 1)
        self.assertEqual(nearest_row_index(rows, 151), 2)


if __name__ == "__main__":
    unittest.main()