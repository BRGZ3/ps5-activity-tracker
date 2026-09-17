import json
import shutil
import sqlite3
import socket
import subprocess
import tempfile
import unittest
import urllib.error
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
            (dashboard / "library.html").write_text(
                "PLAYLOG-LIBRARY", encoding="utf-8"
            )
            (data / "summary.json").write_text('{"ok":true}', encoding="utf-8")
            (data / "backups.json").write_text(
                '{"backups":[]}', encoding="utf-8"
            )
            appmeta_root = data / "appmeta"
            appmeta = appmeta_root / "PPSA02177"
            appmeta.mkdir(parents=True)
            user_app_root = data / "user-app"
            (user_app_root / "PPSA02177").mkdir(parents=True)
            (user_app_root / "PPSA02177" / "eboot.bin").write_bytes(
                b"ELF"
            )
            stale_user_app = user_app_root / "PPSA88888"
            (stale_user_app / "sce_sys").mkdir(parents=True)
            (stale_user_app / "sce_sys" / "param.sfo").write_bytes(b"SFO")
            (appmeta_root / "PPSA99999").mkdir(parents=True)
            icon = b"\x89PNG\r\n\x1a\n" + (b"PLAYLOG-ICON" * 8192)
            database_icon = data / "database-icon.png"
            database_icon.write_bytes(icon)
            (appmeta / "icon0.png").write_bytes(icon)
            (appmeta / "param.json").write_text(
                '{"localizedParameters":{"en-US":{"titleName":"Library Game"}},'
                '"contentVersion":"01.023.000"}',
                encoding="utf-8",
            )
            app_db = data / "app.db"
            with sqlite3.connect(app_db) as database:
                database.execute(
                    "CREATE TABLE tbl_contentinfo ("
                    "titleId TEXT, titleName TEXT, metaDataPath TEXT, "
                    "icon0Info TEXT, categoryType INTEGER)"
                )
                database.execute(
                    "INSERT INTO tbl_contentinfo "
                    "(titleId, titleName, metaDataPath, icon0Info, categoryType) "
                    "VALUES (?, ?, ?, ?, ?)",
                    (
                        "PPSA02177",
                        "Database Game",
                        "/user/appmeta/PPSA02177",
                        "/missing/PPSA02177/icon0.png?ts=123",
                        0,
                    ),
                )
                database.execute(
                    "INSERT INTO tbl_contentinfo "
                    "(titleId, titleName, metaDataPath, icon0Info, categoryType) "
                    "VALUES (?, ?, ?, ?, ?)",
                    (
                        "PPSA02177",
                        "Database Game duplicate",
                        "/user/appmeta/PPSA02177",
                        f'  "file://{database_icon}?ts=123#cache"  ',
                        0,
                    ),
                )
                database.execute(
                    "INSERT INTO tbl_contentinfo "
                    "(titleId, titleName, categoryType) VALUES (?, ?, ?)",
                    ("PPSA01650", "YouTube", 65536),
                )
                database.execute(
                    "INSERT INTO tbl_contentinfo "
                    "(titleId, titleName, metaDataPath, categoryType) "
                    "VALUES (?, ?, ?, ?)",
                    (
                        "PPSA99999",
                        "Forward Compatible Game",
                        "/user/appmeta/PPSA99999",
                        12345,
                    ),
                )
                database.execute(
                    "INSERT INTO tbl_contentinfo "
                    "(titleId, titleName, metaDataPath, categoryType) "
                    "VALUES (?, ?, ?, ?)",
                    (
                        "PPSA88888",
                        "Stale staged image",
                        "/user/app/PPSA88888",
                        12345,
                    ),
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
                    f'-DUSER_APPMETA_DIR="{appmeta_root}"',
                    f'-DUSER_APP_DIR="{user_app_root}"',
                    f'-DAPP_DB_PATH="{app_db}"',
                    "-I",
                    str(ROOT / "activity-probe"),
                    str(ROOT / "tests/http_server_harness.c"),
                    str(ROOT / "activity-probe/http_server.c"),
                    str(ROOT / "activity-probe/game_metadata.c"),
                    str(ROOT / "activity-probe/game_library.c"),
                    "-pthread",
                    "-lsqlite3",
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
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/installed-games?v=1.49.0",
                    timeout=2,
                ) as response:
                    self.assertEqual(
                        response.headers["Access-Control-Allow-Origin"], "*"
                    )
                    library = json.loads(response.read())
                    self.assertEqual(library["count"], 3)
                    self.assertEqual(library["source"], "app.db")
                    self.assertEqual(library["database_status"], "ok")
                    self.assertGreaterEqual(library["database_rows"], 3)
                    self.assertEqual(library["games"][0]["title_id"], "PPSA02177")
                    self.assertEqual(library["games"][0]["platform"], "PS5")
                    self.assertEqual(library["games"][0]["name"], "Database Game")
                    self.assertEqual(library["games"][0]["version"], "01.023.000")
                    self.assertTrue(library["games"][0]["installed"])
                    self.assertTrue(library["games"][0]["tracked"])
                    self.assertEqual(library["games"][0]["session_count"], 2)
                    self.assertEqual(
                        library["games"][1]["title_id"], "PPSA88888"
                    )
                    self.assertFalse(library["games"][1]["installed"])
                    self.assertEqual(library["games"][2]["title_id"], "PPSA99999")
                    self.assertFalse(library["games"][2]["installed"])
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/library.html", timeout=2
                ) as response:
                    self.assertEqual(response.read(), b"PLAYLOG-LIBRARY")
                class NoRedirect(urllib.request.HTTPRedirectHandler):
                    def redirect_request(
                        self, request, response, code, msg, headers, new_url
                    ):
                        return None
                request = urllib.request.Request(
                    f"http://127.0.0.1:{port}/library.html"
                )
                with self.assertRaises(urllib.error.HTTPError) as redirect:
                    urllib.request.build_opener(NoRedirect()).open(
                        request, timeout=2
                    )
                self.assertEqual(redirect.exception.code, 302)
                self.assertEqual(
                    redirect.exception.headers["Location"],
                    "/library.html?v=dev",
                )
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/game-icon"
                    "?title_id=PPSA02177", timeout=2
                ) as response:
                    self.assertEqual(response.read(), icon)
                    self.assertEqual(
                        response.headers["Cache-Control"],
                        "no-store",
                    )
                # If appmeta is not available (common for a mounted image),
                # use the authoritative icon0Info path from app.db and strip
                # the shell's cache-query suffix.
                shutil.rmtree(appmeta)
                (data / "covers" / "PPSA02177.png").unlink()
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/game-icon"
                    "?title_id=PPSA02177", timeout=2
                ) as response:
                    self.assertEqual(response.read(), icon)
                # Some homebrew-created registries omit categoryType.  The
                # runtime must still expose title IDs and names rather than
                # silently returning an empty library.
                shutil.rmtree(stale_user_app)
                app_db.unlink()
                with sqlite3.connect(app_db) as database:
                    database.execute(
                        "CREATE TABLE tbl_contentinfo ("
                        "titleId TEXT, titleName TEXT)"
                    )
                    database.executemany(
                        "INSERT INTO tbl_contentinfo (titleId, titleName) "
                        "VALUES (?, ?)",
                        [
                            ("PPSA02177 ", "Database Game"),
                            ("PPSA99999", "Forward Compatible Game"),
                        ],
                    )
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/installed-games", timeout=2
                ) as response:
                    library = json.loads(response.read())
                    self.assertEqual(library["count"], 2)
                    self.assertEqual(library["source"], "app.db")
                    self.assertEqual(library["database_status"], "ok_compat")
                    self.assertEqual(library["games"][0]["title_id"], "PPSA02177")
                    self.assertEqual(library["games"][0]["name"], "Database Game")
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/installed-games", timeout=2
                ) as response:
                    library = json.loads(response.read())
                    self.assertEqual(library["count"], 2)
                    self.assertEqual(library["games"][0]["name"], "Database Game")
                with urllib.request.urlopen(
                    f"http://127.0.0.1:{port}/api/game-icon"
                    "?title_id=PPSA02177", timeout=2
                ) as response:
                    self.assertEqual(response.read(), icon)
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
