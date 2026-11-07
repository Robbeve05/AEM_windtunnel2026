# Avans Windtunnel — Native PySide6 HMI

The only operator interface is the native Qt application (`desktop.py`). The local FastAPI backend (`main.py`) remains responsible for acquisition, ESP32 Ethernet communication, measurements, calibration sequencing, persistent storage and exports. The browser-based operator frontend has been removed.

## Native interface

Six pages: Overview, Raw Data, Cp Analysis, Measurements, Storage, Settings. Settings has two tabs: General and Calibration. Calibration uses the existing ESP32 MANUAL mode, physical START/STOP and the FastAPI calibration endpoints. It supports configuration of 10–50 Hz, 5 Hz steps and 5-second averaging, live stability/status, plot, history, CSV/JSON export, Save & Activate and Reset Fan Calibration. Calibration lookup/CRC/NVS and torque-PID execution remain in ESP32 firmware.

On the Overview page, ESP32 `WAIT` telemetry lists only the reported conditions blocking `WAIT` → `READY`. Expand **System Diagnostics** to see all reported safety, reset, V20/Modbus, sensor and Pi signals, the V20 status word, and Modbus error/timeout counters. This display is read-only and requires firmware that sends the optional `diag_*` STATUS fields; older firmware remains compatible and reports diagnostics as unavailable.

## Local development

On **Windows**, install `requirements-desktop.txt`, then use **`python start_windows.py`** instead of starting `desktop.py` alone. The launcher starts the matching local FastAPI backend if port 8000 is free, opens the native Qt HMI, and stops only the backend process it started when the HMI exits. If an incompatible/old server is already on port 8000 it refuses to launch rather than quietly showing a misleading `HMI ONLINE`.

If `Save graph scale` shows **Not Found**, your native app is newer than the running FastAPI process. Locate the backend process using PowerShell `Get-NetTCPConnection -LocalPort 8000 -State Listen`, check the process name/command line before stopping it, then run `python start_windows.py` from the **updated HMI directory**. Never kill an unrelated process.

For manual development, start `python -m uvicorn main:app --host 127.0.0.1 --port 8000 --workers 1`, then `python desktop.py` from a second terminal. For headless GUI smoke tests set `QT_QPA_PLATFORM=offscreen`. Only one backend worker is supported; avoid `--reload` during recording.

## Raspberry Pi deployment

Use `deploy/setup-pi-from-github.sh` for initial installation from `main`, or `deploy/update-pi.sh` for updates. `deploy/install-pi.sh` installs a local systemd backend service and a native PySide6 desktop launcher. Do not expose the backend publicly. All existing measurement data and configuration are retained by the installation/update scripts.

## Physical safety

Remote START/RESET are unavailable. ESP32 operational interlocks, drive commissioning, safety circuit and physical STOP remain authoritative. Successful GUI/API tests do not commission real hardware. OTA remains maintenance-gated.

## API

The local API endpoints remain for Qt, including `/api/status`, `/api/config`, `/api/measurement/*`, `/api/storage/*` and `/api/calibration/fan/*`. These are backend interfaces, not a second browser HMI.

## Offline dynamic identification

Saving calibration also exports `dynamics.json` and `dynamic_transitions.csv`. Settings → Calibration shows measured V20 frequency, filtered Pitot airspeed versus fitted response, K/tau/L, fit quality, residual warnings, data completeness and candidate ISA PI **Kp/Ti**. Lambda is configurable for advisory simulation; it never sends controller gains. The fitted effective plant includes the existing pressure-filter dynamics, so no additional measurement filter is introduced. Unit-step simulation assumes 0–50 Hz limits and a 2 Hz/s drive ramp, which requires physical verification.

The read-only `/api/calibration/fan/runs/{id}/dynamics?lambda_s=1.0` endpoint recalculates candidate tuning/simulation without editing the saved identification or activating hardware. Numerical fit acceptance, independent model validation and hardware commissioning remain separate. Historical runs without dynamics exports still open. See [controller design](../docs/control/TORQUE-CONTROL.md).

Run `python -m unittest discover -s tests -v` with `requirements-desktop.txt` installed to include native Qt tests.
