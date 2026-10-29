#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
APP_DIR="$(pwd)"
if [[ "$(readlink -f "$APP_DIR")" != "$(readlink -f "$HOME/HMI")" ]]; then
  echo 'Place this project in ~/HMI before installation.' >&2
  exit 1
fi
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-desktop.txt
if [[ "${RUN_TESTS:-0}" == "1" ]]; then
  .venv/bin/python -m unittest discover -s tests -v
else
  echo 'Skipping backend tests; set RUN_TESTS=1 to run them before installation.'
fi
mkdir -p "$HOME/.config/systemd/user" "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.config/autostart"
install -m 755 deploy/start-hmi.sh "$HOME/.local/bin/windtunnel-hmi"
cat > "$HOME/.config/systemd/user/windtunnel-hmi.service" <<'EOF'
[Unit]
Description=Windtunnel HMI local server
After=network.target

[Service]
Type=simple
WorkingDirectory=%h/HMI
ExecStart=%h/HMI/.venv/bin/python -m uvicorn main:app --host 127.0.0.1 --port 8000 --workers 1
Restart=on-failure
RestartSec=5
TimeoutStopSec=60

[Install]
WantedBy=default.target
EOF
cat > "$HOME/.local/share/applications/windtunnel-hmi.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Windtunnel HMI
Comment=Windtunnel control and measurements
Exec="$HOME/.local/bin/windtunnel-hmi"
Icon=applications-engineering
Terminal=false
Categories=Education;Science;
StartupNotify=false
EOF
cp "$HOME/.local/share/applications/windtunnel-hmi.desktop" "$HOME/.config/autostart/windtunnel-hmi.desktop"
if [[ -d "$HOME/Desktop" ]]; then
  install -m 755 "$HOME/.local/share/applications/windtunnel-hmi.desktop" "$HOME/Desktop/Windtunnel HMI.desktop"
fi
systemctl --user daemon-reload
systemctl --user enable --now windtunnel-hmi.service
echo 'Installed: user service, desktop application and desktop-session autostart.'
echo 'Boot without login requires loginctl enable-linger for this user.'
