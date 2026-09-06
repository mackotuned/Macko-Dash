from __future__ import annotations

import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from theme_preview import (ThemePackage, analyze_theme, display_text, image_runtime_rect,
                           load_theme_package, raw_image_png, resolve_binding, rotated_polygon,
                           runtime_font_size, sample_path)


class ThemePreviewTests(unittest.TestCase):
    def _theme(self, objects: list[dict]) -> ThemePackage:
        return ThemePackage(Path("test.mdtheme.zip"), "Test", 1024, 600, {"background": "#000000", "objects": objects}, {})

    def test_ready_theme_has_no_issues(self) -> None:
        theme = self._theme([
            {"type": "label", "name": "dash_rpm_value", "x": 20, "y": 20, "width": 180, "height": 60, "font_size": 28},
            {"type": "button", "name": "dash_settings_button", "x": 900, "y": 520, "width": 100, "height": 60},
            {"type": "button", "name": "dash_record_button", "x": 820, "y": 520, "width": 70, "height": 60},
        ])
        self.assertEqual(analyze_theme(theme), [])

    def test_reports_unsupported_clipped_missing_and_text_fit(self) -> None:
        issues = analyze_theme(self._theme([
            {"type": "slider", "name": "dash_slider", "x": 0, "y": 0, "width": 50, "height": 20},
            {"type": "label", "name": "dash_odo_value", "x": 990, "y": 580, "width": 30, "height": 10, "font_size": 28},
        ]))
        messages = "\n".join(issue.message for issue in issues)
        self.assertIn("unsupported control", messages)
        self.assertIn("longest value", messages)
        self.assertIn("dash_settings_button", messages)
        self.assertIn("dash_record_button", messages)

    def test_longest_simulated_value_uses_live_binding(self) -> None:
        self.assertEqual(display_text({"name": "ui_dash_odo_value", "text": "0"}, "longest"), "999999.9")

    def test_supported_indicator_is_not_reported_as_unresolved(self) -> None:
        theme = self._theme([
            {"type": "indicator", "name": "dash_cel_indicator", "x": 20, "y": 20, "width": 80, "height": 30},
            {"type": "button", "name": "dash_settings_button", "x": 900, "y": 520, "width": 100, "height": 60},
            {"type": "button", "name": "dash_record_button", "x": 820, "y": 520, "width": 70, "height": 60},
        ])
        self.assertEqual(analyze_theme(theme), [])

    def test_binding_resolution_matches_firmware_typo_tolerance(self) -> None:
        self.assertEqual(resolve_binding("dash_rpm_vlaue"), "rpm")
        self.assertEqual(resolve_binding("ui_dash_record_button_2"), "record")
        self.assertEqual(resolve_binding("dash_sim_button"), "sim")
        self.assertEqual(resolve_binding("dash_rpm_path_gauge"), "rpm")

    def test_path_sampling_uses_calibrated_values(self) -> None:
        samples = sample_path([[0, 0], [10, 0], [100, 0]], 8, [0, 1000, 8000], 0, 8000)
        self.assertEqual(samples[0][0], 5)
        self.assertGreater(samples[1][0], 10)
        self.assertLess(samples[-1][0], 100)

    def test_loads_layout_and_referenced_asset(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.mdtheme.zip"
            with zipfile.ZipFile(path, "w") as package:
                package.writestr("manifest.json", json.dumps({"name": "Test", "resolution": [1024, 600], "layout": "layout.json"}))
                package.writestr("layout.json", json.dumps({"objects": [{"type": "image", "asset": "assets/a.raw"}]}))
                package.writestr("assets/a.raw", b"\x00\x00")
            theme = load_theme_package(path)
        self.assertEqual(theme.assets["assets/a.raw"], b"\x00\x00")

    def test_rgb565_decoder_produces_png(self) -> None:
        png = raw_image_png(b"\x00\xf8", 1, 1, "rgb565")
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))

    def test_centered_image_uses_rendered_dimensions_for_alignment(self) -> None:
        item = {
            "type": "image", "object_align": "center", "x": -269, "y": 12,
            "width": "content", "height": "content", "source_width": 473,
            "source_height": 503, "zoom": 110,
        }
        x, y, width, height = image_runtime_rect(item, 1024, 600)
        self.assertAlmostEqual(x, 141.37, places=1)
        self.assertAlmostEqual(y, 203.93, places=1)
        self.assertAlmostEqual(width, 203.24, places=1)
        self.assertAlmostEqual(height, 216.13, places=1)

    def test_font_size_matches_continuous_firmware_clamp(self) -> None:
        self.assertEqual(runtime_font_size(280, 1024, 600, 768), 150)
        self.assertEqual(runtime_font_size(30, 1024, 600, 768), 22)
        self.assertEqual(runtime_font_size(20, 1024, 600, 768), 15)

    def test_rotated_bar_keeps_its_center(self) -> None:
        points = rotated_polygon(100, 200, 140, 100, -90)
        xs = points[0::2]
        ys = points[1::2]
        self.assertAlmostEqual((min(xs) + max(xs)) / 2, 170)
        self.assertAlmostEqual((min(ys) + max(ys)) / 2, 250)
        self.assertAlmostEqual(max(xs) - min(xs), 100)
        self.assertAlmostEqual(max(ys) - min(ys), 140)

        indicator = rotated_polygon(100, 200, 70, 100, -90, (170, 250))
        indicator_xs = indicator[0::2]
        indicator_ys = indicator[1::2]
        self.assertAlmostEqual(min(indicator_xs), 120)
        self.assertAlmostEqual(max(indicator_xs), 220)
        self.assertAlmostEqual(min(indicator_ys), 250)
        self.assertAlmostEqual(max(indicator_ys), 320)


if __name__ == "__main__":
    unittest.main()