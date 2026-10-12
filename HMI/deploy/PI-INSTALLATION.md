# Installed on windtunnel · 2026-10-05

- User: `windtunnel_pi`; application: `/home/windtunnel_pi/HMI`.
- Platform: Debian 13, aarch64, Python 3.13, labwc Wayland desktop.
- Existing configuration, measurements and native virtual environment retained.
- Previous source/configuration archived in `~/hmi-backups/`.
- User service: `windtunnel-hmi.service`, enabled; user lingering already enabled.
- Server listens only on the Pi at `http://127.0.0.1:8000`.
- Launcher: `~/.local/bin/windtunnel-hmi`.
- Application menu and desktop shortcut: **Windtunnel HMI**.
- Desktop autostart: `~/.config/autostart/windtunnel-hmi.desktop`.
- Existing desktop auto-login retained. No reboot performed during installation.
- Chromium uses a separate profile at `~/.local/share/windtunnel-hmi/chromium`.
- The Pi launcher now starts Chromium in app-window mode (no browser tabs or address bar), maximized rather than kiosk-fullscreen. Run the updated `deploy/install-pi.sh` on the Pi to install the updated launcher.

Close the app window with **Alt+F4**. Reopen using the launcher or the desktop application.
The application remains in simulation mode until ESP32 integration is commissioned.

Service commands on the Pi:

```bash
systemctl --user status windtunnel-hmi
systemctl --user restart windtunnel-hmi
systemctl --user stop windtunnel-hmi
```

`install-pi.sh` is reusable from `~/HMI`, after `python3-venv` and Chromium are installed.
It runs tests, installs the service and creates desktop launchers without requiring sudo.
