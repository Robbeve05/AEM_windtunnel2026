# Windtunnel 2026 · Flight Deck HMI

FastAPI, vanilla JavaScript and offline canvas charts for the Avans wind tunnel. Runs without hardware on Windows and Raspberry Pi OS 64-bit. All six pages work without internet access. No frontend build step or CDN.

## Start on Windows

Open PowerShell in this **HMI** folder. Install Python 3.11 or newer if needed.

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

If script activation is blocked, activation is optional:

```powershell
.venv\Scripts\python.exe -m pip install -r requirements.txt
.venv\Scripts\python.exe -m uvicorn main:app --host 127.0.0.1 --port 8000
```

Open http://localhost:8000. API documentation: http://localhost:8000/docs.
Use one server process / worker: controller and recording ownership are process-local.
Development reload interrupts active experiments; use the command without `--reload` during recording.

## Operator workflow

1. Set the airspeed and press **Start tunnel**. The simulated fan ramps up with a first-order response.
2. Wait for **RUNNING**. Enter a measurement name, for example `NACA0012_AOA5_Run01`.
3. Press **Start measurement** to record. It does not start the tunnel.
4. Press **Stop & save** to close CSV files, write metadata and calculate the mean Cp curve. The tunnel keeps running.
5. Inspect the run in **Measurements**. Raw and processed previews are limited to 200 rows; complete files remain on disk.
6. Choose a run in **Cp Analysis** to recalculate from stored samples using a different density.
7. Open **Storage**, export a run or all unexported runs, then eject the simulated USB.
8. Press **Software stop** to ramp the tunnel down. This does not stop an active recording; stopping flow during an experiment remains recorded.

The software stop is **not a safety-rated emergency stop**. Physical E-stop, interlocks, watchdog and real-time control must be implemented independently in hardware / ESP32 firmware.

## Storage and USB

Each run is stored locally first:

```text
data/measurements/<UTC timestamp>_<sanitized name>_<unique suffix>/
  raw_data.csv         # host UTC, ESP32 milliseconds and every received value
  processed_data.csv   # velocity, calculated q_inf and per-tap Cp
  cp_curve.csv         # mean of valid per-sample Cp, pressure and tap geometry
  experiment.json     # configuration snapshot, timing, counts and export state
```

Files flush each second and flush/fsync/close on stop. A run left open by an interrupted process is marked ERROR at the next startup; its CSV files are retained for recovery. This does not guarantee survival of every sample during power loss. Use reliable power and storage for experiments.

The default `simulate_usb: true` exports to **data/simulated_usb/** on the local disk. This is a test destination, not a physical USB device. **Reconnect simulated USB** reverses simulated ejection.

For physical USB, set `simulate_usb: false` and `usb_mount_path` to an existing, OS-mounted drive root (Windows example `E:/`; Linux example `/media/pi/WINDDATA`). The application never creates a missing physical mount. It checks free space, stages the full folder as `.partial`, then renames it after copying. Local data is never deleted. Re-exporting creates another uniquely named copy. Recalculating Cp marks the run unexported again. A failed export can leave a `.partial` folder on the destination for inspection.

Physical safe unmount is delegated to the operating system: the UI's physical eject action provides instructions and does not claim to unmount the device. On Raspberry Pi use the desktop eject action or `udisksctl unmount -b /dev/sdX1` with the actual USB partition; on Windows use **Safely Remove Hardware**. Do not remove a device while export is running.

## Configuration and Cp conventions

Edit **config/config.json**, then restart. Settings displays the loaded configuration; it is intentionally read-only while experiments can be running.

- `sample_rate_hz`: logger acquisition rate (default 20 Hz). Browser WebSocket updates are bounded at 10 Hz to reduce Pi workload. Python supervisory timing is not hard real-time.
- `pressure_taps`: any 1–128 uniquely named taps, with `x_c` from 0–1 and `surface` upper/lower.
- `air_density`, `max_airspeed_ms`: validated flow parameters.
- `serial_port`, `baud_rate`: future transport settings.
- `data_directory`: absolute or relative to this HMI folder.
- `simulation`, `simulate_usb`, `usb_mount_path`: simulation and export settings.

Simulated pressure taps and p_inf use **absolute Pa**. Cp = (p − p_inf)/(0.5 ρ V²). Cp is blank/null when calculated q < 1 Pa, preventing division by zero and unstable coefficients near zero speed. Stored curves average valid per-sample Cp values, using the original run's tap configuration. Recalculation updates `cp_curve.csv` and Cp metadata; the original per-sample processed CSV is retained with its original density in the configuration snapshot. Negative Cp is plotted upward. `charts.js` accepts extra series for future XFOIL comparison; importing XFOIL files is not part of this delivery.

Raw simulator values are already expressed in engineering units; the raw and calibrated columns are identical until actual calibration and ADC counts are supplied by firmware. The complete incoming frame is also visible in Raw Data.

## ESP32 integration

`backend/esp32.py` prepares a pyserial newline-delimited JSON adapter. Hardware mode intentionally fails startup until a validated firmware protocol is integrated. Keep `simulation: true` for this release. Commission command acknowledgements, sequence numbers, timestamp rollover, heartbeat timeout, pressure units/reference, sensor quality, reconnect behavior, fault reset and hardware interlocks before wiring the transport into `Controller`. The ESP32 must own PID, VFD and fast safety behavior; browser connection loss must never be the physical safety mechanism.

The browser reconnects automatically with bounded exponential backoff and detects stale telemetry after four seconds. On disconnect it marks values stale and disables controls. Recording belongs to the server and continues if a browser disconnects.

## Raspberry Pi OS 64-bit

The Pi can run the HMI as a Python desktop application. Its interface is built with PySide6/Qt and reuses the Python acquisition and storage service; it does not open Chromium or render the web interface. The native interface follows the same navigation, colors, status panels, charts and measurement workflow.

To install or replace the old manually copied version, paste this **single command** into the Pi terminal:

```bash
curl -fsSL https://raw.githubusercontent.com/Robbeve05/AEM_windtunnel2026/RaspberryPi/HMI/deploy/setup-pi-from-github.sh -o /tmp/setup-windtunnel-hmi.sh && bash /tmp/setup-windtunnel-hmi.sh
```

The script uses the public GitHub repository without logging into GitHub. It installs Git and Python prerequisites, clones the `RaspberryPi` branch into `~/AEM_windtunnel2026`, and links `~/HMI` to its `HMI` project folder. If an older `~/HMI` or `~/AEM_windtunnel2026` already exists, it is renamed to a timestamped backup first. The existing `config/config.json` and all files in `data/` are copied into the new installation. It then installs the Python environment and creates the local service, **Windtunnel HMI** desktop/menu launcher, and desktop-session autostart. `sudo` may ask for the Pi's normal system password to install prerequisites; no GitHub credentials are needed.

After installation, start **Windtunnel HMI** from the desktop menu. It opens as a native Qt window; close it with **Alt+F4**. The local service listens on `127.0.0.1` and is not exposed to the network. To update later, paste the same `curl ... && bash ...` installer command again; it backs up the current folders and preserves `config/config.json` and `data/`.

For a browser-only/server deployment instead, copy this directory to `/home/pi/windtunnel-hmi` (replace `pi` with your actual login). Do not copy a Windows `.venv`; recreate it on the Pi.

```bash
cd /home/pi/windtunnel-hmi
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -m uvicorn main:app --host 0.0.0.0 --port 8000 --workers 1
hostname -I
```

From another PC on the same trusted network, open `http://<Pi-IP>:8000`. This initial local-lab application has no authentication; do not expose port 8000 to the public internet. Configure authentication and network access controls before broader deployment.

For automatic startup, save this as `/etc/systemd/system/windtunnel-hmi.service`, adjusting the user and paths:

```ini
[Unit]
Description=Windtunnel HMI
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/windtunnel-hmi
ExecStart=/home/pi/windtunnel-hmi/.venv/bin/python -m uvicorn main:app --host 0.0.0.0 --port 8000 --workers 1
Restart=on-failure
RestartSec=5
TimeoutStopSec=60

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now windtunnel-hmi
journalctl -u windtunnel-hmi -f
```

Give the service user write permission to the measurement directory and mounted USB. Later, serial access may require adding that user to the `dialout` group.

## Project map / verification

- `main.py`: API routes, lifespan, acquisition loop, WebSocket stream.
- `backend/`: simulator, serial transport, models, measurement storage, Cp and USB abstraction.
- `static/`: responsive HTML/CSS and ES modules for UI, offline charts and reconnecting WebSocket.
- `config/config.json`: validated startup settings.
- `tests/test_workflow.py`: isolated temporary-directory tests; no operator measurements modified.

```powershell
.venv\Scripts\python.exe -m unittest discover -s tests -v
```

The tests cover recording guards, independent stop behavior, file persistence, Cp, export/eject, interrupted-run recovery and a reconnecting WebSocket client. Host and hardware remain simulated on this first delivery; Raspberry Pi performance and physical USB/ESP32 operation need testing on the target device.
