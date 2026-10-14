#!/usr/bin/env bash
set -euo pipefail

INSTALL_DIR="$HOME/.local/bin"
ARDUINO_CLI="$INSTALL_DIR/arduino-cli"
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'

if ! command -v curl >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y curl
fi

mkdir -p "$INSTALL_DIR"
touch "$HOME/.bashrc"
if ! grep -Fqx "$PATH_LINE" "$HOME/.bashrc"; then
  printf '%s\n' "$PATH_LINE" >> "$HOME/.bashrc"
fi
export PATH="$INSTALL_DIR:$PATH"

curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
  | BINDIR="$INSTALL_DIR" sh

"$ARDUINO_CLI" version
"$ARDUINO_CLI" core update-index
"$ARDUINO_CLI" core install arduino:avr

echo "Arduino CLI and the Arduino AVR core are ready."
