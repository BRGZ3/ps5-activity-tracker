import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ConsoleTrackerTests(unittest.TestCase):
    def _run_harness(self, temporary, source):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler is unavailable")
        output = Path(temporary) / "tracker-test"
        (Path(temporary) / "config.json").write_text(
            '{"timezone_offset_minutes":180,'
            '"timezone_name":"Europe/Moscow"}',
            encoding="utf-8",
        )
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-D_POSIX_C_SOURCE=200809L",
                f'-DTRACKER_DATA_DIR="{temporary}"',
                "-I",
                str(ROOT / "activity-probe"),
                str(ROOT / "tests" / source),
                str(ROOT / "activity-probe/tracker.c"),
                "-o",
                str(output),
            ],
            check=True,
        )
        subprocess.run([str(output)], check=True)
        return json.loads(
            (Path(temporary) / "summary.json").read_text(encoding="utf-8")
        )

    def test_session_is_split_across_moscow_midnight(self):
        with tempfile.TemporaryDirectory() as temporary:
            summary = self._run_harness(
                temporary, "tracker_midnight_harness.c"
            )
        self.assertEqual(summary["days"][-2]["date"], "2026-07-28")
        self.assertEqual(summary["days"][-1]["date"], "2026-07-29")
        self.assertAlmostEqual(summary["days"][-2]["active_seconds"], 60)
        self.assertAlmostEqual(summary["days"][-1]["active_seconds"], 60)
        self.assertAlmostEqual(
            summary["periods"]["today"]["active_seconds"], 60
        )
        self.assertAlmostEqual(
            summary["games"][0]["today_active_seconds"], 60
        )

    def test_corrupt_main_state_recovers_previous_generation(self):
        with tempfile.TemporaryDirectory() as temporary:
            self._run_harness(temporary, "tracker_harness.c")
            state_path = Path(temporary) / "tracker-state.bin"
            damaged = bytearray(state_path.read_bytes())
            damaged[-32] ^= 0x5A
            state_path.write_bytes(damaged)
            subprocess.run(
                [str(Path(temporary) / "tracker-test")], check=True
            )
            summary = json.loads(
                (Path(temporary) / "summary.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(summary["health"]["diagnostics"], [])
            self.assertTrue(
                list(Path(temporary).glob(
                    "tracker-state.main.corrupt-*.bin"
                ))
            )
            self.assertGreater(summary["totals"]["games"], 0)

    def test_legacy_raw_state_is_migrated_to_crc_container(self):
        with tempfile.TemporaryDirectory() as temporary:
            self._run_harness(temporary, "tracker_harness.c")
            state_path = Path(temporary) / "tracker-state.bin"
            blob = state_path.read_bytes()
            payload_size = int.from_bytes(blob[8:12], "little")
            self.assertGreater(len(blob), payload_size)
            state_path.write_bytes(blob[-payload_size:])
            previous = Path(temporary) / "tracker-state.prev.bin"
            if previous.exists():
                previous.unlink()
            subprocess.run(
                [str(Path(temporary) / "tracker-test")], check=True
            )
            migrated = state_path.read_bytes()
            self.assertGreater(len(migrated), payload_size)
            self.assertEqual(
                int.from_bytes(migrated[8:12], "little"), payload_size
            )

    def test_c_aggregator_tracks_active_and_home_time(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            (Path(temporary) / "config.json").write_text(
                '{"timezone_offset_minutes":180,'
                '"timezone_name":"Europe/Moscow"}',
                encoding="utf-8",
            )
            output = Path(temporary) / "tracker-test"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-D_POSIX_C_SOURCE=200809L",
                    f'-DTRACKER_DATA_DIR="{temporary}"',
                    "-I",
                    str(ROOT / "activity-probe"),
                    str(ROOT / "tests/tracker_harness.c"),
                    str(ROOT / "activity-probe/tracker.c"),
                    "-o",
                    str(output),
                ],
                check=True,
            )
            subprocess.run([str(output)], check=True)
            raw_summary = (Path(temporary) / "summary.json").read_text(
                encoding="utf-8"
            )
            summary = json.loads(raw_summary)

        self.assertGreater(raw_summary.count("\n"), 8)
        self.assertEqual(summary["totals"]["games"], 1)
        self.assertEqual(summary["totals"]["sessions"], 1)
        self.assertAlmostEqual(summary["totals"]["active_seconds"], 70)
        self.assertAlmostEqual(summary["totals"]["paused_seconds"], 20)
        self.assertEqual(summary["games"][0]["name"], "Test Game")
        self.assertEqual(summary["games"][0]["platform"], "PS5")
        self.assertAlmostEqual(
            summary["games"][0]["today_active_seconds"], 70
        )
        self.assertEqual(summary["games"][0]["today_session_count"], 1)
        self.assertIsNotNone(summary["games"][0]["completed_at"])
        self.assertEqual(summary["totals"]["completed_games"], 1)
        self.assertEqual(summary["health"]["diagnostics"], [])
        self.assertEqual(summary["timezone"], "Europe/Moscow")
        self.assertEqual(summary["timezone_offset_minutes"], 180)
        self.assertEqual(summary["timezone_source"], "config")
        self.assertEqual(summary["system"]["firmware"], "4.50")
        self.assertTrue(summary["generated_at_local"].endswith("+03:00"))
        self.assertTrue(
            summary["sessions"][0]["started_at_local"].endswith("+03:00")
        )
        self.assertEqual(summary["sessions"][0]["is_open"], 0)
        self.assertEqual(summary["health"]["current_activity"], None)

    def test_backup_is_verified_and_restore_creates_safety_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            summary = self._run_harness(
                temporary, "tracker_backup_harness.c"
            )
            index = json.loads(
                (Path(temporary) / "backups.json").read_text(
                    encoding="utf-8"
                )
            )
            backups = index["backups"]
            self.assertEqual(summary["totals"]["games"], 1)
            self.assertEqual(summary["games"][0]["name"], "Backup Game")
            self.assertEqual(len(backups), 2)
            self.assertTrue(all(item["valid"] for item in backups))
            self.assertTrue(
                any(item["kind"] == "before_restore" for item in backups)
            )
            for item in backups:
                directory = Path(temporary) / "backups" / item["id"]
                self.assertTrue((directory / "backup.meta").exists())
                self.assertTrue((directory / "manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
