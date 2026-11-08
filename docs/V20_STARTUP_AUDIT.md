# V20 startup and drive state-machine audit

> **Development update:** Sections 1–15 preserve the original read-only baseline. See section 16 for the implemented corrections, current bench configuration, validation results and outstanding installation checks. Baseline line numbers and flag values describe the audited revision, not the updated files.

Date: 8 November 2026. Repository revision: `48e87ecee6218f00ba3808e768d03fbe4d9aebcb`.
Scope: read-only review of production implementation; host compilation, mocked execution and documentation creation only. No flashing, serial-port access, parameter writes or physical actuation. Production sources and existing tests were not changed. Additional diagnostic probe sources and logs are in `output/`.

## 1. Executive summary

**Assessment: FAIL.**

**Based on the current implementation, will the complete wind tunnel startup sequence work correctly, including OFF1, OFF2, OFF3, safety permissions, Modbus communication and transitions to RUNNING? No.** The standard real build intentionally cannot reach READY because both commissioning flags are zero. Even after legitimate commissioning, confirmed software defects prevent the requested startup/recovery guarantees: RUNNING is entered before operation-enabled feedback, runtime transport faults suppress even pending STOP transactions, and a latched physical emergency/safety-feedback fault blocks the reset procedure that would clear that latch.

An additional installation-dependent startup deadlock exists if DO1/In1 really supplies the OFF2 permission: drive-ready is required before READY/START, but DO1 is only requested in RUNNING/STOP. The actual V20 input assignment must be established before choosing a correction. Do not remove readiness/safety checks or turn on commissioning flags to work around this.

The two deterministic control words themselves are appropriate for forward operational control: `047E` requests OFF1 while releasing software OFF2/OFF3; `047F` requests ON. The current decoder correctly treats ZSW bits 4/5 as **inactive-stop** indications, not positive stop requests. `EB78` contains a drive fault; `EB40` contains no drive fault but has OFF2/OFF3 active and switch-on inhibit. Neither word establishes loss of communication.

All 20 existing host executables passed. Three firmware environments built. Seven compile-only I/O configuration checks passed. Three additional audit probes reproduced defects rather than validating desired behavior. These are software results, not verification of the installation or motor startup.

### Documentation basis and limits

Primary Siemens sources consulted through indexed official manual excerpts:

- [V20 operating instructions 12/2024, A5E34559884-017](https://cache.industry.siemens.com/dl/files/897/109977897/att_1309600/v1/V20_op_instr_1224_en-US.pdf): Modbus basic settings and reference-frequency mapping.
- [V20 compact instructions 12/2024](https://cache.industry.siemens.com/dl/files/915/109977915/att_1309695/v1/V20_cmpct_op_instr_1224_en-US.pdf): communication settings and restart parameters.
- [V20 instructions 01/2018, r0052, printed p.193](https://cache.industry.siemens.com/dl/files/193/109756193/att_942991/v1/V20_op_instr_0118_en-US.pdf): V20 status meanings.
- [V20 instructions 03/2015, r2018 mapping, printed p.228](https://cache.industry.siemens.com/dl/files/321/109479321/att_856072/v1/SINAMICS_V20_Operating_Instructions_032015_en-US.pdf?download=true): control-word mapping.
- [V20 instructions 09/2014, r0052 dependency and P2019](https://cache.industry.siemens.com/dl/files/056/104426056/att_70877/v1/v20_operating_instructions_complete_en-US_en-US.pdf): inhibit release and transmitted status mapping.
- [V20 instructions 04/2019, Modbus mapping](https://cache.industry.siemens.com/dl/files/394/109768394/att_988984/v1/V20_op_instr_0419_en-US.pdf): registers 40001/40002 and control aliases.
- [V20 compact instructions 02/2016, Cn011](https://cache.industry.siemens.com/dl/files/382/109736382/att_880377/v1/SINAMICS_V20_Compact_Operating_Instructions_022016_en-US.pdf): macro intent.
- [V20 instructions 05/2020](https://cache.industry.siemens.com/dl/files/683/109780683/att_1026149/v1/V20_op_instr_0520_en-US.pdf): OFF3 ramp semantics.

Full PDF fetching through the browsing tool failed on large-file limits; a local download also failed TLS authentication. This report therefore does not claim exhaustive page-by-page verification of the latest manual. Cross-edition details, especially upper status bits, PZD mapping, watchdog implementation and installed firmware compatibility, remain commissioning checks. No generic PROFIdrive manual is used as evidence for V20 behavior.

## 2. Complete startup sequence

| Stage | Implementation and dependencies | Failure behavior |
| --- | --- | --- |
| Entry | `main.c:366`, `app_main()` logs reset reason and starts application tasks | No motor command at entry |
| Physical input setup | `ApplicationTasks.c:70`, `SafetyIO_Init()` installs GPIO ISR service, GPIO4–11 inputs and unsafe-edge latches | ESP_ERROR_CHECK failures abort/reboot; hardware must remain safe independently |
| Bus/output setup | `I2CBus.c:42`, `DigitalOutputs.c:76` create mutexes, configure I2C, write inactive output latch before output directions | Failure inhibits output module; startup cannot pass input readiness |
| Calibration | `ApplicationTasks.c:189`, `FanCalibration_Init()` before tasks | Return ignored here; AUTO frequency start later checks validity |
| Shared objects | `ApplicationTasks.c:100,121` create seven queues and four event groups before any task | Allocation failure aborts |
| Task creation | `ApplicationTasks.c:139`: input p5/core0; sensor p3/core0; control p4/core1; Pi p2/core0; heartbeat/OTA p1/core0 | Every task creation checked |
| Control init | `ControlTask.c:168` zeroes context, constructs controller/model, INIT machine, initializes UART, disarms and publishes zero request | UART init failure => MODBUS fault |
| Task barrier | `ControlTask.c:273`, wait for input, sensor and Pi readiness, watchdog refreshed | 15 s deadline => INTERNAL and permanent initialization-hold loop until reboot |
| Clock | `ControlTask.c:144,247` GPTimer alternates sensor/control notifications every 5 ms | Each task runs at 10 ms; clock timeout/overrun latches fault |
| First data | sensor samples become available only after clock starts; V20 service starts after barrier | No deadlock from requiring a sample inside the barrier: sensor ready means library initialized |
| Drive bootstrap | `V20Modbus.c:305,695,764`: FC03 status/actual, reference, watchdog | Read-only bootstrap, exceptions/invalid values retained diagnostically; transport failure retries bootstrap |
| INIT exit | `StateMachine.c:273`: fresh valid safety snapshot, sensor healthy, diagnostics complete, status received, communication healthy, fault-free drive and healthy output writes | Missing external prerequisites hold INIT indefinitely; elapsed deadline is telemetry only |
| WAIT exit | RESET accepted, healthy safety, STOP healthy, sensor/drive/Pi/configuration healthy and run commissioned | WAIT indefinitely until all predicates hold |
| START | Fresh physical START after READY entry; model/manual checks and V20 arm | Software RUNNING immediately, before new drive feedback: defect D1 |

INIT does not require drive operational readiness or safety commissioning; WAIT does. A valid inhibited drive can therefore leave INIT. A boot-time drive fault without DI6 fitted stays INIT because `drive_fault_free` is false; it is not assigned a VFD fault by the state machine in INIT. This is a diagnostic/recovery limitation, not proof of missing communication.

## 3. OFF1/OFF2/OFF3 behavior

OFF1 is STW bit 0 low. Normal STOP sends `047E`/HSW=0 and relies on the V20 operational deceleration ramp P1121; software setpoint profiling applies during running, not to the entire OFF1 stop. After OFF2/OFF3 inhibit, a stopped command with those permissions restored is needed before a new ON edge. The Siemens r0052 dependency explicitly distinguishes inhibit release from simply transmitting ON.

OFF2 is STW bit 1 low; it removes pulses and permits coast-down. OFF3 is bit 2 low; it uses P1135 then pulse cancellation/inhibit. Firmware never sends either bit low in its two control words. OFF3 must remain high for ordinary operation even though this application does not use quick stop. Zero-initialized context is not a transmitted zero control word.

Hardware sources can hold OFF2/OFF3 active despite serial bits being high. DO1 is described as relay 11K3 supplying VFD In1 Enable (`MachinePins.h:32`), but the repository contains no complete electrical schematic or V20 parameter export. If In1 is an OFF2 source, the present relay/readiness ordering cannot bootstrap it. Software cannot override the independent safety circuit.

| STW bit | V20 meaning | Intended level in normal STOP / RUN | Current STOP / RUN | Finding |
| --- | --- | --- | --- | --- |
| 0 | ON/OFF1 | 0 / 1 | 0 / 1 | Correct; require deliberate new ON edge |
| 1 | OFF2, active low | 1 / 1 | 1 / 1 | Released in software; inspect hardware source |
| 2 | OFF3, active low | 1 / 1 | 1 / 1 | Released; no normal quick stop |
| 3 | Pulse enable | 1 / 1 | 1 / 1 | OFF1 still controls normal stop |
| 4 | Ramp generator enable | 1 / 1 | 1 / 1 | Defined |
| 5 | Ramp generator start | 1 / 1 | 1 / 1 | Defined |
| 6 | Setpoint enable | 1 / 1 | 1 / 1 | Defined |
| 7 | Fault acknowledge | 0 normally; bounded reset pulse only | 0 / 0 | No drive fault reset implemented |
| 8 | Jog right | 0 / 0 | 0 / 0 | Defined inactive |
| 9 | Jog left | 0 / 0 | 0 / 0 | Defined inactive |
| 10 | PLC process-data control | 1 / 1 | 1 / 1 | Defined active |
| 11 | Setpoint inversion | 0 / 0 | 0 / 0 | Forward command |
| 12 | No assigned function in cited mapping | 0 / 0 | 0 / 0 | Keep zero; verify installed revision |
| 13 | MOP raise | 0 / 0 | 0 / 0 | Inactive |
| 14 | MOP lower | 0 / 0 | 0 / 0 | Inactive |
| 15 | CDS bit 0 | 0 / 0 for selected data set | 0 / 0 | Verify actual CDS selection |

No correction should set every bit high. Serial permissions are operational commands, not safety certification.

## 4. Control-word analysis

Only `V20Modbus.c` builds/transmits STW: constants at lines 45–46, `build_command():248`, `send_operation():447`. There are no separate `v20.c`/`v20.h` modules in this tree. `V20Modbus.h` is the public interface. UART writes are owned by ControlTask; input task owns relay requests. There is no competing UART task.

`047E = 0000 0100 0111 1110`; `047F = 0000 0100 0111 1111`.

| Situation | Actual serial command | Responsible path / qualification |
| --- | --- | --- |
| ESP32 power-on | None | RAM reset is not a UART transmission |
| UART init/drive bootstrap | None; FC03 reads only | Init/restart_commissioning/Service |
| Safety inactive | Normally 047E if commissioned, configured and not transport-latched | Service disarms; hardware relay requested off; no software OFF2 word |
| Safety active | Still 047E while stopped | Safety restoration does not arm |
| WAIT | 047E heartbeat, 150 ms interval | Service, only when operation commissioning predicates pass |
| READY | 047E heartbeat | Same |
| Physical START | 047F with ramped setpoint on next scheduled write | State transition -> Arm -> Service; not synchronous with button |
| RUNNING | 047F, nominal command period 10 ms | Service, bounded by outstanding transactions/status reads |
| Physical STOP | 047E, setpoint zero after outstanding transaction/status scheduling | Disarm sets pending_stop; Service writes |
| E-stop | Relay off; eventual 047E if transport remains healthy | No special coast word; hardware must enforce independent coast |
| Drive/controller fault | Relay off; eventual 047E if driver remains available | Fault/disarm; transport latch can suppress STOP completely |
| RESET | No acknowledge word; bootstrap reads; later stopped heartbeat | RequestRecheck; bit 7 never becomes 1 |

In the default real build, `V20_RUN_COMMISSIONED=0` suppresses **all** control writes, including heartbeat STOP, in every row. Invalid reference/configuration also suppresses writes. A report cannot truthfully assign an unconditional word to each application state.

RUN already passed into the UART transmit buffer cannot be recalled. Service disarms before receiving the outstanding reply, so it does not intentionally queue another RUN after observing a stopped request. However `MotorRequest.epoch`, `.coast`, and `.permissive` are not used by the driver, and outstanding RUN acknowledgment updates `last_run_command` after a STOP request. Normal completion leads to OFF1 later; communication failure leads to D3. There is no proof that the physical drive cannot receive an old buffered RUN after the physical STOP edge. Hardware removal of permission is the independent protection.

## 5. Status-word decoding

The application reads 40110/40111 as ZSW/normalized actual speed. Confirm P2019 mapping on the installation. The r0052/P2019 terminology differs between manual presentations; neither should be replaced with a generic drive-state mask.

| Bit | V20 interpretation used here | EB78 | EB40 |
| --- | --- | --- | --- |
| 0 | Ready to power up/switch on | 0 | 0 |
| 1 | Ready to operate, pulses blocked | 0 | 0 |
| 2 | Operation enabled | 0 | 0 |
| 3 | Drive fault | 1 | 0 |
| 4 | OFF2 inactive when 1 | 1: released | 0: active |
| 5 | OFF3 inactive when 1 | 1: released | 0: active |
| 6 | Switch-on inhibit | 1 | 1 |
| 7 | Warning/alarm | 0 | 0 |
| 8 | Setpoint/actual tolerance satisfied (P2019 mapping) | 1 | 1 |
| 9 | PZD control/control requested | 1 | 1 |
| 10 | Frequency comparison/max threshold reached | 0 | 0 |
| 11 | Current/torque limit warning absent when 1 | 1 | 1 |
| 12 | Reserved in cited P2019 map; r0052 editions also label brake open | 0 | 0 |
| 13 | Motor overload alarm absent when 1 | 1 | 1 |
| 14 | Forward direction indication | 1 | 1 |
| 15 | Converter overload alarm absent when 1 | 1 | 1 |

`EB78 = 1110 1011 0111 1000`; `EB40 = 1110 1011 0100 0000`. Upper direction/threshold flags do not prove shaft motion. A single status snapshot cannot identify the exact fault code or the external inhibit source. Read fault history/display separately.

`accept_reply():538–556` correctly separates `drive_fault_free` from ready. Ready requires bits 0,4,5,9 set and bits 3,6 clear. It does **not** require bit 1 or 2. That may represent a stopped start-ready condition; it cannot prove RUNNING. Warning bit 7 and upper flags are not exposed as named motor diagnostics.

No replacement of the low-bit polarity decoder is warranted. Recommended additive decoder (proposal only):

```c
/* Only call with a validated, fresh ZSW from the verified P2019 mapping. */
fault = (zsw & 0x0008u) != 0;
warning = (zsw & 0x0080u) != 0;
ready_to_switch_on = (zsw & 0x0001u) != 0;
ready_to_operate = (zsw & 0x0002u) != 0;
operation_enabled = (zsw & 0x0004u) != 0;
off2_active = (zsw & 0x0010u) == 0;
off3_active = (zsw & 0x0020u) == 0;
switch_on_inhibited = (zsw & 0x0040u) != 0;
pzd_control = (zsw & 0x0200u) != 0;
/* Transport health is independently calculated from validated response times. */
```

There is no direct ZSW OFF1 command bit. Infer commanded OFF1 from STW bit 0, and confirm the resulting state/speed using fresh feedback. Do not label ZSW bit 0 as the OFF1 request.

## 6. Modbus communication

UART1, TX17/RX18/DE21; slave 1; 115200, 8E1. FC16 writes two registers at PDU99; FC03 reads PDU109 count2, PDU15 count1, PDU0 count1. CRC byte order and word byte order are correct. HIW is signed and converted using 50 Hz/16384. Requests reject nonfinite/negative/above-limit output and ages >=50 ms. There is a single outstanding transaction, 80 ms response timeout, zero-wait UART RX and 1.75 ms minimum silence.

| Response | Current classification |
| --- | --- |
| No reply | Timeout, errors/timeouts increment; bootstrap retry or runtime latch |
| Bad CRC/address/function/count | Invalid transport response and runtime latch |
| Valid exception | Timestamp proves responding peer; operation diagnostic records exception; write exceptions latch |
| Valid inhibited status | Communication can remain true, drive-ready false |
| Valid faulted status | Communication can remain true, drive-fault-free false |
| Valid stopped-ready status | Ready may be true, operation-enabled false |
| Valid enabled status | Bit 2 is published but not used for RUNNING confirmation |

`GetStatus():868` additionally masks communication health with software latch/recheck state. Therefore `comm_ok=0` is not exclusively a wire-health measurement. `errors=1,timeouts=0` could be CRC/frame/exception/UART failure; it is insufficient to attribute an old log to drive readiness. The current implementation does not increment errors merely because EB40 is inhibited. Reference mismatch sets last_error without incrementing errors, so counters and last_error have different meanings.

Runtime failure has a confirmed scheduling defect: `fail_transaction():389` sets pending_stop and latched_fault, but `Service():812` returns on the latch before servicing pending_stop. Even a restored UART receives no further reads or STOP until explicit recheck. Read traffic and the exact watchdog refresh semantics must be checked against the installed drive: a watchdog must not be assumed to catch a frozen RUN while other telegrams continue.

RTU has no transaction ID. A very late FC03 reply with the same length can match a subsequent read; CRC alone cannot prove freshness. Exception handling waits 160 ms, but generic failure recovery waits only 1.75 ms. This is a residual association risk requiring delayed-response tests and a documented maximum slave response latency; `STALE_RESPONSE` is an age diagnostic, not transaction identification.

## 7. Safety and enable chain

Verified source mapping (`MachinePins.h:22–33`): DI1/GPIO4 E-stop, DI2/GPIO5 RESET, DI3/GPIO6 START, DI4/GPIO7 NC STOP, DI5/GPIO8 safety feedback, DO1/bit0 operational enable. Optional DI6 fault and DI7 contactor feedback are disabled by default. Provisional GPIO polarities are E-stop active high; START/RESET/STOP-healthy/safety-healthy active low. Field 24 V polarity must not be equated with optocoupler GPIO polarity.

Inactive latches are loaded before direction enable. DigitalOutputs checks commissioned safety and live inhibition again before writes, serializes complete output images with a mutex, and reports write failures. Acknowledged expander writes do not prove physical contact operation. Power-up/reset electrical behavior, an expander retaining an energized relay across an ESP32-only reboot, and a failed I2C bus cannot be validated from software.

Input task polls at 10 ms, validates/debounces START and RESET, and consumes unsafe edge latches. DO1 requires fresh motor-request permissive. State-machine permissive is true only in RUNNING/STOP with process/safety healthy. Thus DO1 drops in READY, including immediately after stop confirmation. If DO1 controls OFF2, normal STOP -> READY then creates a fresh inhibit and likely MODBUS fault; the same ordering obstructs startup.

No full electrical drawing exists in this checkout; `docs/hardware/MACHINE-IO.md` explicitly says its table is not a complete schematic. Relay naming and lamp assignments also conflict: current header says 11K3 and green DO2/red DO3/yellow DO4, while that document says K2 and yellow DO3/red DO4. Treat the header as code evidence only, not verified wiring. The physical circuit must remove torque permission when the MCU fails; firmware is not a substitute.

## 8. FreeRTOS initialization dependencies

Queue/event creation before task creation prevents null shared-object scheduling races. Control context and V20 context are initialized deterministically. Input task readiness requires its first successful output write; sensor readiness means library initialization; Pi readiness means Ethernet setup/listening socket initialized, not that an HMI heartbeat exists. Heartbeat/OTA are deliberately outside the critical barrier.

The input task sets its local `initialized` on its first iteration even if the output write fails (`InputHandlerTask.c:153`). Later successful writes do not publish the missing ready bit. This produces permanent initialization failure until reboot. Sensor init likewise has no retry, and the task-barrier failure intentionally enters a permanent hold. These are fail-closed availability limitations; they need explicit subsystem diagnostics rather than an unexplained INTERNAL fault.

No task waits for a control-clock sample before setting sensor readiness, so there is no sensor-clock bootstrap cycle. Hardware/RTOS assertions can reset the MCU; reset-safe output hardware remains essential. The 3 s task watchdog is serviced during the 15 s barrier. Its target timing, priority contention and cross-core scheduling are not established by host mocks.

Safety snapshots use milliseconds, most queue timestamps use ticks. `Control_PublishStatus():818` subtracts `safety.tick_ms` from `now_tick`; correct only with configured `CONFIG_FREERTOS_HZ=1000`. The state-machine safety age calculation correctly uses milliseconds. This diagnostic assumption should be removed or asserted.

## 9. State-machine transitions

| Transition | Trigger and requirements | Command / feedback / timeout |
| --- | --- | --- |
| INIT -> WAIT | Fresh safety, sensor, complete drive diagnostics, valid current status, communication, no drive fault, output writes healthy | Stopped/read-only policy; no operational-ready requirement; missing prerequisites hold indefinitely |
| WAIT -> READY | Deliberate RESET release then healthy safety, STOP, sensor, Pi, reference/config, commissioned drive readiness | Recheck reads; reset feedback window 5 s; no RUN |
| READY -> RUNNING | Fresh physical START after READY entry, no STOP/RESET, all process/safety checks, successful arm/model checks | RUN requested; **no operation-enabled confirmation timeout** |
| RUNNING -> STOP | Physical/remote STOP or NC STOP not healthy | Disarm, OFF1, retain relay while healthy; await fresh post-entry speed <=0.1 Hz, pending STOP cleared and last acknowledged command stopped |
| STOP -> READY | `V20Modbus_StopConfirmed()` | 30 s stop timeout => VFD fault; completion does not require STOP button release until next START |
| FAULT -> WAIT | Physical RESET and feedback recovery, all required health predicates | Recheck only; controller stays stopped; urgent-latch circularity blocks physical safety recovery |
| WAIT -> READY after fault | Reset accepted and process healthy | A later fresh physical START is still required |
| Any -> FAULT | E-stop/urgent latch, fresh unsafe relay, optional drive/contactor feedback, timing fault | Disarm; relay off; operational STOP subject to driver availability |
| Non-INIT -> FAULT | Stale/invalid safety or failed outputs | INTERLOCK |
| READY/RUNNING/STOP -> FAULT | Sensor loss, VFD fault, missing ready/drive health, Pi loss or unsafe process | Missing ready is currently labeled MODBUS, even for a valid inhibit |

All WAIT blockers are enumerated by `StateMachine_GetBlockingReasons():96`: E-stop, STOP, invalid/stale/uncommissioned safety, relay, outputs, optional VFD/contactor input, sensor, UART initialization, status response, drive fault, communication, V20 configuration, operation inhibit, run commissioning, Pi, missing RESET or pending RESET feedback. Any can persist indefinitely. `comm_ok=1` alone is intentionally insufficient. Normal OFF1 while ready is not itself a fault; OFF2/OFF3/inhibit can prevent READY, and are mislabeled communication faults once READY is reached.

## 10. Fault recovery

| Event | Current behavior and restart assessment |
| --- | --- |
| Normal STOP | OFF1; fresh speed-based stop confirmation; later physical START required |
| E-stop | Hardware must act; software latch disarms; reset initiation blocked by urgent latch (D2) |
| Safety feedback loss | Same latch/reset circularity as E-stop |
| Modbus timeout | Runtime driver latch blocks further STOP/polling; physical RESET recheck can restore diagnostics if no safety/drive-fault gate blocks it |
| V20 fault | RESET predicate requires `!drive_fault`; no STW7 pulse or equivalent write exists; external drive reset needed |
| Unexpected valid inhibit | WAIT stays inhibited; READY/RUNNING/STOP classify as MODBUS fault |
| ESP32 restart | Arm/state reset; held START is not accepted as fresh press; physical RESET and later START required in normal software path |
| V20 power cycle | If loss exceeds timeout, software disarms/latches. A shorter interruption/brownout can escape detection; drive automatic restart settings are not checked |
| Temporary communication loss | Explicit recovery, not automatic re-arm; unresolved hardware watchdog/relay behavior |

Controller integrators and profiles reset outside RUNNING. Physical action sequence counters are consumed every cycle; old START edges do not replay on READY entry. HMI setpoints may remain as intended targets after recovery; they do not independently arm. RESET never creates a RUN word. Recheck does not clear every diagnostic/pending-command field (`restart_commissioning()` preserves pending_stop and old control-word validity flags), so telemetry must not treat an old write acknowledgment as a new stopped handshake.

## 11. Automated test results

Executed locally, no hardware:

| Check | Result / evidence |
| --- | --- |
| `ESP32/tests/run_host_tests.ps1` | **20/20 PASS**, `output/v20-audit-host-tests.log`; MSVC /W4 /WX |
| PlatformIO `waveshare_s3` | PASS |
| PlatformIO `waveshare_s3_sim` | PASS |
| PlatformIO `waveshare_s3_frequency` | PASS; all three in `output/v20-audit-build.log` |
| `test_source_layout.py` | 1 test PASS |
| HMI `unittest discover -s tests -v` with offscreen Qt | **101 tests PASS**, `output/v20-audit-hmi.log`; these test HMI behavior with simulated/mocked connections |
| `test_io_configuration.py` against production compile database | 7 PASS: active-high/low valid and five invalid configurations rejected; `output/v20-audit-io.log` |
| Additional audit probe | 3 confirmed defect reproductions; `output/v20_audit_probe.c`, `.cmd`, `v20-audit-probe.log` |

Initial sandbox build/compiler attempts failed accessing external PlatformIO locks/compiler paths; reruns with authorized access succeeded. No new production tests were added during this read-only audit. No separate formal analyzer or MATLAB/Simulink execution was performed; compiler warnings-as-errors, configuration compilation, source review and host mocks provide the static/automated evidence here.

The audit probe reuses the existing UART fake and links real `V20Modbus.c` and `StateMachine.c`: after a runtime RX error and restored transport it asserts that no additional STOP is transmitted; it demonstrates READY->RUNNING without any operation-enabled input; and it asserts that released emergency plus RESET with the persistent urgent latch never creates reset_pending. These are passing reproductions of **undesired behavior**, not acceptance tests proving safety.

| Requested scenario | Existing evidence and remaining acceptance test |
| --- | --- |
| Healthy boot, motor stopped | startup/control/V20 tests pass under mocks; commissioned combined input/drive model still needed |
| V20 offline | UART/bootstrap timeout tests pass; require INIT diagnostic and recovery integration test |
| OFF1 active | stopped heartbeat tested; require ON edge accepted only after fresh stopped handshake |
| OFF2 / OFF3 active | EB40 tests check combined inhibits; add separate-bit and relay-dependent models |
| Ready + START | physical freshness tests pass; add STARTING with delayed/absent enabled feedback |
| STOP | OFF1 and fresh post-stop speed tests pass |
| Emergency stop | disarm/inhibit mock tests exist; add real latch-through-reset integration test; physical action unverified |
| RESET with emergency active | inhibited in tests; add released-but-latched case D2 |
| V20 fault | status failure recognized; missing acknowledge/reset recovery test |
| Communication lost running | fault latch tested; audit confirms no STOP retry; independent watchdog action unverified |
| ESP32 reboot | boot/button logic checked separately; retained expander and buffered-UART integration absent |
| V20 power cycle | no full loss/brownout and restart-policy test |
| HMI request before READY | command processing does not arm; HMI/firmware integration is mocked |
| Stale RUN after STOP | stale request lockout passes; buffered TX and delayed-response association not modeled |

The built-in drive simulation always returns `0231` regardless of RUN/relay/fault state (`V20Modbus.c:628`). Consequently simulation can report RUNNING while operation-enabled stays zero, and cannot validate OFF2 relay startup, genuine drive transitions or ramps. The UART fake also starts with independent `0231` and does not model the electrical permission chain.

## 12. Required physical V20 parameter checks

Read the installed model, firmware revision and active command/drive data set first. Record actual values, do not merely copy intended settings:

| Parameter/item | Intended verification |
| --- | --- |
| P2021 | Address 1; not P2011 USS address |
| P2010[0] | Value **12** means 115200; not literal 115200 |
| P2023 | 2 Modbus; save appropriately and power-cycle as Siemens requires |
| P2034/P2035 | 2/1 = even parity/one stop bit; parity/stop-bit constraints matter |
| P2022 | Actual reply delay compatible with 80 ms master timeout; intended 20 ms |
| P2014[0] | Nonzero expected 500 ms; 0 disables telegram timeout; verify actual failure response |
| 40001 / 40002 | Read actual watchdog time/action separately; register map does not establish identity with P2014; firmware reads only 40001 and default does not enforce it |
| P2000 active index | 50 Hz; register40016 should return5000; verify correct data set |
| P0700/P1000 active CDS | 5/5 USS/Modbus command and frequency sources; inspect P0719 override/source selection |
| Cn011 and commissioning | Modbus connection macro is a starting configuration, not proof of the final parameter set; audit settings after quick commissioning/macro changes |
| P0701–P0704 and BICO | Determine what V20 In1 actually does; verify all ON/OFF1, OFF2, OFF3 and pulse-enable sources including P0840/P0844/P0845/P0848/P0849/P0852 where applicable to installed revision |
| P2019/r2018/r0052/r0054 | Verify transmitted word mapping, actual received STW and displayed status; verify command acceptance |
| P1080/P1082 | Minimum/maximum frequency; physical maximum <=50 Hz; check override/jog/fixed setpoints |
| P1120/P1121/P1135 | Acceleration, normal deceleration and OFF3 deceleration; normal stop must fit 30 s timeout under fan inertia/load |
| P1210/P1211/P1214 | Disable automatic restart according to intended no-restart policy; audit all restart mechanisms |
| P1200, motor identification | Flying start/coasting rotor requirements and absence of pending commissioning/identification action on next ON |
| P030x motor data | Correct motor nameplate/model and safe operating limits; VFD rating is not motor identification |

Physically validate E-stop operation with MCU halted, UART disconnected and I2C failed; relay contact function/dropout; DI5 authenticity; watchdog with continued reads but stopped control updates; reset/reboot power sequencing; stop time; no restart after restoration. These are installation acceptance tasks, not permission to energize during this audit.

## 13. Identified software defects

Each entry separates demonstrated code behavior from conditional hardware implications.

### D1 — HIGH: RUNNING is command intent, not confirmed drive operation

- **Location:** `StateMachine.c:297–299`, `Control_UpdateStateMachine():517–537`, `Control_PublishStatus():810`, `V20Modbus.c:548`.
- **Root cause/actual:** physical START immediately transitions RUNNING; ready uses bits0/4/5/9, not bit2. No new enabled feedback is required, even before the first RUN transmission. Audit probe confirms this.
- **Expected:** report RUNNING only after fresh post-start accepted drive operation feedback; zero-speed operation-enabled can be valid at zero setpoint.
- **Correction:** add explicit STARTING phase and command epoch/timestamp; authorize operational relay and deliberate RUN there, confirm post-command bit2 plus healthy mapping/state, then enter RUNNING. Bound the phase and clear it on STOP/fault.
- **Validation:** delayed/never-enabled, rejected ON, zero-setpoint ON, STOP/E-stop during start, stale enabled status.

### D2 — HIGH: physical safety latch prevents its own RESET acknowledgment

- **Location:** `SafetyIO_Fault():207`, `SafetyIO_Acknowledge():232`; `Control_CheckSafety():466`; `StateMachine.c:222,248–267`.
- **Root cause/actual:** urgent_fault remains latched after release. RESET requires urgent_fault NONE to create reset_pending; acknowledgment runs only when reset_pending already exists. E-stop and relay faults cannot initiate recovery through this path. Reproduced in audit probe.
- **Expected:** valid deliberate physical RESET after restored live feedback may acknowledge the software latch, never arm/run.
- **Correction:** validate reset release, freshness, stop and live restored safety first; call generation-protected SafetyIO_Acknowledge independent of reset_pending, reread latches/snapshot, then evaluate recovery. Keep unsafe edges dominant and never clear on mere restoration.
- **Validation:** actual SafetyIO latch + button + ControlTask integration, concurrent unsafe edge, RESET held at boot, released E-stop without RESET, relay loss during acknowledgment.

### D3 — HIGH: runtime communication latch prevents pending STOP and polling

- **Location:** `fail_transaction():372–395`, `accept_exception():425–430`, `Service():812–841`.
- **Root cause/actual:** fault latch sets pending_stop but unconditional early return prevents sending it; recovered transport remains silent until recheck. Probe confirms the fake drive retains047F.
- **Expected:** revoke RUN immediately; preserve latched user recovery requirement while allowing a bounded stopped-command/diagnostic service policy.
- **Correction:** separate run-authorization latch from transport scheduling; prioritize safe STOP attempts after outstanding frame/quarantine, with finite retries and independent watchdog/hardware fallback. Do not retry RUN or declare a timed-out write unapplied.
- **Validation:** dropped RUN acknowledgment, read CRC/error after accepted RUN, restored bus, failed STOP, continuously failed bus, watchdog and relay physical tests.

### D4 — HIGH, conditional installation blocker: operational relay/readiness dependency cycle

- **Location:** `StateMachine.c:213–214,294–299,316–317`; `InputHandlerTask.c:137–146`; `MachinePins.h:32–33`.
- **Root cause/actual:** operational readiness precedes START, but DO1 only requested after RUNNING. If In1 is required for OFF2 release, WAIT cannot become READY. After normal stop, READY drops the same permission.
- **Expected:** a documented stopped preparation sequence can establish only the non-safety operational permission after legitimate reset/safety authorization, without ON or motion.
- **Correction:** first verify circuit and DI/BICO mapping; implement an explicit stopped preparation stage with047E acknowledged, relay feedback/settling and fresh status before READY. Preserve independent safety gating; never force OFF2 against unsafe hardware.
- **Validation:** coupled relay/V20 mock with initial EB40, normal stop/restart, feedback loss and hardware acceptance.

### D5 — HIGH: no controlled physical RESET path for a V20 fault

- **Location:** `StateMachine.c:248–252`; `send_operation():455`; `RequestRecheck():754`.
- **Root cause/actual:** reset requires no drive fault, and only047E/047F exist, both bit7=0. Recheck reads do not clear a V20 trip.
- **Expected:** either explicitly require/document local V20 reset, or implement an authorized physical-reset-to-drive-ack sequence while stopped.
- **Correction:** separate drive acknowledgment from run/reset acceptance; after live safety checks and physical RESET, send bounded OFF1 + bit7 pulse (`04FE`), return047E, reread fault status, require a later physical START. Do not acknowledge automatically or while E-stop is active.
- **Validation:** persistent fault, cleared fault, reset while unsafe, lost ACK and bit7 edge timing, no ON bit during reset.

### D6 — MEDIUM: expected drive inhibit is labeled communication failure

- **Location:** `StateMachine.c:239–242`; `Control_UpdateDiagnostics():912–919`; `GetStatus():868`.
- **Root cause/actual:** drive-not-ready and comm failure share MODBUS fault; diagnostic task can overwrite last_fault with MODBUS after a VFD fault. Latch/recheck also masks wire health.
- **Expected:** distinguish UART init, no/invalid reply, current drive fault, valid inhibit, invalid configuration and confirmed running.
- **Correction:** use separate transport-current/status-current/configuration/fault/inhibit fields and fault reasons; use WAIT/preparation for expected startup inhibits and explicit unexpected-drive-state faults during operation; preserve original cause.
- **Validation:** EB40 healthy communication, EB78 valid fault, CRC/timeout, wrong reference, inhibit during STOP/READY.

### D7 — HIGH commissioning gap: watchdog effectiveness is not enforced

- **Location:** `AppConfig.h:147–160`; `accept_reply():565`; `Arm():728`; repository V20 hardware document.
- **Root cause/actual:** default mapping-confirmed=0 bypasses watchdog verification;40002 and P2014 are not inspected. Host boot deliberately accepts40001=1000 although expected P2014 is500.
- **Expected:** independent validated command-loss behavior before real run commissioning.
- **Correction:** record verified semantics separately for P2014 and40001/40002, validate approved configuration or require explicit traceable commissioning evidence before arm. Do not assume40001 equalsP2014 or enable writes blindly.
- **Validation:** real revision parameter readback and lost/control-frozen traffic tests; mocks for disabled/mismatched watchdog.

### D8 — MEDIUM: first output write failure permanently withholds task-ready bit

- **Location:** `InputHandlerTask():149–165`; `Control_WaitForTasks():273`, `Control_HoldInitializationFault():305`.
- **Root cause/actual:** initialized is set before checking outputs_ok; later recovery cannot publish readiness. General task deadline becomes INTERNAL and requires reboot.
- **Expected:** explicit subsystem failure reason; either documented reboot-required failure or bounded inactive-only recovery.
- **Correction:** mark input task initialized only after successful inactive initialization, and propagate subsystem failure causes. Keep failed safety outputs inhibited throughout.
- **Validation:** first failed write then success, persistent failure and partial output initialization; no relay energization.

### D9 — MEDIUM: simulation/test drive model omits decisive state feedback

- **Location:** `simulate_response():599–638`; `tests/test_v20.c:228`; `MachineInputs` lacks operation_enabled.
- **Root cause/actual:** fixed0231 does not model enabled operation, relay inhibits, ON edges or ramps. Passing integration tests can assert software RUNNING while drive remains disabled.
- **Expected/correction:** stateful drive mock coupled to relay/safety, delayed transitions, V20 faults and ramps; retain fault-injection transport tests.
- **Validation:** full scenario matrix above; assert actual enabled feedback before reporting running.

### D10 — LOW: timestamp units in safety freshness telemetry

- **Location:** `Control_PublishStatus():818–820`.
- **Root cause/actual:** ticks minus milliseconds works only at current1000Hz tick rate.
- **Expected/correction:** compare now_ms to safety.tick_ms using wrap-safe subtraction.
- **Validation:** timestamps across wrap and alternate RTOS tick frequency.

### D11 — MEDIUM: stale hardware/driver documentation

- **Location:** `docs/hardware/V20_MODBUS.md`, register contract/driver description; `docs/hardware/MACHINE-IO.md`, logical assignments; compare `MachinePins.h:32–37`.
- **Actual/root cause:** document claims FC06 watchdog configuration and initial OFF1 commissioning; implementation bootstraps read-only, never uses FC06 and does not attempt STOP after runtime fault latch. Lamp/relay naming conflicts.
- **Expected/correction:** update documentation only after reviewed command-policy changes, label actual read-only behavior and unresolved watchdog mapping, reconcile drawing revision and relay/lamp names.
- **Validation:** source-to-document review and verified as-built drawing.

### D12 — MEDIUM residual risk: delayed reply association is incompletely validated

- **Location:** `fail_transaction():393`, `verify_reply():499`, `send_operation():486`.
- **Actual/root cause:** FC03 reply has no register address/transaction ID; generic recovery has only frame-gap quarantine. A sufficiently delayed same-shaped reply can be attributed to a later operation.
- **Expected/correction:** establish maximum slave response latency and bounded quarantine/drain strategy; avoid treating silence/CRC as proof of transaction identity. Preserve fresh post-stop command/status ordering.
- **Validation:** inject replies delayed past timeout, especially identical-length status reads and differing one-register bootstrap reads. Risk is inferred from protocol/code; not reproduced by current mocks.

## 14. Recommended code changes

Implement only after review; no production patch is included in this audit.

1. Fix D2 physical reset ordering with the existing generation-protected latch API; never weaken live hardware interlocks.
2. Refactor V20 service into transport health plus persistent run lockout; implement bounded STOP/diagnostic handling for D3 and delayed-frame tests for D12.
3. Add STOPPED_PREPARE and STARTING (or equivalent explicit phases) to `StateMachine.h/.c`, `ControlTask.c`, telemetry and HMI. Stop-preparation may only enable the verified non-safety permission after reset/safety checks. STARTING requires a fresh physical START; only fresh drive-enabled feedback completes it. A STOP in either phase wins.
4. Add validated drive-reset pulse handling to the centralized driver if remote drive acknowledgment from physical RESET is required. Keep ON=0 throughout and clear queued run intent/epoch on all recovery paths.
5. Separate drive fault, inhibit, configuration and transport diagnostics; expose warning and preparation/start timeout causes. Keep readback independent of authorization.
6. Add commissioning records/checks for command source, PZD mapping, ramps, watchdogs and no-restart settings. Do not automatically modify or assert unknown parameter values.
7. Fix initialization readiness recovery/diagnostics, timestamp units and stale documentation.
8. Replace unconditional drive-ready mocks with a coupled stateful V20/relay model and implement the full requested acceptance matrix before hardware commissioning.

Manual frequency and AUTO airspeed currently share the centralized state/arm/service path. Manual frequency is limited to0–50Hz and uses the controlled profile; AUTO computes frequency only in software RUNNING and resets its integrator otherwise. AUTO default torque mapping is deliberately uncommissioned and generates INTERNAL on a start attempt; frequency AUTO additionally checks calibration/model commissioning. This should be an explicit readiness diagnostic rather than a surprising start-time INTERNAL fault. HMI commands adjust mode/setpoint/PID or STOP; none in Control_ProcessCommands authorize START/RESET. Preserve these protections in the refactor.

## 15. Remaining uncertainties and final readiness decision

Unverified: actual electrical schematic and relay contacts; GPIO polarities and power-up retained output state; installed V20 firmware/CDS/DI/PZD mapping; watchdog action and timeout refresh semantics; exact latest-edition upper status semantics; motor/ramp/inertia behavior; brief V20 power interruptions; physical independent emergency response and coast-down; target scheduling and UART hardware buffering under fault.

No fan or drive was actuated. Mock tests and successful builds cannot settle these uncertainties. **FAIL is based on confirmed software behavior D1–D3/D5, independently of the unresolved physical checks.** Properly fixing those defects, completing the coupled tests, and reviewing the real commissioning evidence are required before a PASS or CONDITIONAL operating assessment is justified.

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
