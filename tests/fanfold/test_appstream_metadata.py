"""Check that the shipped metainfo maps to the native Debian package in AppStream."""

import pathlib
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


METADATA = (
    pathlib.Path(__file__).resolve().parents[2]
    / "src/fanfold/packaging/io.github.preyevates.FanFold.metainfo.xml"
)


class AppStreamMetadataTest(unittest.TestCase):
    def test_catalog_component_is_associated_with_fanfold_package(self):
        with tempfile.TemporaryDirectory() as directory:
            catalog = pathlib.Path(directory) / "component.yml"
            subprocess.run(
                [sys.argv[1], "convert", str(METADATA), str(catalog)],
                check=True,
                capture_output=True,
                text=True,
            )
            text = catalog.read_text()
            self.assertIn("ID: io.github.preyevates.FanFold\n", text)
            self.assertIn("Package: fanfold\n", text)

    def test_listing_has_real_screenshots_and_raster_icon(self):
        root = ET.parse(METADATA).getroot()
        screenshots = root.findall("screenshots/screenshot")
        self.assertGreaterEqual(len(screenshots), 2)
        self.assertEqual(screenshots[0].attrib.get("type"), "default")
        for screenshot in screenshots:
            image = screenshot.find("image")
            if image is None or image.text is None:
                self.fail("screenshot missing image URL")
            self.assertTrue(image.text.startswith("https://raw.githubusercontent.com/preyevates/fan-fold/main/docs/fan-fold/media/"))
            self.assertTrue(screenshot.findtext("caption"))
            media = METADATA.parents[3] / "docs/fan-fold/media" / image.text.rsplit("/", 1)[-1]
            self.assertTrue(media.is_file(), f"screenshot missing: {media}")
        for size in (128, 256):
            icon = METADATA.parent / "icons" / f"{size}x{size}" / "apps/io.github.preyevates.FanFold.png"
            self.assertTrue(icon.is_file(), f"icon missing: {icon}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_appstream_metadata.py /path/to/appstreamcli")
    unittest.main(argv=[sys.argv[0]])
