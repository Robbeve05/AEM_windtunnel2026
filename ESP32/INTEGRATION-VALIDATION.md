# Integration software validation - 2026-10-30

Author: R.E. van Eekelen

Integration branch: integration/final-main-firmware.
Latest fetched integration baseline: b6753ad7e843fcc7da5d5ecfb465e4cd1ac7b240 (origin/main).

## Executed checks

| Check | Actual result |
| --- | --- |
| pio run -e waveshare_s3 | PASS; ESP-IDF 5.5.0, espressif32 6.12.0; 99.30 s |
| pio run -e waveshare_s3_sim | PASS; same platform; 97.39 s |
| Hardware firmware memory | 18,388 bytes static RAM; 508,997 bytes program; 509,408 byte image |
| Simulation firmware memory | 18,372 bytes static RAM; 498,717 bytes program; 499,120 byte image |
| OTA enabled reception compile-only | PASS with real Xtensa compiler and ESP-IDF headers; isolated test definitions; no link or flash |
| ESP32/tests/run_host_tests.ps1 | PASS; 8 variants, production sources, Visual Studio /W4 /WX |
| HMI python -m unittest discover -s tests -v | PASS; 13 tests |
| Browser JavaScript node --check | PASS |
| Updated Python AST syntax checks | PASS |
| git diff --check | PASS |
| Requested source consistency searches | PASS; no old 20 Hz manual limit, linear manual ramp or duplicate legacy task introduced |
| Production OTA, safety and V20 commissioning defaults | Disabled; no key supplied |

Host variants: ota_safety, sensors, core, curve, v20, v20_inhibited, startup, control.
They cover physical maintenance/isolation, default OTA disable, sensor calibration and timeout,
phased I2C, 0..50 Hz protocol, malformed/NaN/infinite values, analytic quintic derivatives,
manual PI bypass, retained AUTO PI, deferred retargeting, mode interlocks, fresh physical START,
RESET, OFF1, fault latching/bypass, startup synchronization and drive feedback failures.
HMI coverage includes model/adapter limits, full command protocol, disconnected/stale behavior,
recording/export and a real simulated Cp calculation with the real-measurement gate retained.

## Build environment and scope

ESP-IDF rejects spaces in the supplied network project path. Both successful builds therefore used
an exact temporary source copy in a path without spaces. SHA256 source comparison confirmed that
all firmware build inputs matched the repository. Required compiler/IDF packages were installed.
The original direct-path build failed before compilation due solely to that path restriction.

HMI tests used an exact local source copy and isolated temporary dependencies because the checked-out
venv points at a missing Python executable and network debug line loading disturbed short TCP deadlines.
Dependencies satisfy HMI/requirements.txt. A Starlette test-client deprecation warning remains advisory.
One repeated host run was transiently blocked by Windows Device Guard. The exact same script then
completed all 8 variants successfully; no security policy, executable name or execution path was changed.

The enabled OTA object used test-only authorization definitions to compile conditional code. It was not
linked into either firmware. Actual builds retain all disabled production defaults and contain no secret.

No board was flashed and no physical motor, VFD, Ethernet installation, pressure sensor or OTA transfer
was tested. Runtime deadlines, wiring/polarities, pitot calibration, optional pressure-tap part/scaling,
50 Hz motor/fan nameplates and V20 settings, OFF1 deceleration, K2 DO1, emergency hardware,
maintenance isolation and rollback still require physical commissioning. See INTEGRATION-FINAL.md.
There are no remaining software-validation blockers in the executed checks.

## Actual files changed

- `ESP32/.gitignore`
- `ESP32/INTEGRATION-FINAL.md`
- `ESP32/INTEGRATION-VALIDATION.md`
- `ESP32/OTA-SETUP.md`
- `ESP32/V20_MODBUS.md`
- `ESP32/include/AppConfig.h`
- `ESP32/include/BoardConfig.h`
- `ESP32/include/I2CBus.h`
- `ESP32/include/OtaSafety.h`
- `ESP32/include/OtaUpdate.h`
- `ESP32/include/PiProtocol.h`
- `ESP32/include/PressureSensors.h`
- `ESP32/include/SetpointCurve.h`
- `ESP32/include/TaskData.h`
- `ESP32/partitions.csv`
- `ESP32/platformio.ini`
- `ESP32/sdkconfig.defaults`
- `ESP32/src/ApplicationTasks.c`
- `ESP32/src/CMakeLists.txt`
- `ESP32/src/ControlTask.c`
- `ESP32/src/HeartbeatTask.c`
- `ESP32/src/I2CBus.c`
- `ESP32/src/OtaUpdate.c`
- `ESP32/src/PiCommunicationTask.c`
- `ESP32/src/PiProtocol.c`
- `ESP32/src/PressureSensors.c`
- `ESP32/src/SensorTask.c`
- `ESP32/src/SetpointCurve.c`
- `ESP32/src/V20Modbus.c`
- `ESP32/tests/host/driver/i2c.h`
- `ESP32/tests/host/freertos/portmacro.h`
- `ESP32/tests/run_host_tests.ps1`
- `ESP32/tests/test_control.c`
- `ESP32/tests/test_core.c`
- `ESP32/tests/test_curve.c`
- `ESP32/tests/test_ota_safety.c`
- `ESP32/tests/test_sensors.c`
- `ESP32/tests/test_startup.c`
- `HMI/backend/esp32.py`
- `HMI/backend/measurements.py`
- `HMI/backend/models.py`
- `HMI/desktop.py`
- `HMI/main.py`
- `HMI/static/index.html`
- `HMI/static/js/app.js`
- `HMI/tests/test_esp32_ethernet.py`
- `HMI/tests/test_workflow.py`
