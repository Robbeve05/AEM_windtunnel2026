# Windtunnel — ESP32-S3 controller

Firmware for the Avans Aerospace wind tunnel, maintained on the **`main` branch** of [AEM_windtunnel2026](https://github.com/Robbeve05/AEM_windtunnel2026). The ESP32 controls the installation; the Raspberry Pi/Windows native PySide6 HMI is the operator interface (see [HMI/README.md](../HMI/README.md)).

> **Commissioning warning:** A successful build, simulation or HMI connection does not make the installation safe to operate. The independent hardwired emergency-stop/safety circuit must work without the ESP32. Hardware permissions remain disabled until explicitly validated. Do not override commissioning gates to test the fan.

## Architecture

- **Hardware:** Waveshare industrial ESP32-S3-PoE-ETH-8DI-8DO; Ethernet TCP to the Pi, RS-485 Modbus RTU to Siemens SINAMICS V20, I2C to Pitot/pressure-sensor hardware, industrial DI/DO for buttons and operational K2 enable.
- **RTOS:** FreeRTOS with an internal GPTimer, **200 Hz (5 ms)** control cycles. Timer alarms alternate sensor and control notifications at **2.5 ms** phases. Modbus communication is serviced asynchronously; a 5 ms control period is not a 5 ms Modbus round-trip guarantee.
- **Startup:** hardware, queues and event groups are created before tasks. The control loop waits for required task-ready bits; those bits indicate local initialization, not live sensor, V20 or Pi health. Failed initialization leaves motor outputs inhibited.
- **Control modes:** MANUAL commands V20 frequency in Hz through a fifth-order ramp; AUTO uses an airspeed reference ramp, torque PID, a one-mass motor/fan model and an inverse motor model to produce V20 Hz. A valid stored fan calibration can supply the base frequency.
- **Operational state machine:** `INIT → WAIT → READY → RUNNING → STOP → FAULT` (transitions are governed by interlocks and commands). The controller publishes state, input and drive diagnostics to the HMI.

### Main firmware files

| File | Responsibility |
| --- | --- |
| `src/main.c` | Calls `StartApplicationTasks()` |
| `src/ApplicationTasks.c` | Creates hardware services, queues, event groups and FreeRTOS tasks |
| `src/InputHandlerTask.c` | Reads/validates buttons, applies K2 operational output request |
| `src/SafetyIO.c` | Reads safety-related DI, interrupt latches, fault/stop signals and simulated inputs |
| `src/SensorTask.c` | Acquires and publishes sensor samples |
| `src/ControlTask.c` | State machine coordination, references, torque controller, drive requests and diagnostics |
| `src/PiCommunicationTask.c` | Ethernet TCP text commands, heartbeats and status telemetry |
| `src/FanCalibration.c` | Stores and retrieves the calibrated fan characteristic in NVS |
| `include/AppConfig.h` | Operating limits, timing, model parameters and commissioning flags |

Other drivers and public headers are organized under `src/` and `include/`; host tests are in `tests/`. For module-specific detail see [Torque control](../docs/control/TORQUE-CONTROL.md) and [V20 Modbus](../docs/hardware/V20_MODBUS.md). Historical migration notes were removed because they contain superseded IO assignments.

### FreeRTOS tasks

| Task | Core | Priority | Stack (FreeRTOS units) |
| --- | ---: | ---: | ---: |
| InputHandlerTask | 0 | 5 | 3072 |
| SensorTask | 0 | 3 | 4096 |
| ControlTask | 1 | 4 | 6144 |
| PiCommunicationTask | 0 | 2 | 6144 |
| HeartbeatTask | 0 | 1 | 3072 |
| OtaUpdateTask | 0 | 1 | 8192 |
| OtaMarkValidTask | 0 | 1 | 3072 |

## Physical inputs and safety

**Intended wiring from the electrical schematic dated 30 October 2026** (verify actual firmware pin definitions, signal levels, terminals and wiring before commissioning):

| Industrial input | Function | Contact | Intended signal when healthy / idle | Operated state |
| --- | --- | --- | --- | --- |
| DI1 | Emergency stop indication | NC | 24 V | Open / 0 V |
| DI2 | Reset | NO | 0 V | 24 V |
| DI3 | Start | NO | 0 V | 24 V |
| DI4 | Stop | NC | 24 V | Open / 0 V |
| DI5 | Safety feedback | NC | 24 V | Open / 0 V |

These values describe the field-side signals, **not guaranteed ESP32 GPIO logic levels**: verify optocoupler polarity, physical pins and configuration on the actual Waveshare board. Emergency stop and safety faults must latch; Stop requests controlled deceleration where the drive remains healthy. Reset does not automatically restart the machine. The current button handler debounces Start on press and Reset on release. Physical START/RESET are required, not remote actions.

**The ESP32 ISR and K2 command are not safety-rated functions.** The independent electrical safety circuit must remove hazardous actuation regardless of MCU execution, Ethernet, RTOS or Modbus. In particular, do not mistake an OFF1 controlled stop for a safety-rated emergency stop.

## Control and calibration

**MANUAL:** enter a requested frequency from 0 to 50 Hz. The output follows a separate fifth-order frequency profile with slew, acceleration and jerk limits. MANUAL does not close the airspeed feedback loop.

**AUTO:** airspeed setpoint (m/s) → fifth-order reference → torque PID correction and inertia/load feedforward → inverse induction-motor approximation → V20 frequency → fan → Pitot feedback. The frequency command is bounded; motor and drive dynamics still require physical verification.

For RUCK EL 710 D4 02, article 146531, the confirmed rotating inertias used in the theoretical one-mass model are:

- Motor: `J_MOTOR_KGM2 = 0.0182 kg·m²`
- Impeller: `J_FAN_KGM2 = 0.3530 kg·m²`
- Total: `J_TOTAL_KGM2 = 0.3712 kg·m²`

The motor nameplate, slip gain, fan load coefficient, airflow dynamics and frequency-to-torque behavior are **not yet fully validated**. Accordingly, physical AUTO remains blocked by `MOTOR_MODEL_TORQUE_TO_V20_ENABLED=0`; `SAFETY_IO_COMMISSIONED=0` and `V20_RUN_COMMISSIONED=0` are also documented as uncommissioned defaults. Check the actual values in the build before testing.

**Fan calibration:** the HMI guides a MANUAL-mode frequency sweep using the physical controls, stability checks and averaged Pitot readings. A characteristic can then be deployed to the ESP32, checked by version/CRC and retained in NVS. `cal_valid` indicates an accepted stored curve; the currently transmitted `cal_active` also reports `cal.valid` (curve available/used by AUTO), **not** whether a calibration session is running. The HMI calibration workflow has its own execution state. `CAL_INFO` uses the same convention.

The planned 16-channel ABP2 pressure-tap acquisition remains gated pending verified PCB hardware, sensor addresses and calibration. The I2C address clash between the upstream Pitot and default ABP2 configuration must be resolved; a mux selection alone does not isolate the upstream device.

## HMI, communication and diagnostics

The native [PySide6 HMI](../HMI/README.md) operates on Raspberry Pi or Windows with a local FastAPI backend. Its pages are Overview, Raw Data, Cp Analysis, Measurements, Storage and Settings (General/Calibration). The browser-based operator UI has been removed.

The ESP32 communicates via Ethernet TCP using line-oriented text commands and key/value status telemetry. It reports, among other fields, machine state and fault, individual input conditions, Pitot measurement and age, V20 frequency/faults, calibration metadata, controller timing/jitter, missed deadlines, Modbus errors and connection state. The Pi can request mode, setpoint, frequency, filter and calibration operations when allowed; physical safety and start/reset interlocks remain authoritative. Connection loss requests a stop and clears pending commands.

## Build and tests

From `ESP32/`, in a local directory without spaces:

```bash
pio run -e waveshare_s3
pio run -e waveshare_s3_sim
```

Windows host regression tests:

```powershell
./tests/run_host_tests.ps1
```

These tests use mocked hardware; they include V20 asynchronous responses, OFF1/stop confirmation, faults and recovery. A passing test suite cannot validate relay wiring, GPIO voltage levels, drive configuration, braking/coast behavior or functional safety.

## Commissioning checklist

1. Verify electrical drawings against physical NC/NO switches, terminal numbers, DI mapping, optocoupler polarity and an independent hardwired safety circuit.
2. With the motor independently inhibited, confirm boot and startup barrier, default-safe outputs, DI transitions, physical Start/Stop/Reset and FAULT/reset behavior.
3. Verify Pitot calibration, valid/stale sensor detection, networking and loss-of-communication response.
4. Configure and verify the Siemens V20 registers, motor nameplate parameters, Modbus status/frequency scaling, watchdog and OFF1 behavior.
5. Perform controlled MANUAL tests at low frequency, confirming K2 handling, ramp profiles, current, direction, fan coast-down and safe stopping.
6. Collect frequency-versus-airspeed data, validate the stored calibration and motor/fan dynamics, then evaluate AUTO in simulation and separate guarded physical tests before changing commissioning flags.
7. Commission the ABP2 pressure-tap channels and Cp measurement plumbing only after their hardware and calibration are verified.

**Do not treat this README as electrical approval or a safety assessment.** Physical commissioning requires qualified personnel and documented verification of the installed hardware.
