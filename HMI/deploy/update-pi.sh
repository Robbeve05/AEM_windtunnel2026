#!/usr/bin/env bash
set -euo pipefail

APP_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
REPOSITORY_DIR="$(cd -- "$APP_DIR/.." && pwd)"
EXPECTED_REPOSITORY="$HOME/AEM_windtunnel2026"

if [[ "$REPOSITORY_DIR" != "$EXPECTED_REPOSITORY" ]]; then
  echo "Expected the Git checkout at $EXPECTED_REPOSITORY; found $REPOSITORY_DIR." >&2
  exit 1
fi

while IFS= read -r CHANGE; do
  [[ -z "$CHANGE" ]] && continue
  if [[ "${CHANGE:3}" == "HMI/config/config.json" ]]; then
    continue
  fi
  echo "Cannot update: uncommitted source changes found: $CHANGE" >&2
  exit 1
done < <(git -C "$REPOSITORY_DIR" status --porcelain --untracked-files=normal)

CONFIG_BACKUP="$(mktemp "$HOME/.windtunnel-hmi-config.XXXXXX")"
cp -a -- "$APP_DIR/config/config.json" "$CONFIG_BACKUP"

restore_config() {
  if [[ -f "$CONFIG_BACKUP" ]]; then
    cp -a -- "$CONFIG_BACKUP" "$APP_DIR/config/config.json"
    rm -f -- "$CONFIG_BACKUP"
  fi
}
trap restore_config EXIT

git -C "$REPOSITORY_DIR" restore --source=HEAD --staged --worktree -- HMI/config/config.json
git -C "$REPOSITORY_DIR" pull --ff-only origin feature/raspberry-pi-ethernet-hmi

restore_config
trap - EXIT

# Preserve existing user data, but migrate legacy Arduino settings to ESP32.
python3 - "$APP_LINK/config/config.json" <<'PY'
import json
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
config = json.loads(path.read_text(encoding="utf-8"))
config["device_mode"] = "esp32_tcp"
config["simulation"] = False
config["esp32_host"] = config.get("esp32_host") or "192.168.50.10"
config["esp32_port"] = 5000
config["max_airspeed_ms"] = min(float(config.get("max_airspeed_ms", 30)), 30)
config["filter_cutoff_hz"] = max(0.1, min(float(config.get("filter_cutoff_hz", 1)), 10))
config["simulate_usb"] = False
config.pop("serial_port", None)
config.pop("baud_rate", None)
path.write_text(json.dumps(config, indent=2) + "\\n", encoding="utf-8")
PY

bash "$HOME/HMI/deploy/install-pi.sh"
systemctl --user restart windtunnel-hmi.service

echo
echo "HMI updated. Close and reopen the Windtunnel HMI desktop window to load the new UI."
echo "Local config/config.json and data/ were preserved."
