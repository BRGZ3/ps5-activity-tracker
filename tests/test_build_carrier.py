import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class CarrierBuilderTests(unittest.TestCase):
    def test_carrier_contains_dashboard_library_and_runtime(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node.js is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            png = directory / "base.png"
            output = directory / "carrier.png"
            dashboard = directory / "index.html"
            library = directory / "library.html"
            runtime = directory / "Playlog.elf"
            png.write_bytes(b"PNG\x89PLAYLOG")
            dashboard.write_text("dashboard", encoding="utf-8")
            library.write_text("library", encoding="utf-8")
            runtime.write_bytes(b"elf-runtime")
            subprocess.run(
                [
                    node,
                    str(ROOT / "release-build/build-carrier.js"),
                    str(png),
                    str(output),
                    str(dashboard),
                    str(library),
                    str(runtime),
                    "1.54",
                    "1.48",
                    "1.45.2",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            blob = output.read_bytes()
        self.assertGreaterEqual(len(blob), 56)
        footer = blob[-56:]
        self.assertEqual(footer[:8], b"PLGUPD02")
        offset, length = struct.unpack_from("<QQ", footer, 8)
        bundle = blob[offset : offset + length]
        self.assertEqual(bundle[:8], b"PLGBND02")
        self.assertEqual(struct.unpack_from("<I", bundle, 8)[0], 2)
        self.assertEqual(struct.unpack_from("<I", bundle, 12)[0], 3)
        self.assertEqual(bundle[16:32].split(b"\0", 1)[0], b"1.54")
        self.assertEqual(bundle[32:48].split(b"\0", 1)[0], b"1.48")
        self.assertEqual(bundle[48:64].split(b"\0", 1)[0], b"1.45.2")

        position = 64
        entries = []
        for _ in range(3):
            entry_type, entry_length, _crc, _reserved = struct.unpack_from(
                "<IIII", bundle, position
            )
            position += 16
            entries.append((entry_type, bundle[position : position + entry_length]))
            position += entry_length
        self.assertEqual([entry[0] for entry in entries], [1, 4, 3])
        self.assertEqual(entries[0][1], b"dashboard")
        self.assertEqual(entries[1][1], b"library")
        self.assertEqual(entries[2][1], b"elf-runtime")
        self.assertEqual(position, len(bundle))


if __name__ == "__main__":
    unittest.main()
