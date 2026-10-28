#!/usr/bin/env bash
set -euo pipefail

REPOSITORY_URL="https://github.com/Robbeve05/AEM_windtunnel2026.git"
REPOSITORY_DIR="$HOME/AEM_windtunnel2026"
APP_LINK="$HOME/HMI"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
STAGING_DIR="$(mktemp -d "$HOME/.windtunnel-hmi-install.XXXXXX")"
USER_DATA="$STAGING_DIR/user-data"
CLONE_DIR="$STAGING_DIR/repository"
BACKUP_SUFFIX="$TIMESTAMP-$$"

cleanup() {
  rm -rf -- "$STAGING_DIR"
}
trap cleanup EXIT

if [[ -L "$APP_LINK" ]]; then
  LINK_TARGET="$(readlink -f -- "$APP_LINK" || true)"
  if [[ "$LINK_TARGET" != "$REPOSITORY_DIR/HMI" ]]; then
    echo "Refusing to replace unexpected HMI symlink: $APP_LINK -> $LINK_TARGET" >&2
    exit 1
  fi
elif [[ -e "$APP_LINK" && ! -d "$APP_LINK" ]]; then
  echo "Refusing to replace non-directory path: $APP_LINK" >&2
  exit 1
fi

sudo apt-get update
sudo apt-get install -y git python3 python3-venv python3-pip

SOURCE_DIRS=()
if [[ -d "$APP_LINK" ]]; then
  SOURCE_DIRS+=("$(readlink -f -- "$APP_LINK")")
fi
if [[ -d "$REPOSITORY_DIR/HMI" ]]; then
  SOURCE_DIRS+=("$(readlink -f -- "$REPOSITORY_DIR/HMI")")
fi

if [[ "${#SOURCE_DIRS[@]}" -gt 0 ]]; then
  mkdir -p "$USER_DATA"
  for SOURCE_DIR in "${SOURCE_DIRS[@]}"; do
    if [[ ! -f "$USER_DATA/config.json" && -f "$SOURCE_DIR/config/config.json" ]]; then
      cp -a -- "$SOURCE_DIR/config/config.json" "$USER_DATA/config.json"
    fi
    if [[ -d "$SOURCE_DIR/data" ]]; then
      mkdir -p "$USER_DATA/data"
      cp -an -- "$SOURCE_DIR/data/." "$USER_DATA/data/"
    fi
  done
fi

git clone --branch feature/raspberry-pi-ethernet-hmi --single-branch "$REPOSITORY_URL" "$CLONE_DIR"

if systemctl --user is-active --quiet windtunnel-hmi.service; then
  systemctl --user stop windtunnel-hmi.service
fi

if [[ -d "$APP_LINK" && ! -L "$APP_LINK" ]]; then
  mv -- "$APP_LINK" "$HOME/HMI-backup-$BACKUP_SUFFIX"
elif [[ -L "$APP_LINK" ]]; then
  rm -- "$APP_LINK"
fi

if [[ -e "$REPOSITORY_DIR" || -L "$REPOSITORY_DIR" ]]; then
  mv -- "$REPOSITORY_DIR" "$HOME/AEM_windtunnel2026-backup-$BACKUP_SUFFIX"
fi

mv -- "$CLONE_DIR" "$REPOSITORY_DIR"
ln -s "$REPOSITORY_DIR/HMI" "$APP_LINK"

if [[ -f "$USER_DATA/config.json" ]]; then
  cp -a -- "$USER_DATA/config.json" "$APP_LINK/config/config.json"
  "$APP_LINK/.venv/bin/python" -c "" 2>/dev/null || true
fi
if [[ -d "$USER_DATA/data" ]]; then
  mkdir -p "$APP_LINK/data"
  cp -a -- "$USER_DATA/data/." "$APP_LINK/data/"
fi

cd "$APP_LINK"
bash deploy/install-pi.sh

echo
echo "Windtunnel HMI is installed from the ESP32 Ethernet feature branch."
echo "No GitHub login was used. Existing files are backed up under your home directory."
echo "Start it from the desktop menu as 'Windtunnel HMI'."
