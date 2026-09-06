from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from theme_preview import load_theme_package, resolve_binding
from theme_studio_model import StudioProjectError, ThemeStudioProject


class ThemeStudioProjectTests(unittest.TestCase):
    def test_save_load_and_runtime_export_preserve_objects(self) -> None:
        project = ThemeStudioProject(theme_id="customer.track", name="Track Dash")
        rpm = project.add_object("label", 24, 30)
        rpm.update({"name": "dash_rpm_value", "width": 220, "text": "0"})

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project.save(root / "track.mdstudio.json")
            loaded = ThemeStudioProject.load(root / "track.mdstudio.json")
            loaded.export(root / "track.mdtheme.zip")
            package = load_theme_package(root / "track.mdtheme.zip")

        self.assertEqual(package.name, "Track Dash")
        self.assertEqual(package.layout["objects"], loaded.objects)
        self.assertEqual(resolve_binding(package.layout["objects"][0]["name"]), "rpm")

    def test_constrains_objects_to_dashboard_canvas(self) -> None:
        project = ThemeStudioProject()
        item = project.add_object("bar", 1000, 590)
        self.assertEqual((item["x"], item["y"]), (764, 572))

    def test_rejects_duplicate_names_and_invalid_ids(self) -> None:
        project = ThemeStudioProject(theme_id="Not Valid")
        project.add_object("label")
        with self.assertRaisesRegex(StudioProjectError, "Theme ID"):
            project.validate()
        project.theme_id = "customer.valid"
        project.add_object("bar")["name"] = project.objects[0]["name"]
        with self.assertRaisesRegex(StudioProjectError, "unique name"):
            project.validate()

    def test_snapshot_allows_in_progress_identity_edit(self) -> None:
        project = ThemeStudioProject(theme_id="")
        self.assertEqual(project.snapshot()["theme_id"], "")
        with self.assertRaisesRegex(StudioProjectError, "Theme ID"):
            project.document()

    def test_layer_order_snap_and_templates(self) -> None:
        project = ThemeStudioProject.from_template("Performance")
        self.assertEqual(len(project.objects), 9)
        self.assertEqual(project.objects[-1]["name"], "dash_settings_button")
        self.assertEqual(project.move_layer(0, 1), 1)
        self.assertEqual(project.objects[1]["name"], "dash_rpm_bar")
        self.assertEqual(project.snap(43, 10), 40)

    def test_embedded_image_is_written_to_runtime_package(self) -> None:
        project = ThemeStudioProject(theme_id="customer.image", name="Image Theme")
        project.add_image("logo", 2, 1, b"\x00\xf8\xff\xe0\x07\x80")
        with tempfile.TemporaryDirectory() as directory:
            path = project.export(Path(directory) / "image.mdtheme.zip")
            package = load_theme_package(path)
        self.assertEqual(package.assets["assets/logo.rgb565a8"], b"\x00\xf8\xff\xe0\x07\x80")
        self.assertNotIn("asset_data", package.layout["objects"][0])

    def test_arc_geometry_and_bar_image_export_to_runtime_package(self) -> None:
        project = ThemeStudioProject(theme_id="customer.details", name="Detailed Theme")
        arc = project.add_object("arc")
        arc.update({"rotation": 225, "sweep": 180})
        bar = project.add_object("bar")
        pixels = b"\x00\xf8\xff\xe0\x07\x80"
        project.set_bar_image(bar, "RPM Texture", 2, 1, pixels)
        with tempfile.TemporaryDirectory() as directory:
            path = project.export(Path(directory) / "details.mdtheme.zip")
            package = load_theme_package(path)
        self.assertEqual(package.layout["objects"][0]["rotation"], 225)
        self.assertEqual(package.layout["objects"][0]["sweep"], 180)
        self.assertEqual(package.layout["objects"][1]["indicator_asset"], "assets/rpm_texture.rgb565a8")
        self.assertEqual(package.assets["assets/rpm_texture.rgb565a8"], pixels)
        self.assertNotIn("indicator_asset_data", package.layout["objects"][1])

    def test_bar_and_arc_preview_values_stay_in_studio_project(self) -> None:
        project = ThemeStudioProject(theme_id="customer.preview", name="Preview Values")
        bar = project.add_object("bar")
        arc = project.add_object("arc")
        bar["preview_value"] = 42.5
        arc["preview_value"] = 75
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project.save(root / "preview.mdstudio.json")
            loaded = ThemeStudioProject.load(root / "preview.mdstudio.json")
            loaded.export(root / "preview.mdtheme.zip")
            package = load_theme_package(root / "preview.mdtheme.zip")
        self.assertEqual(loaded.objects[0]["preview_value"], 42.5)
        self.assertEqual(loaded.objects[1]["preview_value"], 75)
        self.assertNotIn("preview_value", package.layout["objects"][0])
        self.assertNotIn("preview_value", package.layout["objects"][1])

    def test_path_gauge_preserves_geometry_and_strips_preview_value(self) -> None:
        project = ThemeStudioProject(theme_id="customer.path", name="Path Gauge")
        gauge = project.add_object("path_gauge", 20, 30)
        gauge.update({"points": [[0, 170], [120, 30], [700, 10]],
                  "point_values": [0, 1000, 8000], "preview_value": 6750})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project.save(root / "path.mdstudio.json")
            loaded = ThemeStudioProject.load(root / "path.mdstudio.json")
            loaded.export(root / "path.mdtheme.zip")
            package = load_theme_package(root / "path.mdtheme.zip")
        self.assertEqual(loaded.objects[0]["points"], [[0, 170], [120, 30], [700, 10]])
        self.assertEqual(package.layout["objects"][0]["points"], [[0, 170], [120, 30], [700, 10]])
        self.assertEqual(package.layout["objects"][0]["point_values"], [0, 1000, 8000])
        self.assertNotIn("preview_value", package.layout["objects"][0])

    def test_path_gauge_rejects_out_of_bounds_points(self) -> None:
        project = ThemeStudioProject()
        gauge = project.add_object("path_gauge")
        gauge["points"] = [[0, 0], [gauge["width"] + 1, 20]]
        with self.assertRaisesRegex(StudioProjectError, "inside the gauge bounds"):
            project.validate()

    def test_path_gauge_rejects_invalid_calibration_values(self) -> None:
        project = ThemeStudioProject()
        gauge = project.add_object("path_gauge")
        gauge["point_values"] = [0, 1000, 1000, 6000, 8000]
        with self.assertRaisesRegex(StudioProjectError, "must increase"):
            project.validate()

    def test_bar_fill_assets_are_unique_and_share_image_limit(self) -> None:
        project = ThemeStudioProject()
        pixels = b"\x00\xf8\xff"
        first = project.add_object("bar")
        second = project.add_object("bar")
        project.set_bar_image(first, "Fill", 1, 1, pixels)
        project.set_bar_image(second, "Fill", 1, 1, pixels)
        self.assertEqual(first["indicator_asset"], "assets/fill.rgb565a8")
        self.assertEqual(second["indicator_asset"], "assets/fill_2.rgb565a8")
        for index in range(10):
            project.add_image(f"image_{index}", 1, 1, pixels)
        with self.assertRaisesRegex(StudioProjectError, "up to 12 images"):
            project.add_image("too_many", 1, 1, pixels)

    def test_needle_and_analog_tach_preserve_customization(self) -> None:
        project = ThemeStudioProject(theme_id="customer.gauges", name="Custom Gauges")
        needle = project.add_object("needle")
        needle.update({"name": "dash_rpm_bar", "rotation": 210, "sweep": 240,
                       "needle_color": "#00FF88", "needle_width": 8})
        tach = project.add_object("analog_tach")
        tach.update({"name": "dash_rpm_bar_2", "max": 11000, "tick_count": 23,
                     "major_tick_every": 2, "arc_width": 12, "show_value": False})
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project.save(root / "gauges.mdstudio.json")
            loaded = ThemeStudioProject.load(root / "gauges.mdstudio.json")
            loaded.export(root / "gauges.mdtheme.zip")
            package = load_theme_package(root / "gauges.mdtheme.zip")
        self.assertEqual(package.layout["objects"][0]["needle_color"], "#00FF88")
        self.assertEqual(package.layout["objects"][0]["needle_width"], 8)
        self.assertEqual(package.layout["objects"][1]["tick_count"], 23)
        self.assertFalse(package.layout["objects"][1]["show_value"])

    def test_speedometer_and_transparency_preserve_customization(self) -> None:
        project = ThemeStudioProject(theme_id="customer.speedo", name="Speedometer")
        project.background_opa = 0
        speedo = project.add_object("analog_speedo")
        speedo.update({"name": "dash_speed_bar", "max": 220, "needle_opa": 0,
                       "tick_opa": 128, "background_opa": 200, "border_opa": 0})
        label = project.add_object("label")
        label["color_opa"] = 0
        with tempfile.TemporaryDirectory() as directory:
            path = project.export(Path(directory) / "speedo.mdtheme.zip")
            package = load_theme_package(path)
        self.assertEqual(package.layout["objects"][0]["type"], "analog_speedo")
        self.assertEqual(package.layout["background_opa"], 0)
        self.assertEqual(package.layout["objects"][0]["max"], 220)
        self.assertEqual(package.layout["objects"][0]["needle_opa"], 0)
        self.assertEqual(package.layout["objects"][1]["color_opa"], 0)


if __name__ == "__main__":
    unittest.main()