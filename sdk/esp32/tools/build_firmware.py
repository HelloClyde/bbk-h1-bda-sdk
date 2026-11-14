"""Build pinned ESP32-S3 firmware, without flashing or copying private config."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

BASE = Path(__file__).resolve().parents[1]
SDK = BASE.parents[1]
IDF_COMMIT = "880a203ae603d08a94d06aa01e0d6761958e5e9e"

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stage", type=Path, default=Path(tempfile.gettempdir()) / "h1-sdk-esp32")
    ap.add_argument("--output", type=Path, default=SDK / "build/esp32")
    ap.add_argument("--jobs", type=int, default=2, help="bound compiler RAM use (default: 2)")
    args = ap.parse_args()
    if args.jobs < 1:
        ap.error("--jobs must be positive")
    idf = Path(os.environ["IDF_PATH"]).resolve()
    revision = subprocess.check_output(["git", "-c", f"safe.directory={idf.as_posix()}",
                                       "-C", str(idf), "rev-parse", "HEAD"], text=True).strip()
    if revision != IDF_COMMIT:
        raise SystemExit("requires ESP-IDF 5.5.6 at " + IDF_COMMIT)
    stage = args.stage.resolve()
    if any(c.isspace() for c in str(stage)):
        raise SystemExit("ESP-IDF stage path must not contain spaces")
    # Copy an explicit source allowlist, never SDK build output or credentials.
    copied = {}
    for folder in ("common", "esp32", "third_party"):
        for source in (BASE / folder).rglob("*"):
            relative = source.relative_to(BASE)
            if not source.is_file() or any(p in ("build", "managed_components", "__pycache__", ".git") for p in relative.parts):
                continue
            if source.name in ("sdkconfig", "sdkconfig.old"):
                continue
            destination = stage / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            copied[relative.as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()
    project = stage / "esp32"
    shutil.copyfile(project / "sdkconfig.defaults", project / "sdkconfig")
    environment = dict(os.environ, IDF_TARGET="esp32s3", IDF_PY_BUILD_JOBS=str(args.jobs))
    subprocess.run([sys.executable, str(idf / "tools/idf.py"), "reconfigure", "build"],
                   cwd=project, env=environment, check=True)
    build = project / "build"
    config = json.loads((build / "config/sdkconfig.json").read_text())
    for key in ("USB_HOST_HUBS_SUPPORTED", "MBEDTLS_EXTERNAL_MEM_ALLOC", "SPIRAM"):
        if config.get(key) is not True:
            raise SystemExit("required generated configuration missing: " + key)
    args.output.mkdir(parents=True, exist_ok=True)
    names = ["h1_wifi.bin", "bootloader/bootloader.bin", "partition_table/partition-table.bin",
             "flasher_args.json", "config/sdkconfig.json", "config/sdkconfig.h"]
    hashes = {}
    for name in names:
        destination = args.output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(build / name, destination)
        hashes[name] = hashlib.sha256(destination.read_bytes()).hexdigest()
    manifest = dict(firmware="0.4.4", target="esp32s3", idf_commit=revision,
                    hardware_verified=False, source_sha256=copied, files=hashes)
    (args.output / "build.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print("Built and hashed firmware:", args.output.resolve())

if __name__ == "__main__":
    main()
