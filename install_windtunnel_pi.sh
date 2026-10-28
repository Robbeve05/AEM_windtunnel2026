#!/usr/bin/env bash
# Author: R.E. van Eekelen
# Single Pi entrypoint for ESP32 wind-tunnel native desktop installation/update.
# Refuses to claim or attempt firmware flashing unless an OTA receiver is verified.
set -Eeuo pipefail
BRANCH="feature/raspberry-pi-ethernet-hmi"
URL="https://github.com/Robbeve05/AEM_windtunnel2026.git"
REPO="$HOME/AEM_windtunnel2026"
HMI="$REPO/HMI"
LINK="$HOME/HMI"
stamp="$(date +%Y%m%d-%H%M%S)"
log(){ printf '\n[Windtunnel] %s\n' "$*"; }
die(){ printf '\n[Windtunnel] ERROR: %s\n' "$*" >&2; exit 1; }
command -v git >/dev/null || { log "Installing prerequisites"; sudo apt-get update; sudo apt-get install -y git python3 python3-venv python3-pip; }
command -v python3 >/dev/null || die "Python 3 is required."
if [[ -e "$REPO/.git" ]]; then
  log "Checking existing Git checkout"
  [[ -z "$(git -C "$REPO" status --porcelain --untracked-files=normal | grep -v -F 'HMI/config/config.json' || true)" ]] ||
    die "Uncommitted source changes; commit or stash them before update."
  git -C "$REPO" fetch origin "$BRANCH"
  current="$(git -C "$REPO" branch --show-current)"
  [[ "$current" == "$BRANCH" ]] || git -C "$REPO" switch -c "$BRANCH" --track "origin/$BRANCH" 2>/dev/null ||
    git -C "$REPO" switch "$BRANCH"
  # The tracked config is intentionally overwritten by a fast-forward: backup first.
  cp "$HMI/config/config.json" "$HOME/.windtunnel-config-$stamp.json" 2>/dev/null || true
  git -C "$REPO" restore --worktree -- HMI/config/config.json
  git -C "$REPO" pull --ff-only origin "$BRANCH"
  [[ -f "$HOME/.windtunnel-config-$stamp.json" ]] &&
    cp "$HOME/.windtunnel-config-$stamp.json" "$HMI/config/config.json"
else
  [[ ! -e "$REPO" ]] || die "$REPO exists but is not a Git checkout; refusing to overwrite."
  log "Cloning project"
  git clone --single-branch --branch "$BRANCH" "$URL" "$REPO"
fi
if [[ -L "$LINK" ]]; then
  [[ "$(readlink -f "$LINK")" == "$(readlink -f "$HMI")" ]] ||
    die "$LINK links to another directory; refusing to replace it."
elif [[ -e "$LINK" ]]; then
  die "$LINK exists independently; back it up before continuing."
else
  ln -s "$HMI" "$LINK"
fi
log "Migrating config without deleting measurements"
python3 - "$HMI/config/config.json" <<'PY'
import json, pathlib, sys
p=pathlib.Path(sys.argv[1])
c=json.loads(p.read_text())
c.update(device_mode="esp32_tcp", simulation=False, esp32_port=5000,
         max_airspeed_ms=min(30, float(c.get("max_airspeed_ms",30))),
         simulate_usb=False)
c.setdefault("esp32_host","192.168.50.10")
c.pop("serial_port",None)
c.pop("baud_rate",None)
p.write_text(json.dumps(c,indent=2)+"\n")
PY
log "Installing native Qt desktop HMI"
if [[ ! -d "$HMI/.venv" ]]; then
  sudo apt-get update
  sudo apt-get install -y python3-venv python3-pip
fi
bash "$HMI/deploy/install-pi.sh"
log "Testing Pi backend"
(cd "$HMI" && .venv/bin/python -m unittest discover -s tests -v) || die "Pi tests failed"
log "Native HMI service"
systemctl --user restart windtunnel-hmi.service
log "ESP32 OTA check"
python3 - "$HMI/config/config.json" <<'PY'
import json,socket,sys
cfg=json.load(open(sys.argv[1]))
host=cfg["esp32_host"]
port=cfg["esp32_port"]
print(f"ESP32 address: {host}:{port}")
try:
    with socket.create_connection((host,port),timeout=2):
        print("TCP connection established to ESP32 telemetry port.")
except OSError as ex:
    print(f"ESP32 TCP not reachable yet: {ex}")
print()
print("Firmware Ethernet upload NOT executed:")
print("Current ESP32 firmware has no commissioned, authenticated OTA receiver.")
print("An initial wired flash with matching OTA partitions, signed image policy")
print("and an independently verified physical maintenance interlock is required.")
print("Until then, do not treat copying firmware to the Pi as flashing the ESP32.")
PY
log "Pi native HMI updated. ESP32 remains unchanged."
