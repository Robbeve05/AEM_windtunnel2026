#!/usr/bin/env bash
set -euo pipefail

export PATH="$HOME/.local/bin:$PATH"

APP_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH_NAME="Arduino_Uno_Pitot_HMI"
SOURCE_SKETCH="$APP_DIR/Arduino/Arduino_Uno_Pitot_HMI.ino"
FQBN="arduino:avr:uno"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required. Run: bash HMI/deploy/install-arduino-cli.sh" >&2
  exit 1
fi
if ! arduino-cli core list | grep -q '^arduino:avr'; then
  echo "The Uno board core is missing. Run: bash HMI/deploy/install-arduino-cli.sh" >&2
  exit 1
fi
if [[ ! -f "$SOURCE_SKETCH" ]]; then
  echo "Arduino firmware not found: $SOURCE_SKETCH" >&2
  exit 1
fi

PORTS=()
for port in /dev/serial/by-id/*; do
  [[ -e "$port" ]] && PORTS+=("$port")
done
if ((${#PORTS[@]} == 0)); then
  for port in /dev/ttyACM* /dev/ttyUSB*; do
    [[ -e "$port" ]] && PORTS+=("$port")
  done
fi
if ((${#PORTS[@]} == 0)); then
  echo "No Arduino serial port found. Connect the Uno to the Pi and check /dev/ttyACM*." >&2
  exit 1
fi
if ((${#PORTS[@]} != 1)); then
  printf 'More than one serial device is connected; refusing to guess:\n' >&2
  printf '  %s\n' "${PORTS[@]}" >&2
  exit 1
fi

PORT="${PORTS[0]}"
BUILD_DIR="$(mktemp -d)"
SERVICE_WAS_ACTIVE=0

restart_service() {
  if ((SERVICE_WAS_ACTIVE)); then
    systemctl --user restart windtunnel-hmi.service
  fi
}
cleanup() {
  local result=$?
  trap - EXIT
  restart_service || result=$?
  rm -rf -- "$BUILD_DIR"
  exit "$result"
}
trap cleanup EXIT

if systemctl --user is-active --quiet windtunnel-hmi.service; then
  SERVICE_WAS_ACTIVE=1
  systemctl --user stop windtunnel-hmi.service
fi

mkdir -p "$BUILD_DIR/$SKETCH_NAME"
cp -- "$SOURCE_SKETCH" "$BUILD_DIR/$SKETCH_NAME/$SKETCH_NAME.ino"
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR/build" "$BUILD_DIR/$SKETCH_NAME"
arduino-cli upload --fqbn "$FQBN" --port "$PORT" --input-dir "$BUILD_DIR/build" "$BUILD_DIR/$SKETCH_NAME"
echo "Firmware uploaded successfully to $PORT. The HMI service will now restart."
