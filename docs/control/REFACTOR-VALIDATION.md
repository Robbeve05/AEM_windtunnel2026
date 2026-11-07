# ISA controller refactor validation — 7 November 2026

## Follow-up: complete file-information layout

The initial layout check only checked section labels and did not require the complete ControlTask-style information block. Following the review, 41 blocks were corrected to include the actual filename, a blank line and a description. On explicit user request, the missing `Author: R.E. van Eekelen` line was also added to 14 files. All 44 application C/H files now have the complete metadata block. The strengthened test requires the actual filename, multiline layout and author line as well as the existing section/include/prototype checks. All 44 files retain exactly the same C tokens as commit `37133cc2788617f29f4a8906a957347986a9e2c2`; only comments changed.

The follow-up host run passed 19/20; Device Guard blocked `indicators` (exit 4551), and the explicit `-Case indicators` retry passed. The stricter layout test passes. Both firmware configurations were rebuilt successfully after the complete metadata correction. HMI code and control behavior were not changed by this follow-up.

Starting commit: `695f95be90536d082ca32906eb808ee9ee0ae34c` on `main`, initially clean and equal to fetched `origin/main`. No feature branch or PR was created. No upload or physical motor test was performed.

## Results on the final implementation

| Check | Actual result |
| --- | --- |
| `ESP32/tests/run_host_tests.ps1` | Final full run: **19/20 passed**, `models` could not execute because Windows Device Guard blocked the executable (exit 4551). No assertion/compile failure occurred in that run. |
| `ESP32/tests/run_host_tests.ps1 -Case models` | **1/1 passed** on the same final production/test sources after the blocked invocation. Thus every one of the 20 final executable cases has executed successfully; the full invocation's nonzero result is not concealed. |
| Earlier complete host runs | All cases passed before the last additional startup/invalid-measurement assertions. Subsequent runs intermittently blocked different executables under Device Guard. |
| HMI `python -m unittest discover -s tests -v` | **56/56 passed**, including all 12 native Qt tests; no skips. |
| Source-layout unittest | **Passed** across all **44** `.c`/`.h` application files in `ESP32/src` and `ESP32/include`. |
| I/O configuration compiler cases | **7/7 passed**: active-high/active-low accepted; invalid board/VFD polarity, negative index, duplicate index and out-of-range index rejected by production assertions. |
| PlatformIO `waveshare_s3` | **Passed**, default torque model, commissioning flags remain disabled. |
| PlatformIO `waveshare_s3_frequency` | **Passed**, `CONTROL_TYPE=1`, no approved installation model or automatic commissioning. |
| Formatting-only infrastructure audit | **123 function bodies in 18 infrastructure modules** lexically identical to the starting commit after discarding whitespace/comments. Includes sensor filtering, quintic curve, I/O, state machine and Modbus functions. |
| `git diff --check` | **Passed**. |
| Dynamic plot | Native Qt rendering exercised by tests and visually inspected with a local offscreen preview. |

The runner now attempts every selected executable and reports all failures before returning nonzero; it does not hide an environmental block. `-Case` explicitly selects named cases for diagnosis/retry. The default still runs all 20.

## Environment failures and resolutions

- `python`/`pio` were absent from PATH. PlatformIO was available at `C:\Users\robbe\.platformio\penv\Scripts\pio.exe`.
- PlatformIO's protected cache/lock files required execution outside the workspace sandbox. Builds were run with the requested permission and did not upload.
- The first Python environment lacked HMI dependencies. A local, ignored `.venv-hmi` based on the bundled Python **3.12** runtime, with `HMI/requirements-desktop.txt`, executed the complete suite. A PlatformIO-Python 3.11 test run stalled in the existing reconnect test and was interrupted; production communication code was not changed to accommodate that environment.
- Device Guard intermittently blocked compiled host executables, including outside the sandbox. A successful earlier complete run and the final complete run plus explicit blocked-case retry are distinguished above. No security policy was disabled or bypassed.
- The I/O overlay test used superseded lamp flags/bit names already unused in the starting production code. It was migrated to the existing actual board polarity/bit definitions. Production control logic/assertions were not weakened.
- An intermediate CMake attempt used `CONFIGURE_DEPENDS`, which ESP-IDF rejects in its component-scan script mode. The final CMake uses an explicit current source list. Intermediate duplicate `IRAM_ATTR` prototype warnings were removed by leaving the attribute on each definition only; the final incremental builds succeeded.
- A calibration export-count assertion was updated from four to the exact six required filenames. The underlying CRC/NVS/deployment workflow tests remain.

## Reconstructed baseline and numerical regression

Initial baseline execution was interrupted by environmental blocks. To preserve an explicit numerical comparison, the original design, inverse and torque-controller sources/headers/tests were subsequently reconstructed **from the unchanged starting Git commit**, built and executed in ignored `.pio/baseline`. All three original numerical tests passed. They were not mistaken for tests of the new modules.

| Quantity/vector | Original and migrated result |
| --- | --- |
| Manufacturer Jm/Jfan/Jtotal | 0.0182 / 0.3530 / 0.3712 kg m² |
| Default theoretical design | Kp=1.77074, Ti=7.95775 s, Td=2.38732 s, Tf=0.0795775 s |
| Full continuous design diagnostics | Phase margin=108.182°, sensitivity error=0.427797 m/s |
| Legacy diagnostic vector J=.3, Kv=.25, v=20, reference acceleration=1, kL=.001 | Acceleration=1.2 Nm, fan load=6.4 Nm, total with zero PID=7.6 Nm |
| Same vector, PID=2 Nm, uncalibrated | Total=9.6 Nm; f=(80+9.6)/π Hz |
| Same vector, calibrated stationary base=20 Hz | f=20+(2+1.2)/π Hz; fan load is already in the calibration base and is not added again |
| Legacy PI Kp=2, Ki=1, Ts=.005; first e=0, next e=2 | ISA Ti=2 s; first feedback=0; next feedback=4.005 |
| ISA PID Kp=2, Ti=2, Td=.5, Tf=.1, Ts=.01; first e=2 then y rises by 1 at constant e | First I=D=0; next I=.02, D=-2/.21; next constant measurement derivative decays by .19/.21 |
| Frequency SIMC K=.8, tau=2, L=.2, lambda=.8 | Kp=2.5 Hz/(m/s), Ti=2 s, Td=0 |
| Upper saturation for 10,000 samples | Integral increments rejected, command recomputed and remains exactly 50 Hz; error reversal immediately recovers below the feedforward command |

Design tolerances are asserted in host tests, not inferred from rounded console output. The continuous design margin is a mathematical template diagnostic, not physical validation of default PI operation.

Tests also cover NONE/P/PI/PID, zero/changing error, setpoint derivative-kick avoidance, reset/parameter changes, NaN/Inf/invalid time constants, single-use integral rejection, missing/invalid calibration, stale versus fresh tracking, upper/lower clamps, physical START/RESET restrictions, MANUAL independence, STOP/emergency faults, lost sensor/Modbus and model initialization gates. The torque integration executable links only the torque model; the frequency executable links only the frequency model, preventing accidental inactive-model calls at link time.

Offline identification regression uses synthetic **measured ramp** trajectories, K=.8, effective tau=.8 s and delay=.4 s, and verifies recovered estimates within the finite search-grid tolerance with R²>0.99. No installation calibration time-series files were present. These are algorithm tests, not measured fan parameters. Static-only estimates, fit acceptance, independent validation and hardware commissioning are separate.

## Remaining commissioning limitations

Manufacturer inertias are supplied data. Motor nameplate/slip/load/Kv and the simplified torque plant are still assumptions requiring installation validation. Frequency K/tau/L defaults are deliberately unavailable (zero K/tau, validation and commissioning disabled), not fabricated from static points. Candidate PI tuning is advisory and never sent to the ESP32 by identification or simulation. Existing safety/V20/torque commissioning flags remain off, and frequency operation has its own approval gate plus calibration requirement. Real aerodynamic response, drive ramps, loop timing, independent hardwired safety, power-interruption persistence and guarded hardware behavior were not tested.

## Exact changed-file inventory

The following paths include additions, modifications and deletions, without rename collapsing. C/H layout-only changes outside the coordinator/models retain the original authorship and behavior. See Git for per-file status and diff.

```text
.gitignore
ESP32/README.md
ESP32/include/AppConfig.h
ESP32/include/ApplicationTasks.h
ESP32/include/BoardConfig.h
ESP32/include/DigitalOutputs.h
ESP32/include/Ethernet.h
ESP32/include/FanCalibration.h
ESP32/include/FrequencyModel.h
ESP32/include/FrequencyPI.h
ESP32/include/I2CBus.h
ESP32/include/MachinePins.h
ESP32/include/MotorModel.h
ESP32/include/OtaSafety.h
ESP32/include/OtaUpdate.h
ESP32/include/PiProtocol.h
ESP32/include/PressureSensors.h
ESP32/include/Regelaar.h
ESP32/include/RegelaarDesign.h
ESP32/include/SafetyIO.h
ESP32/include/SetpointCurve.h
ESP32/include/StateMachine.h
ESP32/include/StatusIndicators.h
ESP32/include/StatusOutput.h
ESP32/include/TaskData.h
ESP32/include/TorqueLoadControl.h
ESP32/include/TorqueModel.h
ESP32/include/V20Modbus.h
ESP32/platformio.ini
ESP32/src/ApplicationTasks.c
ESP32/src/CMakeLists.txt
ESP32/src/ControlTask.c
ESP32/src/DigitalOutputs.c
ESP32/src/Ethernet.c
ESP32/src/FanCalibration.c
ESP32/src/FrequencyModel.c
ESP32/src/FrequencyPI.c
ESP32/src/HeartbeatTask.c
ESP32/src/I2CBus.c
ESP32/src/InputHandlerTask.c
ESP32/src/MotorModel.c
ESP32/src/OtaUpdate.c
ESP32/src/PiCommunicationTask.c
ESP32/src/PiProtocol.c
ESP32/src/PressureSensors.c
ESP32/src/Regelaar.c
ESP32/src/RegelaarDesign.c
ESP32/src/SafetyIO.c
ESP32/src/SensorTask.c
ESP32/src/SetpointCurve.c
ESP32/src/StateMachine.c
ESP32/src/StatusIndicators.c
ESP32/src/TorqueLoadControl.c
ESP32/src/TorqueModel.c
ESP32/src/V20Modbus.c
ESP32/src/main.c
ESP32/tests/host/calibration_stub.c
ESP32/tests/run_host_tests.ps1
ESP32/tests/test_control.c
ESP32/tests/test_io_configuration.py
ESP32/tests/test_models.c
ESP32/tests/test_motor_model.c
ESP32/tests/test_regelaar.c
ESP32/tests/test_regelaar_design.c
ESP32/tests/test_source_layout.py
ESP32/tests/test_torque_design.c
ESP32/tests/test_torque_inverse.c
ESP32/tests/test_torque_load_control.c
FAN-CALIBRATION.md
HMI/README.md
HMI/backend/fan_calibration.py
HMI/backend/fan_dynamics.py
HMI/main.py
HMI/native_calibration.py
HMI/tests/test_fan_calibration.py
HMI/tests/test_fan_dynamics.py
HMI/tests/test_native_hmi.py
HMI/tests/test_workflow.py
README.md
docs/README.md
docs/control/REFACTOR-VALIDATION.md
docs/control/TORQUE-CONTROL.md
```
