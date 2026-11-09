## 16. Implementation and validation update

Updated 8 November 2026. Sections 1-15 preserve the original baseline audit. Their line numbers, flag values and FAIL assessment describe the old revision. This section describes the corrected working tree.

**Current assessment: CONDITIONAL software validation; NOT READY for physical operation.** Software corrections are implemented and tested. D4/D7 installation evidence, maximum RTU latency, independent safety behavior and actual controller identification/calibration remain outstanding. A disconnected motor is a bench condition, not installation verification.

### Current configuration and subsequent instructions

The operator superseded the initial instruction to preserve commissioning flags, requesting bench release with the motor disconnected. The final correction was frequency model ON, torque model OFF.

| Setting | Current default | Limitation |
| --- | --- | --- |
| CONTROL_TYPE | CONTROL_FREQUENCY_PI | Standard controller selection |
| SAFETY_IO_COMMISSIONED | 1 | Requested bench release, not a measurement record |
| V20_RUN_COMMISSIONED | 1 | Allows operational writes; physical START still required |
| FREQUENCY_MODEL_VALIDATED / COMMISSIONED | 1 / 1 | Numeric model and calibration checks remain |
| MOTOR_MODEL_TORQUE_TO_V20_ENABLED | 0 | Torque AUTO blocked |
| V20_FAULT_ACK_VERIFIED | 0 | New fault acknowledgment disabled until installed configuration verified |
| V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED | 0 | Unknown mapping not asserted as verified |
| VFD_FAULT_DI_FITTED / CONTACTOR_FEEDBACK_FITTED | 0 / 0 | No contacts invented |
| OTA_MAINTENANCE_ENABLED | 0 | Unchanged |

No V20 parameters were modified, firmware uploaded or physical outputs actuated. Frequency gain/time-constant defaults remain zero and valid stored fan calibration is still required. Missing model/calibration reports CONFIGURATION without arming. Permissions alone do not make AUTO usable. Manual frequency has its own physical/readiness prerequisites.

### Current startup contract

`INIT -> WAIT -> STOPPED_PREPARE -> READY -> STARTING -> RUNNING -> STOP -> READY`, with latched fault branches.

1. Recoverable output/sensor initialization is retried. A failed task barrier reports INITIALIZATION and resumes INIT when services recover, without accepting RESET/START. UART setup failures are retried while disarmed, with partial-install cleanup. A responding faulted drive can reach WAIT for deliberate recovery.
2. Deliberate physical RESET release acknowledges restored physical safety latches through the existing generation-protected API before recovery evaluation. Unsafe edges remain dominant. Restoration alone never resets or arms.
3. The operator clarified intended V20 In1=OFF2. STOPPED_PREPARE may supply DO1 operational permission after RESET and an acknowledged OFF1 in the new reset/recheck history, under the existing commissioned/live safety gates. Repeated OFF1 heartbeats preserve established permission while awaiting their echo. No electrical polarity or BICO value is invented.
4. READY requires a status-read request after acknowledged OFF1, disabled operation and speed <=0.1 Hz. Healthy READY retains operational relay permission. Preparation times out after 5 s and never sends ON.
5. A fresh physical START enters STARTING, arms the centralized driver and sends RUN at zero setpoint, with profiles/integrators disabled and running=0. A validated status read requested after RUN acknowledgment must report operation-enabled before RUNNING. Timeout is 5 s; STOP/unsafe conditions interrupt startup.
6. RUNNING enables controller output. Unexpected loss of enabled feedback revokes authorization and reports drive inhibit/state failure. Power interruptions entirely between polls cannot be proven detectable; hardware restart settings remain mandatory.
7. STOP disarms, sends OFF1 and retains healthy relay permission through the ramp. Post-acknowledgment disabled/low-frequency feedback and STOP release permit READY. FAULT removes the relay request independently of bounded software STOP attempts.

### Implementation and validation matrix

| Defect | Correction and software evidence | Remaining hardware/limits |
| --- | --- | --- |
| D1 | Explicit stopped preparation and STARTING; fresh post-RUN enabled confirmation, zero start setpoint, start timeout, STOP during start and unexpected enable-loss tests | Actual status mapping and zero-setpoint enabled behavior |
| D2 | Physical RESET acknowledgment precedes recovery; real SafetyIO/ControlTask tests cover released-but-latched E-stop, no RESET, active-E-stop rejection and recovery without arm | Polarity, authentic feedback and physical edge behavior |
| D3 | Persistent RUN lock separated from current transport health; bounded OFF1 attempts outrank polling; restored responses do not re-arm. RX/TX/lost-response/recovery/retry-exhaustion/outstanding-RUN tests | Independent watchdog/relay action if transport never recovers |
| D5 | Gated RequestFaultAck sends 0x04FE then 0x047E with ON=0, checks current safety, writes no parameters. Tests compile permission 0 and 1; cleared/persistent faults and unsafe cancellation | Default permission 0 until installed V20/CDS behavior verified; local drive reset otherwise needed |
| D6 | Separate DRIVE_INHIBIT, CONFIGURATION, MODBUS, VFD, START_TIMEOUT and INITIALIZATION reasons; communication/run lock and warning/ack/attempt fields exposed separately; first cause retained; independent reason/parser tests | Actual fault code and inhibit source |
| D8 | Output recovery publishes readiness; inactive output init and sensor init retry; task barrier resumes INIT; UART cleanup/retry. First-failure-then-success and barrier tests pass | Allocation/assert failures may require reboot; physical bus faults |
| D9 | Independent V20 model and coupled real-driver/state-machine simulation cover relay permission, ON edge, OFF1/2/3, inhibit, enable, acknowledgment/persistent faults and separate ramps | Illustrative rates are not real fan/V20 dynamics |
| D10 | Millisecond safety telemetry; fresh/stale boundaries tested with deliberately different RTOS tick value | Target clock verification |
| D12 | Failed transactions enter 160 ms receive-draining quarantine; received bytes extend it. Feedback confirmation uses read-request ordering after command echo. Injected late valid status cannot confirm operation | RTU cannot identify arbitrarily late identical replies; verify actual maximum slave/bridge latency against bound |
| D4 | Intended relay/OFF2 sequence represented in preparation/READY after operator clarification; coupled tests cover OFF1 acknowledgment prerequisite and repeated heartbeats | As-built contacts, DI/BICO mapping, polarity and independent safety path unverified |
| D7 | No invented mapping/value or automatic parameter writes | P2014/40001/40002 action and refresh semantics, continued-read command loss and coast response unverified |

The existing 1024-byte STATUS buffer could never satisfy its diagnostic append guard. A single-task-owned 6144-byte static buffer now accommodates diagnostics and rejects overflow. Hardware documentation reflects FC03/FC16 and no FC06 watchdog configuration.

### Final validation results

| Check | Final result | Evidence |
| --- | --- | --- |
| Complete MSVC host suite, /W4 /WX | 23/23 executables PASS | output/v20-final-host.log |
| Production/simulation/frequency firmware | 3/3 builds PASS | output/v20-final-build.log |
| Positive/negative I/O configuration compilation | 7 PASS | output/v20-final-io.log |
| Source layout | 1 PASS | output/v20-final-layout.log |
| HMI/offscreen Qt | 104 PASS | output/v20-final-hmi.log |
| Deployment migration | 7 PASS | output/v20-migration.log |
| Existing MATLAB/Simulink scripts | Not verified; further checks cancelled by operator | output/v20-matlab.log, output/v20-matlab-smoke.log |

New host cases are v20_recovery, v20_fault_ack and v20_simulation. They test in-flight/stale RUN after STOP, delayed/dropped responses, bounded retries, persistent run lock, post-command feedback, MCU reboot with stale RUN, modeled drive enable loss, independent stop sources and acknowledgment gates. Existing physical START/RESET network rejection remains tested. Recovery of output/sensor initialization and task readiness does not accept START or RESET automatically.

Some initial executables received Windows Device Guard exit 4551. Subsequent executions passed without changing security settings. HMI validation also reproduced an existing Windows atomic-file sharing race: measurement JSON now uses an owned same-directory temporary file and bounded retry only for Windows access/sharing errors. Permanent denial preserves the old file and raises an error. Two tests cover transient/persistent denial. The aerodynamic API test waits for the acquisition task, avoiding a completed-metadata/finishing-worker race. These changes do not affect motor authorization.

No external formal static analyzer was run. MATLAB was discovered and an additional headless run attempted, but exited before producing simulation results. The operator explicitly excluded further MATLAB checks; the startup diagnostic was stopped. No MATLAB/Simulink pass is claimed. Builds, warning-clean host compilation, negative configuration checks and the passing coupled C drive simulation are the software evidence.

### Exact remaining installation checks

1. Verify DO1/11K3 contacts supply intended In1/OFF2; measure input/output polarity, open-wire behavior and relay dropout. Record the complete drawing revision. OFF2 assignment alone does not establish an independent/certified safety function.
2. Record installed V20 firmware/CDS, P0700/P1000/P0719 and applicable DI/BICO OFF1/OFF2/OFF3/pulse sources. Confirm stopped OFF1 can be established with relay off, relay restoration cannot create ON and only fresh physical START starts operation.
3. Validate post-STOP/post-START status mapping, zero-setpoint enable and 5 s preparation/start limits. Check P1120/P1121/P1135, actual inertia and 30 s stop limit.
4. Read P2014[0], 40001 and 40002 separately. Verify telegram refresh semantics and action on command loss with/without reads, halted MCU and disconnected UART. Do not infer mapping from an expected value.
5. Before enabling fault acknowledgment, verify stopped STW7 pulse/clear on the installed V20/CDS, ON staying low, persistent/unsafe faults remaining inhibited and a later physical START being required.
6. Check saved serial settings/power-cycle behavior and maximum slave/bridge processing latency against the 160 ms quarantine. Arbitrarily late RTU replies have no transaction identifier.
7. Verify P1210/all restart paths, brownout/full V20 cycle, ESP32-only reboot, communication restoration, retained expander state and permission removal on MCU/I2C failure.
8. Keep the fan disconnected until independent safety/mechanical checks pass. Supply actual positive frequency gain/time constant and valid fan calibration before AUTO; permissions do not supply model data.

**Current conclusion:** corrected stopped preparation, feedback-confirmed startup and persistent fault/run lockout are supported by software tests. The installation is not declared ready for physical operation. D4/D7 evidence, acknowledgment permission, bounded latency and actual controller identification remain prerequisites.
