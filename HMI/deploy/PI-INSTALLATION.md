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
- The native desktop UI is a PySide6/Qt application. It connects to the local Python service and does not use Chromium or the web UI.
- Run the updated `deploy/install-pi.sh` on the Pi to install PySide6 and the native application launcher.

Close the app window with **Alt+F4**. Reopen using the launcher or the desktop application.
The application remains in simulation mode until ESP32 integration is commissioned.

Service commands on the Pi:

```bash
systemctl --user status windtunnel-hmi
systemctl --user restart windtunnel-hmi
systemctl --user stop windtunnel-hmi
```

`install-pi.sh` is reusable from `~/HMI`, after `python3-venv` is installed.
It installs the service and creates desktop launchers without requiring sudo. To run the
backend tests before installation, set `RUN_TESTS=1` when invoking the script.

After committing and pushing changes to the public `RaspberryPi` branch from a development
computer, update the Pi by closing the HMI window and running:

```bash
cd ~/AEM_windtunnel2026 && bash HMI/deploy/update-pi.sh
```

The updater preserves `HMI/config/config.json` and `HMI/data/`, pulls the latest code,
reinstalls dependencies and launchers, and restarts the local service. Reopen the desktop
application after the update completes.
