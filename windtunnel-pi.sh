#!/usr/bin/env bash
# R.E. van Eekelen — single Raspberry Pi setup/update entrypoint.
# Fail-closed: NEVER claim to flash an ESP32 without an authenticated OTA receiver.
set -Eeuo pipefail
REPO="https://github.com/Robbeve05/AEM_windtunnel2026.git"
BRANCH="feature/raspberry-pi-ethernet-hmi"
ROOT="$HOME/AEM_windtunnel2026"
MODE="${1:-update}"
case "$MODE" in update|build|stage|install) ;; *) echo "Usage: bash windtunnel-pi.sh [update|install|build|stage]" >&2; exit 2;; esac

say() { printf '\n[Windtunnel] %s\n' "$*"; }
fail() { echo "[Windtunnel] ERROR: $*" >&2; exit 1; }
command -v git >/dev/null || fail "Install git (sudo apt install git)"
command -v python3 >/dev/null || fail "Install Python 3"

if [[ ! -d "$ROOT/.git" ]]; then
  [[ ! -e "$ROOT" ]] || fail "$ROOT exists but is not a Git checkout; back it up yourself."
  say "Cloning feature branch"
  git clone --branch "$BRANCH" --single-branch "$REPO" "$ROOT"
else
  say "Checking existing checkout"
  git -C "$ROOT" fetch origin "$BRANCH"
  current="$(git -C "$ROOT" branch --show-current)"
  [[ "$current" == "$BRANCH" ]] || fail "Current branch is '$current'. Switch manually to '$BRANCH' before updating."
  # Protect user data/config and uncommitted source edits.
  while IFS= read -r changed; do
    [[ -z "$changed" ]] && continue
    path="${changed:3}"
    [[ "$path" == "HMI/config/config.json" || "$path" == HMI/data/* ]] && continue
    fail "Local source change '$path' must be committed/stashed before update."
  done < <(git -C "$ROOT" status --porcelain --untracked-files=normal)
  cp -a "$ROOT/HMI/config/config.json" "$HOME/.windtunnel-config-backup.json" || true
  git -C "$ROOT" restore -- HMI/config/config.json
  git -C "$ROOT" merge --ff-only "origin/$BRANCH"
  [[ -f "$HOME/.windtunnel-config-backup.json" ]] && cp -a "$HOME/.windtunnel-config-backup.json" "$ROOT/HMI/config/config.json"
fi

[[ -e "$HOME/HMI" ]] || ln -s "$ROOT/HMI" "$HOME/HMI"
[[ "$(readlink -f "$HOME/HMI")" == "$ROOT/HMI" ]] || fail "~/HMI points elsewhere; do not overwrite it."

say "Migrating ESP32 Ethernet HMI configuration"
python3 - "$ROOT/HMI/config/config.json" <<'PY'
import json, pathlib, sys
p = pathlib.Path(sys.argv[1]); c = json.loads(p.read_text())
c.update(device_mode="esp32_tcp", simulation=False, esp32_port=5000,
         simulate_usb=False, sample_rate_hz=20)
c["esp32_host"] = c.get("esp32_host") or "192.168.50.10"
c["max_airspeed_ms"] = min(float(c.get("max_airspeed_ms", 30)), 30)
c["filter_cutoff_hz"] = max(0.1, min(float(c.get("filter_cutoff_hz", 1)), 10))
c.pop("serial_port", None); c.pop("baud_rate", None)
p.write_text(json.dumps(c, indent=2) + "\n")
print("Configured ESP32 host:", c["esp32_host"], "(verify this matches the ESP32 IP!)")
PY

if [[ "$MODE" != build && "$MODE" != stage ]]; then
  say "Installing native Qt HMI and local FastAPI service"
  bash "$ROOT/HMI/deploy/install-pi.sh"
  say "Running Pi software tests"
  (cd "$ROOT/HMI" && .venv/bin/python -m unittest discover -s tests -v) || fail "Pi tests failed; do not use this build."
fi

if [[ "$MODE" != install ]]; then
  say "Checking ESP32 build tools"
  if ! command -v pio >/dev/null 2>&1; then
    say "PlatformIO not installed; install with: python3 -m pip install --user platformio"
    say "No ESP32 image built or uploaded."
  else
    (cd "$ROOT/ESP32" && pio run -e waveshare_s3) || fail "ESP32 build failed"
    image="$ROOT/ESP32/.pio/build/waveshare_s3/firmware.bin"
    [[ -s "$image" ]] || fail "ESP32 build produced no firmware.bin"
    "$ROOT/HMI/.venv/bin/python" "$ROOT/HMI/tools/stage_firmware.py" "$image" || fail "Firmware staging failed"
    say "Firmware staged. It has NOT been uploaded to ESP32."
  fi
fi

say "OTA status: NOT AVAILABLE. Current firmware lacks a verified authenticated OTA receiver."
say "First install needs wired flashing of the ESP32 bootloader, OTA partition table and OTA app."
say "ESP32 motor commissioning remains disabled. Never bypass hardware safety."
say "HMI is installed; launch from Windtunnel HMI desktop shortcut."
