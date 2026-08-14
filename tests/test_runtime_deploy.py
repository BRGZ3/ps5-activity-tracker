import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class RuntimeDeployTests(unittest.TestCase):
    def build_harness(self, root: Path) -> Path:
        compiler = shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler is unavailable")
        paths = {
            "RUNTIME_DIR": root / "runtime",
            "RUNTIME_TARGET": root / "runtime" / "Playlog.elf",
            "ETAHEN_ELF_TARGET": root / "etahen" / "Playlog.elf",
            "AUTOLOADER_TARGET": root / "autoload" / "Playlog.elf",
            "AUTOLOADER_LIST": root / "autoload" / "autoload.txt",
            "PLDMGR_ELF_TARGET": root / "pldmgr" / "Playlog.elf",
            "OFFLINE_DATA_ROOT": root / "data",
            "OFFLINE_USB_ROOT": root / "usb",
        }
        binary = root / "runtime-deploy-test"
        command = [compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L"]
        command.extend(
            f'-D{name}="{path}"' for name, path in paths.items()
        )
        command.extend(
            [
                "-I",
                str(ROOT / "activity-probe"),
                str(ROOT / "tests" / "runtime_deploy_harness.c"),
                str(ROOT / "activity-probe" / "offline_update.c"),
                "-o",
                str(binary),
            ]
        )
        subprocess.run(command, check=True)
        return binary

    def test_updates_manual_plk_and_usb_runtime_copies(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = self.build_harness(root)
            source = root / "new.elf"
            source.write_bytes(b"PLAYLOG-NEW")
            autoload = root / "autoload"
            pldmgr = root / "pldmgr"
            usb = root / "usb0" / "ps5_autoloader"
            for directory in (autoload, pldmgr, usb):
                directory.mkdir(parents=True)
                (directory / "Playlog.elf").write_bytes(b"OLD")
            (autoload / "autoload.txt").write_text(
                "kstuff.elf\n!5000\nPlaylog.elf\n", encoding="utf-8"
            )

            result = subprocess.run(
                [str(binary), str(source), "autoloader"],
                check=True,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.stdout.strip(), "0 3 0")
            for directory in (autoload, pldmgr, usb):
                self.assertEqual(
                    (directory / "Playlog.elf").read_bytes(), b"PLAYLOG-NEW"
                )

    def test_does_not_confirm_unconfigured_autoloader(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = self.build_harness(root)
            source = root / "new.elf"
            source.write_bytes(b"PLAYLOG-NEW")
            (root / "autoload").mkdir()

            result = subprocess.run(
                [str(binary), str(source), "autoloader"],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout.strip(), "1 1 1")

    def test_updates_payload_manager_without_creating_default_autoloader(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = self.build_harness(root)
            source = root / "new.elf"
            source.write_bytes(b"PLAYLOG-NEW")
            pldmgr = root / "pldmgr"
            pldmgr.mkdir()
            (pldmgr / "Playlog.elf").write_bytes(b"OLD")

            result = subprocess.run(
                [str(binary), str(source), "autoloader"],
                check=True,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.stdout.strip(), "0 1 1")
            self.assertEqual(
                (pldmgr / "Playlog.elf").read_bytes(), b"PLAYLOG-NEW"
            )
            self.assertFalse((root / "autoload").exists())


if __name__ == "__main__":
    unittest.main()
