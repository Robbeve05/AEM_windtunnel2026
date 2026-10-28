#!/usr/bin/env bash
# One-step Raspberry Pi updater for the native wind tunnel HMI.
# R.E. van Eekelen
set -euo pipefail
ROOT="$HOME/AEM_windtunnel2026"
BRANCH="feature/raspberry-pi-ethernet-hmi"
if [[ -d "$ROOT/.git" ]]; then
  cd "$ROOT"
  git fetch origin "$BRANCH"
  git switch "$BRANCH"
  bash HMI/deploy/update-pi.sh
else
  bash <(curl -fsSL "https://raw.githubusercontent.com/Robbeve05/AEM_windtunnel2026/$BRANCH/HMI/deploy/setup-pi-from-github.sh")
fi
echo "Native Pi HMI updated."
echo "Ethernet OTA upload is NOT available yet: ESP32 receiver and physical maintenance interlock are unimplemented."
echo "To stage an already-built firmware.bin run:"
echo "  python3 ~/HMI/tools/stage_firmware.py /path/to/firmware.bin"
