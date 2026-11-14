"""Verify manifest, then flash ESP USB2. App-only by default; NVS is retained."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True, help="ESP USB2 UART port, e.g. COM7 or /dev/ttyUSB0")
    ap.add_argument("--images", type=Path, default=Path(__file__).resolve().parents[3] / "build/esp32")
    ap.add_argument("--all", action="store_true", help="first installation: bootloader, partition table and app")
    args = ap.parse_args()
    root = args.images.resolve()
    manifest = json.loads((root / "build.json").read_text())
    if manifest["target"] != "esp32s3":
        raise SystemExit("unexpected target")
    for name, digest in manifest["files"].items():
        path = (root / name).resolve()
        if root not in path.parents or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit("image manifest mismatch: " + name)
    config = json.loads((root / "flasher_args.json").read_text())
    images = config["flash_files"] if args.all else {config["app"]["offset"]: config["app"]["file"]}
    pairs = []
    for offset, name in images.items():
        if name not in manifest["files"]:
            raise SystemExit("unverified flash image")
        pairs += [offset, str(root / name)]
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port]
    subprocess.run([*command, "write_flash", *config["write_flash_args"], *pairs], check=True)
    subprocess.run([*command, "verify_flash", *pairs], check=True)

if __name__ == "__main__":
    main()
