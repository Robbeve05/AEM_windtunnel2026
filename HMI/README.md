# Avans Windtunnel — Native PySide6 HMI

The only operator interface is the native Qt application (`desktop.py`). The local FastAPI backend (`main.py`) remains responsible for acquisition, ESP32 Ethernet communication, measurements, calibration sequencing, persistent storage and exports. The browser-based operator frontend has been removed.

## Native interface

Six pages: Overview, Raw Data, Cp Analysis, Measurements, Storage, Settings. Settings has two tabs: General and Calibration. Calibration uses the existing ESP32 MANUAL mode, physical START/STOP and the FastAPI calibration endpoints. It supports configuration of 10–50 Hz, 5 Hz steps and 5-second averaging, live stability/status, plot, history, CSV/JSON export, Save & Activate and Reset Fan Calibration. Calibration lookup/CRC/NVS and torque-PID execution remain in ESP32 firmware.

## Local development

Install `requirements-desktop.txt`, start `uvicorn main:app --host 127.0.0.1 --port 8000 --workers 1`, then launch `python desktop.py`. For headless GUI smoke tests set `QT_QPA_PLATFORM=offscreen`. Only one backend worker is supported; avoid `--reload` during recording.

## Raspberry Pi deployment

Use `deploy/setup-pi-from-github.sh` for initial installation from `main`, or `deploy/update-pi.sh` for updates. `deploy/install-pi.sh` installs a local systemd backend service and a native PySide6 desktop launcher. Do not expose the backend publicly. All existing measurement data and configuration are retained by the installation/update scripts.

## Physical safety

Remote START/RESET are unavailable. ESP32 operational interlocks, drive commissioning, safety circuit and physical STOP remain authoritative. Successful GUI/API tests do not commission real hardware. OTA remains maintenance-gated.

## API

The local API endpoints remain for Qt, including `/api/status`, `/api/config`, `/api/measurement/*`, `/api/storage/*` and `/api/calibration/fan/*`. These are backend interfaces, not a second browser HMI.
