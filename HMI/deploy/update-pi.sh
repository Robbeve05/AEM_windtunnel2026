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
git -C "$REPOSITORY_DIR" pull --ff-only origin RaspberryPi

restore_config
trap - EXIT

cd "$APP_DIR"
bash deploy/install-pi.sh
systemctl --user restart windtunnel-hmi.service

echo
echo "HMI updated. Close and reopen the Windtunnel HMI desktop window to load the new UI."
echo "Local config/config.json and data/ were preserved."
