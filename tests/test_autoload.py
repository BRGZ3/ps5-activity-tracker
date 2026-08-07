import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AutoloadTests(unittest.TestCase):
    def test_preserves_file_and_inserts_pause_before_single_elf(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            binary = directory / "autoload-test"
            autoload = directory / "autoload.txt"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-D_POSIX_C_SOURCE=200809L",
                    "-I",
                    str(ROOT / "activity-probe"),
                    str(ROOT / "tests/autoload_harness.c"),
                    str(ROOT / "activity-probe/offline_update.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            autoload.write_text(
                "first.elf\nPlaylog.elf\nsecond.elf\nPlaylog.elf\n",
                encoding="utf-8",
            )
            subprocess.run(
                [str(binary), str(autoload), "Playlog.elf"], check=True
            )
            self.assertEqual(
                autoload.read_text(encoding="utf-8"),
                "first.elf\n!5000\nPlaylog.elf\nsecond.elf\n",
            )
            autoload.write_text(
                "legacy.elf\nps5-activity-tracker.elf\nPlaylog.elf\n",
                encoding="utf-8",
            )
            subprocess.run(
                [str(binary), "remove", str(autoload),
                 "ps5-activity-tracker.elf"], check=True
            )
            self.assertEqual(
                autoload.read_text(encoding="utf-8"),
                "legacy.elf\nPlaylog.elf\n",
            )


if __name__ == "__main__":
    unittest.main()
