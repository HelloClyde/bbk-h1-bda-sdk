"""The flashing tool must verify images and retain NVS; no device is used."""
import importlib.util
import json
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("esp_flash", ROOT / "sdk/esp32/tools/flash_firmware.py")
flash = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(flash)

class FlashManifestTests(unittest.TestCase):
    def images(self, root):
        config = dict(app=dict(offset="0x10000", file="h1_wifi.bin"),
                      flash_files={"0x0":"bootloader.bin", "0x8000":"partition.bin", "0x10000":"h1_wifi.bin"},
                      write_flash_args=["--flash_mode", "dio", "--flash_size", "16MB", "--flash_freq", "40m"])
        for name in config["flash_files"].values():
            (root / name).write_bytes(b"public-test-image")
        (root / "flasher_args.json").write_text(json.dumps(config))
        hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
        (root / "build.json").write_text(json.dumps(dict(target="esp32s3",files=hashes)))

    def test_default_app_only_and_first_install_preserve_nvs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); self.images(root)
            for extra, expected in (([], 1), (["--all"], 3)):
                argv = ["flash", "--port", "TEST-NO-DEVICE", "--images", temp, *extra]
                with patch.object(sys, "argv", argv), patch.object(flash.subprocess, "run") as run:
                    flash.main()
                    self.assertEqual(run.call_count, 2)  # write, then read-back verification
                    write, verify = [call.args[0] for call in run.call_args_list]
                    self.assertIn("write_flash", write); self.assertIn("verify_flash", verify)
                    for command in (write, verify):
                        self.assertEqual(sum(arg.endswith(".bin") for arg in command), expected)
                        self.assertNotIn("erase_flash", command)
                        self.assertIn("0x10000", command)
                        if not extra:
                            self.assertNotIn("0x0", command); self.assertNotIn("0x8000", command)

    def test_changed_image_never_reaches_esptool(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); self.images(root)
            (root / "h1_wifi.bin").write_bytes(b"corrupt")
            with patch.object(sys,"argv",["flash","--port","TEST-NO-DEVICE","--images",temp]), \
                 patch.object(flash.subprocess,"run") as run:
                with self.assertRaisesRegex(SystemExit,"manifest mismatch"):
                    flash.main()
                run.assert_not_called()
