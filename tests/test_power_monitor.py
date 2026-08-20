import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class PowerMonitorTests(unittest.TestCase):
    def test_detects_early_suspend_and_shutdown_transitions(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "power-monitor-test"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "activity-probe"),
                    str(ROOT / "tests" / "power_monitor_harness.c"),
                    str(ROOT / "activity-probe" / "power_monitor.c"),
                    "-o",
                    str(output),
                ],
                check=True,
            )
            subprocess.run([str(output)], check=True)


if __name__ == "__main__":
    unittest.main()
