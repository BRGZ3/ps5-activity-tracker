import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class EtaHenPluginContractTests(unittest.TestCase):
    def test_runtime_does_not_remove_its_plugin_container(self):
        source = (ROOT / "activity-probe" / "main.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn(
            'unlink("/data/etaHEN/plugins/ps5-activity-tracker.plugin")',
            source,
        )
        self.assertNotIn(
            'unlink("/data/etaHEN/plugins/'
            'ps5-activity-tracker.plugin.auto_start")',
            source,
        )

    def test_klog_interruptions_are_recoverable(self):
        source = (ROOT / "activity-probe" / "main.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("open_klog(void)", source)
        self.assertIn('write_event("klog_reopen_pending"', source)
        self.assertIn("if(klog_fd < 0 && now_ms >= next_klog_retry_ms)", source)
        self.assertIn("http_retry_reported", source)
        self.assertIn("tracker_refresh_console_ip", source)
        self.assertIn("signal(SIGPIPE, SIG_IGN)", source)
        self.assertIn("signal(SIGHUP, SIG_IGN)", source)
        self.assertNotIn(
            "&& errno != EINTR) {\n            break;\n        }\n        /* Do not enter",
            source,
        )

    def test_http_disconnect_cannot_terminate_runtime(self):
        source = (ROOT / "activity-probe" / "http_server.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("MSG_NOSIGNAL", source)
        self.assertIn("calloc(INSTALLED_GAME_CAPACITY", source)
        self.assertNotIn("game_library_entry_t entries[256]", source)
        self.assertIn("malloc(FILE_RESPONSE_BUFFER_SIZE)", source)
        self.assertIn("flags & ~O_NONBLOCK", source)
        self.assertIn("would_block++", source)

    def test_offline_update_does_not_trust_a_stale_applied_marker(self):
        source = (ROOT / "activity-probe" / "offline_update.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('"/user/app/ACTV00002/sce_sys/icon0.png"', source)
        self.assertIn('"/system_ex/app/ACTV00002/sce_sys/icon0.png"', source)
        self.assertIn("version_compare", source)
        self.assertIn("dashboard_matches", source)
        self.assertIn("file_contains(DASHBOARD_LIBRARY_TARGET", source)
        self.assertIn("available = !applied_matches || !dashboard_matches", source)

    def test_library_does_not_use_appmeta_cache_as_install_proof(self):
        source = (ROOT / "activity-probe" / "game_library.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("path_is_metadata_cache", source)
        self.assertIn("mount_link_is_live", source)
        self.assertIn("source_directory_is_live", source)
        self.assertIn("Metadata alone", source)
        self.assertIn('"eboot.bin", "app.pkg"', source)
        self.assertIn("never use it as proof", source)
        roots = source.split("title_directory_exists", 1)[1].split(
            "database_retry_pause", 1
        )[0]
        self.assertNotIn("USER_APPMETA_DIR, SYSTEM_APPMETA_DIR", roots)


if __name__ == "__main__":
    unittest.main()
