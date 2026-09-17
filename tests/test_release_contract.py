import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ReleaseContractTests(unittest.TestCase):
    def test_stable_release_metadata_and_no_compatibility_runtime(self):
        self.assertEqual((ROOT / "VERSION").read_text(encoding="utf-8").strip(), "1.3.0")

        makefile = (ROOT / "activity-probe/Makefile").read_text(encoding="utf-8")
        self.assertIn('PROBE_VERSION=\\"0.7.0\\"', makefile)
        self.assertIn('TRACKER_VERSION=\\"1.64\\"', makefile)
        self.assertIn('DASHBOARD_VERSION=\\"1.49.0\\"', makefile)
        self.assertNotIn("LITE_ELF", makefile)
        self.assertNotIn("LITE_INSTALLER", makefile)
        self.assertFalse((ROOT / "activity-probe/lite_main.c").exists())

        release_makefile = (ROOT / "release-build/Makefile").read_text(
            encoding="utf-8"
        )
        self.assertIn("ICON_SOURCE ?= playlog-logo.png", release_makefile)
        self.assertIn('cp "$(ICON_SOURCE)" app/sce_sys/base-icon.png', release_makefile)
        self.assertFalse((ROOT / "release-build/render_icon.js").exists())
        icon = ROOT / "release-build/playlog-logo.png"
        self.assertTrue(icon.is_file())
        self.assertEqual(icon.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
        self.assertIn("PACKAGE_VERSION ?= 1.70", release_makefile)
        self.assertIn("TRACKER_VERSION ?= 1.64", release_makefile)
        self.assertIn("DASHBOARD_VERSION ?= 1.49.0", release_makefile)

        param = (ROOT / "release-build/app/sce_sys/param.json").read_text(
            encoding="utf-8"
        )
        self.assertIn('"contentVersion": "01.070.000"', param)
        self.assertIn('"masterVersion": "01.70"', param)

        library = (ROOT / "dashboard/library.html").read_text(encoding="utf-8")
        self.assertEqual(library.count("var UI_VERSION="), 1)
        self.assertNotIn("playlogLibraryEnhanced", library)


if __name__ == "__main__":
    unittest.main()
