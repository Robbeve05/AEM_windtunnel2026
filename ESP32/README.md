# Windtunnel — ESP32-S3 controller

Firmware for the Avans Aerospace wind tunnel, maintained on the **`main` branch** of [AEM_windtunnel2026](https://github.com/Robbeve05/AEM_windtunnel2026). The ESP32 controls the installation; the Raspberry Pi/Windows native PySide6 HMI is the operator interface (see [HMI/README.md](../HMI/README.md)).

> **Commissioning warning:** A successful build, simulation or HMI connection does not make the installation safe to operate. The independent hardwired emergency-stop/safety circuit must work without the ESP32. Hardware permissions remain disabled until explicitly validated. Do not override commissioning gates to test the fan.

## Architecture

- **Hardware:** Waveshare industrial ESP32-S3-PoE-ETH-8DI-8DO; Ethernet TCP to the Pi, RS-485 Modbus RTU to Siemens SINAMICS V20, I2C to Pitot/pressure-sensor hardware, industrial DI/DO for buttons and the VFD-enable relay.
- **RTOS:** FreeRTOS with an internal GPTimer, **100 Hz (10 ms)** control cycles. Timer alarms alternate sensor and control notifications at **5 ms** phases. The Pitot sensor is sampled at 100 Hz and averaged in groups of five for 20 Hz telemetry. Modbus communication is serviced asynchronously; responses need not complete within one 10 ms control period.
- **Timing faults:** `CONTROL_CLOCK_MISSED_TICKS_ALLOWED` defaults to `1`, tolerating one consecutive missed 100 Hz clock tick. Set it to `0` for strict clock-miss handling. A 50 ms clock timeout, control execution at or above 10 ms, or Modbus service at or above 1 ms still faults immediately; the tolerance does not relax those limits.
- **Startup:** hardware and NVS-backed fan calibration are initialized before application tasks, then queues/event groups and tasks are created. Flash initialization must not overlap the 10 ms control task. The control loop waits for required task-ready bits; those bits indicate local initialization, not live sensor, V20 or Pi health. Failed initialization leaves motor outputs inhibited.
- **Control modes:** MANUAL commands V20 frequency in Hz through a fifth-order ramp; AUTO uses an airspeed reference ramp, torque PID, a one-mass motor/fan model and an inverse motor model to produce V20 Hz. A valid stored fan calibration can supply the base frequency.
- **Operational state machine:** `INIT → WAIT → READY → RUNNING → STOP → FAULT` (transitions are governed by interlocks and commands). The controller publishes state, input and drive diagnostics to the HMI.

The serial console reports startup readiness and task states every five seconds. On a new fault, it also logs the state the controller faulted from, the latched reason, and the safety, sensor, and V20 readiness/communication flags from that fault cycle. Its diagnostics distinguish local task initialization from current hardware health: sensor samples may remain invalid or stale, the V20 may not respond, and the Pi heartbeat may be absent even after the corresponding task/listener initialized. Ethernet link/IP, each raw DI1-DI8 level, interpreted input function and configured polarity, commissioning flags, machine state/fault, output image and status indication are included. The onboard RGB LED adds a brief blue pulse each second while fresh control-cycle notifications are being processed; the existing red/amber/green colors continue to show machine status. External lamps follow the same state indication: DO2 green, DO3 red, DO4 yellow/amber. Output-image acknowledgement is not physical contact feedback. These reports do not issue output or motor commands.

### Main firmware files

| File | Responsibility |
| --- | --- |
| `src/main.c` | Calls `StartApplicationTasks()` |
| `src/ApplicationTasks.c` | Creates hardware services, queues, event groups and FreeRTOS tasks |
| `src/InputHandlerTask.c` | Reads/validates buttons, applies VFD-enable relay request |
| `src/SafetyIO.c` | Reads safety-related DI, interrupt latches, fault/stop signals and simulated inputs |
| `src/SensorTask.c` | Acquires and publishes sensor samples |
| `src/ControlTask.c` | State machine coordination, references, torque controller, drive requests and diagnostics |
| `src/PiCommunicationTask.c` | Ethernet TCP text commands, heartbeats and status telemetry |
| `src/FanCalibration.c` | Stores and retrieves the calibrated fan characteristic in NVS |
| `include/AppConfig.h` | Operating limits, timing, model parameters and commissioning flags |

Other drivers and public headers are organized under `src/` and `include/`; host tests are in `tests/`. For module-specific detail see [Torque control](../docs/control/TORQUE-CONTROL.md), [V20 Modbus](../docs/hardware/V20_MODBUS.md), [Machine IO](../docs/hardware/MACHINE-IO.md), and [OTA setup](../docs/deployment/OTA-SETUP.md). Historical migration notes were removed because they contain superseded IO assignments.

### FreeRTOS tasks

| Task | Core | Priority | Stack (FreeRTOS units) |
| --- | ---: | ---: | ---: |
| InputHandlerTask | 0 | 5 | 3072 |
| SensorTask | 0 | 3 | 4096 |
| ControlTask | 1 | 4 | 6144 |
| PiCommunicationTask | 0 | 2 | 8192 |
| HeartbeatTask | 0 | 1 | 3072 |
| OtaUpdateTask | 0 | 1 | 8192 |
| OtaMarkValidTask | 0 | 1 | 3072 |

## Physical inputs and safety

**Intended wiring from the electrical schematic dated 30 October 2026** (verify signal levels and terminals before commissioning):

| Waveshare terminal / GPIO | Schematic function |
| --- | --- |
| DI1 / GPIO4 | Emergency stop indication |
| DI2 / GPIO5 | Reset |
| DI3 / GPIO6 | Start |
| DI4 / GPIO7 | Stop |
| DI5 / GPIO8 | Safety feedback |

The Waveshare board documentation maps DI1-DI8 in order to GPIO4-GPIO11. The schematic's DI order is reflected in `MachinePins.h`. Current polarity settings reflect the reported observation that 24 V at DI corresponds to GPIO low: E-stop pressed, GPIO high; Reset/Start pressed, GPIO low; Stop and safety feedback healthy, GPIO low. These settings are provisional until individually verified through physical commissioning; `SAFETY_IO_COMMISSIONED` remains disabled. DI6 and DI7 are not assigned in the schematic and their optional VFD-fault/contactor features remain disabled. Emergency stop and safety faults must latch; Stop requests controlled deceleration where the drive remains healthy. After an E-stop is released, physical RESET acknowledges the latch and returns the state machine to WAIT only; READY remains blocked until all commissioning, sensor, drive and other permissive checks pass. Reset does not automatically restart the machine. Physical START/RESET are required, not remote actions.

On schematic sheet 11, DO1 drives relay 11K3; its contact supplies the VFD In1 Enable signal shown on sheet 3. This is the operational VFD-enable relay, not the independent emergency-stop safety relay. DO2 and DO3 connect to indicators 10SH5 and 10SH6. Based on the operator's lamp test, firmware maps DO2 to green, DO3 to red, and DO4 to yellow/amber. Waveshare specifies opto-isolated Darlington outputs with up to 500 mA sink current. Thus DOx is a current-sinking output, not a +24 V source; the actual current path must be compatible with the field device and the board's COM/DGND connections. The monitor reported an acknowledged image of `0x00` while the DO indicators and stack lights were illuminated, so firmware now treats expander logic low as active and initializes the bank to inactive logic high. Verify indicator behavior with the motor/VFD isolated; software image acknowledgement is not physical output/contact feedback.

**The ESP32 ISR and VFD-enable relay command are not safety-rated functions.** The independent electrical safety circuit must remove hazardous actuation regardless of MCU execution, Ethernet, RTOS or Modbus. In particular, do not mistake an OFF1 controlled stop for a safety-rated emergency stop.

## Compile-time feedback-controller selection

Set `CONTROLLER_SETTING` in `include/AppConfig.h`:

```c
#define CONTROLLER_SETTING CONTROLLER_PI
```

| Value | AUTO torque feedback | Feedforward |
| --- | --- | --- |
| `CONTROLLER_NONE` | No P, I or D | Acceleration and fan-load model remain active |
| `CONTROLLER_P` | Proportional only | Active |
| `CONTROLLER_PI` | Proportional and integral (default) | Active |
| `CONTROLLER_PID` | Proportional, integral and filtered derivative | Active |

Selection is enforced in `TorqueLoadControl_Step()`, so commands that update stored PID gains cannot activate disabled terms. It does **not** affect MANUAL frequency control and **does not** bypass physical AUTO commissioning gates. `NONE` means no feedback correction, not a stopped fan or disabled feedforward. Rebuild/flash firmware after changing the setting. These modes have not been physically tuned or commissioned.

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

The dedicated Pi link uses static IPv4: ESP32 `192.168.50.2/24`, Raspberry Pi `192.168.50.1/24`, with no gateway or DHCP. The ESP32 TCP listener binds `0.0.0.0:5000`; the Pi initiates the connection. The line-oriented command protocol refreshes the application heartbeat only when a valid `PING` is received. Telemetry includes `ethernet_link`, `ipv4_configured`, `pi_tcp_connected`, `pi_heartbeat_received`, and `pi_heartbeat_fresh`; the legacy `pi_connected` field continues to mean heartbeat-dependent controller health. A TCP socket or configured IP alone is not an application-level online indication. The HMI should require a live TCP session and fresh heartbeat/status before reporting the controller online. The ESP32 reports, among other fields, machine state and fault, individual input conditions, Pitot measurement and age, V20 frequency/faults, calibration metadata, controller timing/jitter, missed deadlines, Modbus errors and connection state. In WAIT, `diag_blockers` is a bitmask from the same safety/process/reset predicates used by the WAIT-to-READY transition; the `diag_*` fields expose current signal values, including the raw V20 status word and the separate software RUN commissioning gate. Serial WAIT summaries print on WAIT entry, blocker changes, and every `WAIT_DIAGNOSTICS_INTERVAL_MS`. These fields are read-only and do not change transitions or issue RUN/START/RESET commands. The Pi can request mode, setpoint, frequency, filter and calibration operations when allowed; physical safety and start/reset interlocks remain authoritative. Connection loss requests a stop and clears pending commands.

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
5. Perform controlled MANUAL tests at low frequency, confirming VFD-enable relay handling, ramp profiles, current, direction, fan coast-down and safe stopping.
6. Collect frequency-versus-airspeed data, validate the stored calibration and motor/fan dynamics, then evaluate AUTO in simulation and separate guarded physical tests before changing commissioning flags.
7. Commission the ABP2 pressure-tap channels and Cp measurement plumbing only after their hardware and calibration are verified.

**Do not treat this README as electrical approval or a safety assessment.** Physical commissioning requires qualified personnel and documented verification of the installed hardware.
