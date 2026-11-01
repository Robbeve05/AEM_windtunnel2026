"""Migrate the Raspberry Pi's saved HMI configuration to the checked-out main schema.

Standard-library only: runs before the Pi virtualenv/dependencies are installed.
The only defaults are those in HMI/config/config.json at Git HEAD; never keep
another hidden set of runtime defaults here. Preserve local site customizations.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def merged_defaults(defaults: dict, existing: dict) -> dict:
    """Recursively add missing defaults while preserving user/site values."""
    if not isinstance(defaults, dict) or not isinstance(existing, dict):
        raise ValueError("The template and saved configuration must be JSON objects")
    combined = {}
    for key, value in defaults.items():
        if key in existing and isinstance(value, dict) and isinstance(existing[key], dict):
            combined[key] = merged_defaults(value, existing[key])
        else:
            combined[key] = existing.get(key, value)
    # Preserve possible site-specific/forward-compatible keys. They are ignored
    # by the current backend if not part of its validated Config model.
    combined.update({k: v for k, v in existing.items() if k not in combined})
    return combined


def migrate_config(defaults: dict, existing: dict) -> dict:
    """Return validated migration data without performing any hardware action."""
    result = merged_defaults(defaults, existing)
    # Pi installation is always the real ESP32 Ethernet backend, not simulation.
    result["device_mode"] = "esp32_tcp"
    result["simulation"] = False
    result["simulate_usb"] = False
    result.pop("serial_port", None)
    result.pop("baud_rate", None)

    # Never raise an existing operating ceiling while upgrading software.
    # Respect configured lower maxima, but reject out-of-range settings.
    airspeed = float(result["max_airspeed_ms"])
    min_hz = float(result["min_manual_frequency_hz"])
    max_hz = float(result["max_manual_frequency_hz"])
    if not (0 < airspeed <= float(defaults["max_airspeed_ms"])):
        raise ValueError("max_airspeed_ms exceeds validated firmware/HMI limit")
    if not (float(defaults["min_manual_frequency_hz"]) <= min_hz < max_hz <=
            float(defaults["max_manual_frequency_hz"])):
        raise ValueError("manual frequency range must stay within 10–50 Hz")
    cal = result["calibration_defaults"]
    if not (min_hz <= float(cal["start_hz"]) < float(cal["end_hz"]) <= max_hz):
        raise ValueError("calibration range must stay within configured manual frequency range")
    if not result.get("esp32_host"):
        raise ValueError("Configure a valid ESP32 IP/hostname before starting the HMI")
    if not (1 <= int(result["esp32_port"]) <= 65535):
        raise ValueError("Invalid ESP32 TCP port")
    cutoff = float(result["filter_cutoff_hz"])
    if not (0.1 <= cutoff <= 10.0):
        raise ValueError("Pitot filter cutoff must be 0.1–10 Hz")
    return result


def migrate_file(config_file: Path, repository: Path) -> None:
    # 'git show' reads repository defaults even when the tracked working copy
    # has been replaced by the site's saved config.json.
    canonical = subprocess.check_output(
        ["git", "-C", str(repository), "show", "HEAD:HMI/config/config.json"],
        text=True, encoding="utf-8",
    )
    defaults = json.loads(canonical)
    saved = json.loads(config_file.read_text(encoding="utf-8"))
    upgraded = migrate_config(defaults, saved)
    encoded = json.dumps(upgraded, indent=2, ensure_ascii=False) + "\n"
    # Validate first; atomic replace only on success.
    descriptor, temp = tempfile.mkstemp(prefix=".hmi-config-", suffix=".json",
                                      dir=str(config_file.parent))
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            handle.write(encoded)
        os.replace(temp, config_file)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config_file", type=Path)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[2]
    migrate_file(args.config_file, repository)
    print(f"Migrated HMI configuration: {args.config_file}")
