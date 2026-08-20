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


if __name__ == "__main__":
    unittest.main()
