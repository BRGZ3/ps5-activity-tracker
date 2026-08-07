import shutil
import socket
import subprocess
import tempfile
import unittest
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DashboardHttpTests(unittest.TestCase):
    def test_serves_dashboard_and_summary(self):
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler is unavailable")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary)
            dashboard = data / "dashboard"
            dashboard.mkdir()
            (dashboard / "index.html").write_text("PLAYLOG", encoding="utf-8")
            (data / "summary.json").write_text('{"ok":true}', encoding="utf-8")
            (data / "backups.json").write_text(
                '{"backups":[]}', encoding="utf-8"
            )
            binary = data / "http-test"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-D_POSIX_C_SOURCE=200809L",
                    f"-DDASHBOARD_HTTP_PORT={port}",
                    f'-DDASHBOARD_DIR="{dashboard}"',
                    f'-DTRACKER_DATA_DIR="{data}"',
                    "-I",
                    str(ROOT / "activity-probe"),
                    str(ROOT / "tests/http_server_harness.c"),
                    str(ROOT / "activity-probe/http_server.c"),
                    "-pthread",
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            process = subprocess.Popen(
                [str(binary)], stdout=subprocess.PIPE, text=True
            )
            try:
                self.assertEqual(process.stdout.readline().strip(), "ready")
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/", timeout=2
                ) as response:
                    self.assertEqual(response.read(), b"PLAYLOG")
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/summary.json", timeout=2
                ) as response:
                    self.assertEqual(response.read(), b'{"ok":true}')
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/access", timeout=2
                ) as response:
                    self.assertEqual(
                        response.read(), b'{"ok":true,"read_only":false}\n'
                    )
                request = urllib.request.Request(
                    f"http://127.0.0.1:{port}/api/completed"
                    "?title_id=PPSA02177&completed=1",
                    method="POST",
                )
                with urllib.request.urlopen(request, timeout=2) as response:
                    self.assertEqual(response.read(), b'{"ok":true}\n')
                request = urllib.request.Request(
                    f"http://127.0.0.1:{port}/api/backups/create",
                    method="POST",
                )
                with urllib.request.urlopen(request, timeout=2) as response:
                    self.assertIn(b'"ok":true', response.read())
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/backups.json", timeout=2
                ) as response:
                    self.assertEqual(response.read(), b'{"backups":[]}')
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/update/status", timeout=2
                ) as response:
                    self.assertIn(b'"available":false', response.read())
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/setup/status", timeout=2
                ) as response:
                    self.assertIn(b'"installed":false', response.read())
                for mode in ("etahen", "autoloader"):
                    request = urllib.request.Request(
                        f"http://127.0.0.1:{port}/api/setup/install"
                        f"?mode={mode}",
                        method="POST",
                    )
                    with urllib.request.urlopen(
                        request, timeout=2
                    ) as response:
                        body = response.read()
                        self.assertIn(b'"ok":true', body)
                        self.assertIn(mode.encode(), body)
                request = urllib.request.Request(
                    f"http://127.0.0.1:{port}/api/update/apply",
                    method="POST",
                )
                with urllib.request.urlopen(request, timeout=2) as response:
                    self.assertIn(b'"restart_required":true', response.read())
                for action in ("restore", "delete"):
                    request = urllib.request.Request(
                        f"http://127.0.0.1:{port}/api/backups/{action}"
                        "?id=backup-20260730-120000",
                        method="POST",
                    )
                    with urllib.request.urlopen(
                        request, timeout=2
                    ) as response:
                        self.assertEqual(response.read(), b'{"ok":true}\n')
                request = urllib.request.Request(
                    f"http://127.0.0.1:{port}/api/config"
                    "?timezone_offset_minutes=180"
                    "&timezone_name=Europe%2FMoscow"
                    "&firmware=4.50",
                    method="POST",
                )
                with urllib.request.urlopen(request, timeout=2) as response:
                    self.assertEqual(response.read(), b'{"ok":true}\n')
            finally:
                process.terminate()
                process.wait(timeout=2)
                process.stdout.close()


if __name__ == "__main__":
    unittest.main()
