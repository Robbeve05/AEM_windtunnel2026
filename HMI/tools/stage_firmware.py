"""Prepare an ESP32 app image on the Raspberry Pi; NEVER performs remote flashing.

Author: R.E. van Eekelen
Copy an ESP-IDF/PlatformIO built firmware.bin to Pi staging with SHA-256 metadata.
Actual OTA requires a separately reviewed ESP32 OTA receiver, signed/authenticated
update channel, dual OTA partitions and hardware maintenance interlocks.
"""
import argparse
import hashlib
import json
import os
import shutil
from datetime import datetime, timezone
from pathlib import Path


def stage(image: Path, output: Path):
    image = image.resolve(strict=True)
    if image.suffix.lower() != ".bin" or not image.is_file():
        raise ValueError("Expected a compiled ESP32 app .bin image")
    size = image.stat().st_size
    if size < 32768 or size > 4 * 1024 * 1024:
        raise ValueError("Firmware size outside expected app image range")
    with image.open("rb") as stream:
        header = stream.read(1)
    if header != b"\xe9":
        raise ValueError("Not an ESP32 application image (missing 0xE9 magic)")
    output.mkdir(parents=True, exist_ok=True)
    destination = output / "firmware.bin"
    temporary = output / "firmware.bin.tmp"
    digest = hashlib.sha256()
    with image.open("rb") as src, temporary.open("wb") as dst:
        for chunk in iter(lambda: src.read(64 * 1024), b""):
            digest.update(chunk)
            dst.write(chunk)
        dst.flush()
        os.fsync(dst.fileno())
    os.replace(temporary, destination)
    metadata = {
        "sha256": digest.hexdigest(),
        "size": size,
        "staged_utc": datetime.now(timezone.utc).isoformat(),
        "status": "STAGED_ONLY_NOT_FLASHED",
        "source": str(image),
    }
    manifest_temp = output / "firmware.json.tmp"
    manifest_temp.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    os.replace(manifest_temp, output / "firmware.json")
    return metadata


def main():
    parser = argparse.ArgumentParser(description="Stage ESP32 firmware; no OTA flashing")
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--output", type=Path, default=Path.home() / "windtunnel-firmware-staged")
    args = parser.parse_args()
    print(json.dumps(stage(args.firmware, args.output), indent=2))


if __name__ == "__main__":
    main()
