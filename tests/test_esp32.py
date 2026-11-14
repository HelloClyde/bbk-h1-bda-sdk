"""Host integration and MIPS-link checks; no network, serial port or secrets."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ESP32Tests(unittest.TestCase):
    def test_wifi_ui_host(self):
        command = os.environ.get("HOST_CC", "cc")
        compiler = [command] if Path(command).is_file() else shlex.split(command, posix=os.name != "nt")
        if not shutil.which(compiler[0]):
            self.skipTest("host C compiler not installed; set HOST_CC")
        base = ROOT / "sdk/esp32"
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / ("wifi-ui.exe" if os.name == "nt" else "wifi-ui")
            result = subprocess.run([*compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                "-Wno-misleading-indentation", "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast", "-I", str(ROOT / "sdk/include"),
                "-I", str(base / "common"), str(ROOT / "tests/esp32/wifi_ui.c"),
                str(ROOT / "examples/esp32_wifi/core.c"),
                *[str(base / p) for p in ("h1_esp32.c", "common/h1net_client.c",
                    "common/h1net_wire.c", "common/h1net_wifi_config.c")],
                "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe), temp], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(len(list(Path(temp).glob("*.ppm"))), 3)
            if os.environ.get("H1_TEST_ARTIFACT_DIR"):
                output = Path(os.environ["H1_TEST_ARTIFACT_DIR"])
                output.mkdir(parents=True, exist_ok=True)
                for frame in Path(temp).glob("*.ppm"):
                    shutil.copyfile(frame, output / frame.name)

    def test_session_protocol_and_lifetimes(self):
        command = os.environ.get("HOST_CC", "cc")
        compiler = [command] if Path(command).is_file() else shlex.split(command, posix=os.name != "nt")
        if not shutil.which(compiler[0]):
            self.skipTest("host C compiler not installed; set HOST_CC")
        base = ROOT / "sdk/esp32"
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / ("session.exe" if os.name == "nt" else "session")
            subprocess.run([*compiler, "-std=c99", "-Wall", "-Wextra", "-Werror",
                "-Wno-misleading-indentation", "-I", str(ROOT / "sdk/include"),
                "-I", str(base / "common"), str(ROOT / "tests/esp32/session.c"),
                *[str(base / p) for p in ("h1_esp32.c", "common/h1net_client.c",
                    "common/h1net_wire.c", "common/h1net_server.c", "common/h1net_wifi_config.c")],
                "-o", str(exe)], check=True, capture_output=True, text=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS: 11", result.stdout)

    def test_wifi_example_links_as_real_mips(self):
        from h1_bda.build import compile_sources
        example = ROOT / "examples/esp32_wifi"
        payload = compile_sources([example / "main.c", example / "core.c"], [], esp32=True, runtime=True)
        self.assertGreater(len(payload), 1024)
        self.assertLess(len(payload), 1024*1024)

    def test_native_backend_rejects_v2_entry(self):
        from h1_bda.build import compile_sources
        with self.assertRaisesRegex(ValueError, "V1.41"):
            compile_sources([ROOT / "examples/esp32_wifi/main.c"], [], entry_va=0x82600020, esp32=True)
