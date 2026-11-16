"""Verify manifest, then flash ESP USB2. App-only by default; NVS is retained."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="ESP USB2 UART port, e.g. COM7 or /dev/ttyUSB0")
    ap.add_argument("--images", type=Path, default=Path(__file__).resolve().parents[3] / "build/esp32")
    ap.add_argument("--all", action="store_true", help="first installation: bootloader, partition table and app")
    ap.add_argument("--check", action="store_true", help="verify images and show the flash plan without opening a port")
    ap.add_argument("--baud", type=int, default=115200, help="ROM flashing rate (not the firmware console rate)")
    args = ap.parse_args()
    if not args.check and not args.port:
        ap.error("--port is required when flashing; use --check for offline validation")
    if args.baud <= 0:
        ap.error("--baud must be positive")
    root = args.images.resolve()
    manifest = json.loads((root / "build.json").read_text())
    if manifest["target"] != "esp32s3":
        raise SystemExit("unexpected target")
    for name, digest in manifest["files"].items():
        path = (root / name).resolve()
        if root not in path.parents or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise SystemExit("image manifest mismatch: " + name)
    if "flasher_args.json" not in manifest["files"]:
        raise SystemExit("unverified flash configuration")
    config = json.loads((root / "flasher_args.json").read_text())
    images = config["flash_files"] if args.all else {config["app"]["offset"]: config["app"]["file"]}
    pairs = []
    for offset, name in images.items():
        if name not in manifest["files"]:
            raise SystemExit("unverified flash image")
        pairs += [offset, str(root / name)]
    if args.check:
        print(json.dumps(dict(target="esp32s3", firmware=manifest.get("firmware"),
                              mode="all" if args.all else "app-only",
                              flash_files=images, write_flash_args=config["write_flash_args"],
                              verified_files=len(manifest["files"])), indent=2))
        return
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port,
               "--baud", str(args.baud)]
    subprocess.run([*command, "write_flash", *config["write_flash_args"], *pairs], check=True)
    subprocess.run([*command, "verify_flash", *pairs], check=True)

if __name__ == "__main__":
    main()
