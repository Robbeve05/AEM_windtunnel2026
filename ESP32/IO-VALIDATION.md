# Machine I/O software validation - 2026-10-30

Author: R.E. van Eekelen
Baseline main: 05a6936b457fb8cc0bd592b3599f0a96dbcf42a4.

## Actual results

| Check | Result |
| --- | --- |
| Final pio run -e waveshare_s3 | PASS, 5.90 s incremental; 18,420 bytes static RAM, 511,013 bytes program |
| Final pio run -e waveshare_s3_sim | PASS, 5.92 s incremental; 18,404 bytes static RAM, 500,769 bytes program |
| Output configuration compile checks | PASS, 7 cases with the actual Xtensa compiler and ESP-IDF headers |
| Host suite /W4 /WX | Full 13-variant run PASSED during implementation; final full repetitions BLOCKED by Windows Device Guard (see below) |
| Final HMI unittest suite | PASS, 14 tests |
| Browser JS syntax / updated Python AST | PASS |
| Firmware source SHA256 comparison with local build copy | PASS |
| git diff --check | PASS |

Configuration checks compile the enabled and active-low output variants, and require compilation
failure for invalid lamp polarity, invalid K2 polarity, invalid enable value, duplicate output bits
and an out-of-range output index. Test definitions are used only for isolated object compilation;
no firmware is linked or flashed with these flags and all production commissioning gates stay off.

The 13 host variants are indicators, outputs_default, outputs_disabled, outputs_enabled,
outputs_active_low, ota_safety, sensors, core, curve, v20, v20_inhibited, startup and control.
The output tests use real Windows threads, critical sections and mutexes, with mocked I2C replies.
They check that LED staging completes while a K2 write is paused, that the following K2 disable
wins, that LEDs never write the bus or restore K2, and that failed writes remain unconfirmed.
Startup image, both polarities, disabled features, all pin assignments and error cases are checked.
The existing StateMachine, physical START/RESET, OFF1, V20, sensor and curve assertions remain.

The last suite repetitions encountered organization Device Guard blocks at different freshly built
executables, including ota_safety_tests.exe and outputs_enabled_tests.exe. The initial complete run
passed all 13 variants; the strengthened output assertions also passed all four output variants in
a later run before the subsequent OTA executable was blocked. No test assertion failure or firmware
compile failure occurred in these runs. A final single complete host rerun could not be confirmed.
No Device Guard policy, executable name/path or test assertions were changed to bypass this block.
The original fail-on-error runner is retained. This host execution restriction remains to be resolved
by the organization's device policy/support if a new complete run is required.

HMI tests cover authoritative server color flags, existing status text, independence from the disabled
external-lamp feature, output diagnostic types, invalid/incomplete flags, missing/stale status,
unsupported lamp overrides and existing recording/export workflows. An existing recovery unit test
was isolated from the active acquisition recorder after it exposed a close-versus-append race; the
same interrupted-file recovery assertions are preserved. No production recording behavior changed.

## Scope and commissioning limits

Both firmware builds used the same short local build copy as the preceding integration because
ESP-IDF rejects the supplied network path containing spaces. Source hashes verify that firmware
inputs match this repository. PlatformIO espressif32 6.12.0 and ESP-IDF 5.5.0 were retained.
HMI tests used an exact local source copy with isolated dependencies satisfying requirements.txt.
The native Qt source passed Python syntax checks; a live Qt UI or physical Pi was not exercised.
A Starlette test-client deprecation warning is advisory.

ControlTask, StateMachine and InputHandlerTask behavior are unchanged. Existing regulator and V20
configuration values were moved to AppConfig.h without retuning. No new task, queue or EventGroup
was created. External lamps are disabled and safety/V20 commissioning gates remain zero by default.
K2 is still DO1; proposed lamp assignments are DO2 green, DO3 yellow, DO4 red. Electrical polarity,
load supply/current, common terminals, reset/power-up behavior and actual lamp wiring must be verified
against the purchased Waveshare revision/schematic before enabling the feature or commissioning a motor.
No board was flashed and no motor or electrical output was physically tested. See MACHINE-IO.md.

## Actual changed files

- `ESP32/IO-VALIDATION.md`
- `ESP32/MACHINE-IO.md`
- `ESP32/MIGRATION.md`
- `ESP32/include/AppConfig.h`
- `ESP32/include/BoardConfig.h`
- `ESP32/include/DigitalOutputs.h`
- `ESP32/include/I2CBus.h`
- `ESP32/include/MachinePins.h`
- `ESP32/include/StatusOutput.h`
- `ESP32/src/DigitalOutputs.c`
- `ESP32/src/HeartbeatTask.c`
- `ESP32/src/I2CBus.c`
- `ESP32/src/PiCommunicationTask.c`
- `ESP32/src/Regelaar.c`
- `ESP32/src/StatusIndicators.c`
- `ESP32/src/V20Modbus.c`
- `ESP32/tests/host/freertos/FreeRTOS.h`
- `ESP32/tests/host/freertos/semphr.h`
- `ESP32/tests/run_host_tests.ps1`
- `ESP32/tests/test_indicators.c`
- `ESP32/tests/test_io_configuration.py`
- `ESP32/tests/test_outputs.c`
- `HMI/backend/esp32.py`
- `HMI/desktop.py`
- `HMI/static/index.html`
- `HMI/static/js/app.js`
- `HMI/tests/test_esp32_ethernet.py`
- `HMI/tests/test_workflow.py`
