# Avans Windtunnel — Aerospace Engineering & Maintenance

Software and documentation for the Avans Aerospace wind tunnel project. The system measures airflow and airfoil pressure distributions and provides an operator interface for experimental measurements and Cp analysis.

> **Development / commissioning:** Software tests do not validate electrical safety. The independent hardwired emergency-stop circuit, motor protection and Siemens V20 commissioning must be verified on the physical installation before operation.

## System overview

| Component | Purpose | Documentation |
| --- | --- | --- |
| Waveshare industrial ESP32-S3 | FreeRTOS machine controller, input supervision, sensor acquisition, fan control | [ESP32 firmware](ESP32/README.md) |
| Siemens SINAMICS V20 | Frequency converter for the fan, controlled through RS-485 Modbus RTU | [V20 protocol and configuration](ESP32/V20_MODBUS.md) |
| Raspberry Pi / Windows | Native PySide6 operator interface, local FastAPI backend, data recording, calibration and export | [HMI](HMI/README.md) |
| Pitot and pressure sensors | Airspeed feedback and experimental pressure measurements | [ESP32 architecture](ESP32/README.md) |

**High-level flow:** native HMI ↔ local FastAPI backend ↔ Ethernet TCP ↔ ESP32-S3 ↔ Modbus RTU ↔ V20/fan. The ESP32 also receives industrial inputs and sensor data. The emergency-stop circuit is independent of this communication chain.

## Functions

- **MANUAL:** set V20 frequency (0–50 Hz) using a bounded fifth-order frequency profile.
- **AUTO:** requested airspeed → fifth-order reference → torque PID and motor/fan model → V20 frequency; physical AUTO is not yet commissioned.
- **Measurements:** Pitot readings, recording, history, processing, exports and Cp-oriented analysis.
- **Fan calibration:** frequency sweep and storage of a characteristic in ESP32 NVS.
- **Diagnostics:** live machine state, sensor and drive conditions, communication, timing and fault information.

## Repository layout

```text
AEM_windtunnel2026/
├── README.md                   Project overview (this page)
├── docs/
│   └── README.md               Documentation map and status
├── ESP32/
│   ├── README.md               Firmware architecture, safety and build
│   ├── src/                    Firmware implementations
│   ├── include/                Public headers and configuration
│   ├── tests/                  Host-based regression tests
│   ├── TORQUE-CONTROL.md        Control-model detail
│   ├── V20_MODBUS.md           Modbus and drive detail
│   └── MIGRATION.md            Historical migration and commissioning notes
└── HMI/
    ├── README.md               Native UI, backend and deployment
    ├── desktop.py              PySide6 operator application
    ├── main.py                 FastAPI backend
    ├── backend/                HMI services, acquisition and storage
    └── deploy/                 Raspberry Pi deployment scripts
```

## Getting started

**Windows HMI** (from the `HMI/` directory):

```powershell
pip install -r requirements-desktop.txt
python start_windows.py
```

The launcher starts the local API backend when needed. See [HMI/README.md](HMI/README.md) for details and Raspberry Pi setup.

**ESP32 firmware** (from `ESP32/`, using PlatformIO):

```bash
pio run -e waveshare_s3
pio run -e waveshare_s3_sim
```

Host regression tests on Windows: `./tests/run_host_tests.ps1`. Hardware commissioning is **not** implied by compiling or passing tests.

## Documentation

See the [documentation index](docs/README.md) for active engineering references, dated records, and obsolete Arduino/browser-HMI instructions. Do not use archived notes as current wiring or deployment instructions.

## Operational limitations

- The physical start/reset inputs and independent safety circuit remain authoritative; remote START/RESET are not operator functions.
- Confirm the **current electrical drawing and actual board polarity** before wiring DI1–DI5. Some historical migration notes still use an old mapping.
- `SAFETY_IO_COMMISSIONED=0`, `V20_RUN_COMMISSIONED=0` and `MOTOR_MODEL_TORQUE_TO_V20_ENABLED=0` are documented uncommissioned settings; verify the actual build configuration.
- The 16-channel pressure-tap system requires confirmed PCB sensor addresses, ranges and physical commissioning.

This repository documents an educational engineering project, not a certified safety controller.
