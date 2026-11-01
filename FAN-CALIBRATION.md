# Fan characteristic calibration and torque AUTO control

Open **Settings → Calibration**. General retains connection information, pressure filtering, flow statistics, and sensor/pressure-tap settings. The requested AUTO airspeed range is 0–40 m/s; the validated V20 limit remains 50 Hz. The achievable airspeed depends on the installation.

## Measurement procedure

1. Commission the existing hardware safety path and MANUAL operation first. Confirm fresh ESP32 READY, stopped V20 feedback, valid Pitot data, and matching filter/density settings.
2. Prepare Calibration selects the existing MANUAL mode and clears its frequency reference. Wait for ARMED, then press physical START. An already RUNNING machine cannot be used.
3. The dedicated Pi asyncio worker sends 10, 15, …, 50 Hz by default. The existing fifth-order MANUAL trajectory performs all acceleration, jerk, and slew limiting; no motor task or control mode was added.
4. Tracking requires applied error ≤0.15 Hz and actual V20 error ≤0.20 Hz. Only after tracking does the 30-second stabilization timeout start. A 3-second window requires standard deviation ≤max(0.15 m/s, 1% mean) and absolute trend ≤0.05 m/s². The worker then collects at least five seconds and 80 distinct Pitot samples, restarting averaging if stability is lost.
5. After the final point the worker requests FREQ 0 and verifies the applied/actual ramp-down. Press physical STOP when prompted. A completed run becomes available for review only after READY with stopped feedback.
6. Inspect the characteristic, statistics and CSV/JSON files. Save & Activate uploads the selected completed run. Activation and reset require fresh READY and stopped feedback at both Pi and ESP32.

The data source is ESP32 `pv`: existing ten-sample pressure averaging, existing pressure low-pass filter, then pressure-to-speed conversion. No Pi low-pass filter is added. `sample_seq` identifies unique samples (including unsigned rollover); repeated/backward sequences do not count. Firmware-reported air density and filter cutoff must match configured settings. Mode, frequency, setpoint and filter edits are serialized and rejected while the calibration worker or deployment owns control. Physical STOP, RESET and emergency stop retain their existing behavior. No network START or RESET is added.

Cancellation requests FREQ 0 only with fresh communication and MANUAL READY/RUNNING, outside emergency stop. FAULT/STOP ends sequencing; hardware safety remains authoritative. Zero frequency is not electrical isolation. Ramp timeout is configurable (180 seconds default); START/STOP waits are limited to ten minutes. The telemetry queue holds at most 128 frames.

## Files and protocol

Each unique `HMI/data/calibrations/<id>/` contains `calibration.json`, `raw_samples.csv`, `characteristic.csv`, and `inverse_lookup.csv`. Raw rows include transitions, measurement phase, sequence, measured quantities and validity. Metadata contains settings, software format version, completion/validation and verified deployment metadata. Failed and cancelled runs are retained and cannot activate. Reset preserves all historical Pi files.

`CAL_BEGIN <version> <count> <crc32>`, indexed `CAL_POINT <index> <velocity_mmps> <frequency_mhz>`, and `CAL_COMMIT` upload a table. `CAL_ABORT` discards staging; `CAL_INFO` is read-only; `CAL_RESET` clears persistent calibration. Version 1 supports 2–32 strictly increasing positive velocity/frequency points, ≤50,000 mHz. CRC32/ISO-HDLC covers little-endian uint16 version/count followed by interleaved uint32 velocity/frequency for each ascending index. CRC is unsigned decimal on the wire. Shared test vector: [(10000,10000),(20000,25000)] → 364007285.

All ESP32 calibration storage, protocol, CRC, validation, interpolation and RAM publication are in `FanCalibration.c`. Upload writes the inactive NVS slot, commits, reads back and validates, then commits the active selector before publishing RAM. Incomplete or rejected uploads preserve the active table. Boot uses only a valid selected table; otherwise DEFAULT. Reset erases both slots and selector, so old data cannot reactivate after reboot. NVS failures do not erase unrelated configuration. The control loop acknowledges a stopped-state gate before persistent mutation and consumes START edges while that gate is held. No NVS work runs in the 200 Hz control loop.

## AUTO calculation

The torque-only branch is integrated on main. DEFAULT retains the complete commissioned MotorModel calculation; calibration absence alone is not a fault. CALIBRATED uses piecewise linear stationary feedforward plus p/(2πKs) × (PID torque + acceleration torque). Stationary fan load is not added again. Below the first measured velocity, the stationary physical model blends continuously into the first measured endpoint. No zero anchor is represented as measured data. Above the highest measured velocity the lookup clamps to its final measured frequency. Final output clamps to 0–50 Hz. Integration that worsens frequency saturation or calibrated-command tracking lag is rejected. The physical torque/slip relationship remains an approximation requiring commissioning.

Telemetry reports cal_valid, cal_active, cal_points, cal_crc, cal_version and cal_source, plus air_density. The HMI takes active state from fresh ESP32 telemetry; a date is displayed only for matching CRC metadata with a verified deployment.

## Verification and commissioning

Run `ESP32/tests/run_host_tests.ps1` with Visual Studio C++ tools, `python -m unittest discover -s tests -v` from HMI with its requirements and httpx installed, Node syntax checks, and the existing PlatformIO build. Host NVS tests simulate storage and reboot; they do not demonstrate real flash power-failure behavior. Async tests simulate physical input events and sensors; they do not validate real aerodynamic stability or timing.

Before physical AUTO operation, verify the motor identity, inertia, pole pairs, airspeed/rotor relationship, local torque-slip slope, frequency feedback/ramp behavior, PID gains and anti-windup, Pitot calibration/density, physical buttons, emergency stop, K2, enable polarity, Modbus watchdog, heartbeat and sensor freshness. `SAFETY_IO_COMMISSIONED`, `V20_RUN_COMMISSIONED`, and `MOTOR_MODEL_TORQUE_TO_V20_ENABLED` retain their safe defaults. Test real reboot/reset/power-interruption persistence and measure 5 ms control jitter during NVS writes before commissioning. No firmware upload or motor operation is performed by the software checks.

## Integration file inventory

Status is relative to the starting local main (A added, M modified, D removed). Torque-branch simulation files are included in the integration.

```text
M	ESP32/README.md
A	ESP32/TORQUE-CONTROL.md
M	ESP32/include/AppConfig.h
A	ESP32/include/FanCalibration.h
A	ESP32/include/MotorModel.h
D	ESP32/include/Regelaar.h
A	ESP32/include/TorqueLoadControl.h
M	ESP32/src/CMakeLists.txt
M	ESP32/src/ControlTask.c
A	ESP32/src/FanCalibration.c
A	ESP32/src/MotorModel.c
M	ESP32/src/PiCommunicationTask.c
D	ESP32/src/Regelaar.c
A	ESP32/src/TorqueLoadControl.c
M	ESP32/tests/host/esp_log.h
A	ESP32/tests/host/nvs.h
A	ESP32/tests/host/nvs_flash.h
M	ESP32/tests/run_host_tests.ps1
M	ESP32/tests/test_control.c
M	ESP32/tests/test_core.c
A	ESP32/tests/test_fan_calibration.c
A	ESP32/tests/test_motor_model.c
A	ESP32/tests/test_torque_load_control.c
A	FAN-CALIBRATION.md
M	HMI/.gitignore
M	HMI/backend/esp32.py
A	HMI/backend/fan_calibration.py
M	HMI/backend/models.py
M	HMI/config/config.json
M	HMI/main.py
M	HMI/static/css/style.css
M	HMI/static/index.html
M	HMI/static/js/app.js
A	HMI/static/js/fan-calibration.js
M	HMI/tests/test_esp32_ethernet.py
A	HMI/tests/test_fan_calibration.py
M	HMI/tests/test_workflow.py
A	Simulation/build_windtunnel_simulink.m
A	Simulation/windtunnel_torque_simulation.m
A	simulation/windtunnel_torque_model.m
```

## Executed software checks

- 23 HMI unittest cases (TCP/parser, calibration worker/data and workflow/API); passed.
- 17 C host executables; passed, including production calibration with mocked NVS and control-gate behavior.
- Seven IO configuration compiler cases; passed.
- ESP32 PlatformIO waveshare_s3 build; passed (no upload).
- Node syntax checks for app.js, charts.js and fan-calibration.js; passed.
- Git whitespace check; passed.

Hardware, real flash power-interruption and physical timing tests were not run.
