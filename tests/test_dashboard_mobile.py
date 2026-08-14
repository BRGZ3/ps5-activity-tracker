import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DashboardMobileContractTests(unittest.TestCase):
    def test_dashboard_has_mobile_layout_contract(self):
        html = (ROOT / "dashboard/index.html").read_text(encoding="utf-8")
        self.assertIn('name="viewport"', html)
        self.assertIn("@media(max-width:800px)", html)
        self.assertIn("@media(max-width:480px)", html)
        self.assertIn(".table-scroll { overflow-x:auto }", html)
        self.assertIn(".donut-row { grid-template-columns:1fr;", html)
        self.assertIn('id="readonlyNotice"', html)
        self.assertIn('fetch("/api/access?', html)
        self.assertIn('pageIsLoopback', html)
        self.assertIn('lanReadOnly=!pageIsLoopback()', html)
        self.assertIn('/api/game-icon?title_id=', html)
        self.assertIn('class="game-cover"', html)
        self.assertNotIn('id="settingsFirmware" type="number" min="0.01" max="99.99" step="0.01" inputmode="decimal" placeholder="4.50" required', html)
        self.assertIn('data.timezone_source!=="config"', html)

    def test_script_dom_references_are_present(self):
        html = (ROOT / "dashboard/index.html").read_text(encoding="utf-8")
        ids = set(re.findall(r'id="([A-Za-z0-9_-]+)"', html))
        references = set(re.findall(r'\$\("([A-Za-z0-9_-]+)"\)', html))
        self.assertFalse(references - ids, sorted(references - ids))


if __name__ == "__main__":
    unittest.main()
