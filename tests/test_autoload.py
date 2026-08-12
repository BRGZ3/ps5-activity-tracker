import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AutoloadTests(unittest.TestCase):
    def build_harness(self, directory: Path) -> Path:
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler is unavailable")
        binary = directory / "autoload-test"
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
        return binary

    def test_appends_after_existing_payload_chain(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            binary = self.build_harness(directory)
            autoload = directory / "autoload.txt"
            autoload.write_text("first.elf\nsecond.elf", encoding="utf-8")

            subprocess.run(
                [str(binary), str(autoload), "Playlog.elf"], check=True
            )

            self.assertEqual(
                autoload.read_text(encoding="utf-8"),
                "first.elf\nsecond.elf\n!5000\nPlaylog.elf\n",
            )

    def test_existing_playlog_position_is_left_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            binary = self.build_harness(directory)
            autoload = directory / "autoload.txt"
            original = (
                "first.elf\n!2000\n"
                "Playlog.elf # intentional order\nsecond.elf\n"
            )
            autoload.write_text(original, encoding="utf-8")

            subprocess.run(
                [str(binary), str(autoload), "Playlog.elf"], check=True
            )

            self.assertEqual(autoload.read_text(encoding="utf-8"), original)

            original = "!5000\nPlaylog.elf\n"
            autoload.write_text(original, encoding="utf-8")
            result = subprocess.run(
                [str(binary), str(autoload), "Playlog.elf"], check=False
            )
            self.assertEqual(result.returncode, 1)
            self.assertEqual(autoload.read_text(encoding="utf-8"), original)

    def test_missing_empty_or_directive_only_requires_manual_setup(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            binary = self.build_harness(directory)
            autoload = directory / "autoload.txt"

            result = subprocess.run(
                [str(binary), str(autoload), "Playlog.elf"], check=False
            )
            self.assertEqual(result.returncode, 1)
            self.assertFalse(autoload.exists())

            samples = ("", "# keep fallback\n!5000\n@sync\n")
            for original in samples:
                autoload.write_text(original, encoding="utf-8")
                result = subprocess.run(
                    [str(binary), str(autoload), "Playlog.elf"], check=False
                )
                self.assertEqual(result.returncode, 1)
                self.assertEqual(
                    autoload.read_text(encoding="utf-8"), original
                )

    def test_remove_legacy_entry(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            binary = self.build_harness(directory)
            autoload = directory / "autoload.txt"
            autoload.write_text(
                "legacy.elf\nps5-activity-tracker.elf\nPlaylog.elf\n",
                encoding="utf-8",
            )

            subprocess.run(
                [
                    str(binary),
                    "remove",
                    str(autoload),
                    "ps5-activity-tracker.elf",
                ],
                check=True,
            )

            self.assertEqual(
                autoload.read_text(encoding="utf-8"),
                "legacy.elf\nPlaylog.elf\n",
            )

            original = "# exact bytes stay intact\r\n!5000\r\nPlaylog.elf"
            autoload.write_bytes(original.encode("utf-8"))
            subprocess.run(
                [
                    str(binary),
                    "remove",
                    str(autoload),
                    "ps5-activity-tracker.elf",
                ],
                check=True,
            )
            self.assertEqual(autoload.read_bytes(), original.encode("utf-8"))


if __name__ == "__main__":
    unittest.main()
