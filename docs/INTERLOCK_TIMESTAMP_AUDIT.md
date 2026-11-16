# INTERLOCK 0x0002 and compact fault diagnostics

Investigation and software validation: 16 November 2026. No firmware was flashed
and no hardware timing or post-change boot was measured.

## Confirmed source defect

`SAFETY_SNAPSHOT_STALE` is bit 1, so `detail=0002` is the freshness predicate.
The reported `4294967295` is produced by the fault snapshot's direct unsigned
subtraction, not by an unavailable-data sentinel in that capture path:

```
(uint32_t)(now_ms - safety.tick_ms) == UINT32_MAX
```

This means the snapshot timestamp is one millisecond ahead of the selected
control timestamp modulo 2^32. Source inspection and a regression reproduce
exactly that relation in the second safety queue read after Pitot acquisition.
The supplied report did not include the original individual timestamps; the
new recorder preserves both for subsequent hardware decisions.

The initial `Control_ReadInputs()` path already samples the clock after queue
reads. However, the later `Control_AcquirePitot()` path sampled the clock before
its final safety queue peek:

| Step | Control on core 1, before correction | InputHandler on core 0 |
| --- | --- | --- |
| 1 | Finishes bounded Pitot I2C call | |
| 2 | Saves `now_ms=7000` | |
| 3 | Can be interrupted/preempted | Acquires GPIO, timestamps `tick_ms=7001`, completes output update, publishes snapshot |
| 4 | Copies the coherent new snapshot | |
| 5 | Builds state-machine inputs and evaluates `7000-7001` | |
| 6 | Unsigned age becomes `UINT32_MAX`; READY faults INTERLOCK | |

This is a clock/read ordering defect, not evidence of a torn queue item.
Both times come from the same `esp_timer_get_time()/1000` monotonic clock.
`SafetyIO_Read()` samples physical GPIOs before assigning its microsecond and
millisecond timestamp. InputHandler debounces buttons, completes the existing
output/I2C work and publishes a full item with `xQueueOverwrite`. FreeRTOS
serializes this copy with Control's full-sized `xQueuePeek`. InputHandler runs
every 10 ms, priority 5/core 0; Control runs on core 1. I2C waits and scheduling
can also cause genuinely old data, but neither is needed to reproduce this
artificial one-millisecond future timestamp.

The correction moves the final safety peek before the clock samples in
`Control_AcquirePitot()`. No new bus operation or additional queue read is added.
The initial read and final read now use the same ordering. A deterministic test
injects a complete producer update at that final peek: the original ordering
fails its exact `now/tick/sequence` assertion; the corrected ordering reads
`now=7001`, `tick=7001`, sequence 43 and age zero.

`SafetyTime.h` centralizes wrap-safe age and freshness handling for StateMachine
blocking/health/step decisions and Control reset/readiness reporting. A future
timestamp is classified INVALID and still fails closed; it is never clamped to
zero or allowed to authorize operation. A real age of 100 ms or greater still
fails the unchanged `INPUT_TIMEOUT_MS=100`. Millisecond wraparound within the
valid half-range remains supported. A half-range ambiguity also fails closed.

## Recorder, output and preservation of evidence

- `FaultCodeRegistry.h` is the only source of numeric fault-code definitions.
  `FaultCodes.h/.c` map every existing fault reason and every interlock bit.
  Codes are explicit/stable; `0302` maps exactly to the stale/invalid-order
  interlock predicate. A combined mask selects the lowest defined bit as its
  primary code while retaining the entire mask.
- `FaultRecorder.h/.c` contain a static 32-entry ring. Each record is statically
  asserted to be **32 bytes**: event sequence, decision time, safety time and
  publication sequence, raw age, full detail mask, code, timeout, previous state,
  actual MANUAL/AUTO mode, validity/age flags and original fault reason.
  InputHandler assigns `snapshot_sequence` just before publication; it is
  independent of START and RESET button counters and has no safety authority.
- Capture uses the exact latched `MachineInputs` from the interlock decision.
  It does not resample GPIOs, buses or telemetry. For other fault transitions it
  uses the already assembled control inputs. Direct startup/timing/configuration
  faults refresh their recorded clock, safety copy and mode from the current
  control context without new I/O. Unchanged faults do not append
  duplicate records. An emergency-priority escalation appends its own event
  without replacing the episode's first cause.
- The first record has a separate 32-byte latch. Physical-reset exit marks the
  episode archived; the original record is already in the ring and remains
  readable until normal 32-entry overwrite. The first latch remains readable
  until the next episode. Recovery of the original startup task barrier also
  archives its diagnostic episode when it re-enters INIT, without granting
  reset acceptance or START. The original full snapshot is retained in static
  telemetry storage until the next episode as before. Storage is volatile RAM,
  not a persistent reboot archive. Sequence gaps report overwritten records.
- Capture has no printf/snprintf/ESP_LOG, heap allocation, JSON, blocking queue
  operation or I2C/Modbus traffic. It writes one fixed-size record in a short
  spinlock critical section. Readers copy one record under the same lock and
  format only after releasing it. A read scans at most 32 records. Capture and
  drains are bounded independently of external communication.
- The existing full `Control_CaptureFault()` evidence is now filled in place,
  avoiding its previous large automatic compound initializer. The recorded
  output image comes from the already captured decision output status, rather
  than a newer status sampled after the decision.
- `FaultDiagnosticsTask.c` is a separate priority-1/core-0 consumer with a
  4096-byte stack. Default output is one compact line per recorded transition,
  including `now`, `tick`, raw/INVALID age, timeout, sequence and validity.
  `FAULT?`, `FAULT HISTORY`, and `FAULT DETAIL` are read-only commands on the
  existing native USB serial console. Input is nonblocking and line length and
  per-pass reads/drains are bounded. Unsupported input commands cannot trigger
  START or any other control action.
- `FAULT DETAIL` formats the full captured snapshot only when explicitly
  requested and only if the telemetry episode still matches the retained compact
  first cause. It never substitutes live data for that evidence. If a snapshot
  is no longer available, it reports that fact and prints retained compact data.
- `APP_VERBOSE_DIAGNOSTICS=0` is the default: main's large periodic reports and
  PiCommunication's repeated WAIT reports are opt-in. Startup information,
  periodic heap/stack minima and low-stack warnings remain. No automatic full
  FIRST_FAULT snapshot is transmitted; PiCommunication sends bounded compact
  numeric FAULT records and replays the available history on reconnect.

HMI decoding lives in `HMI/backend/fault_codes.py`. The numeric dictionary and
state names are generated from firmware sources into
`HMI/backend/fault_codes.json`; `docs/FAULT_CODES.md` is generated at the same
time. The generator checks that every fault and every interlock bit is mapped
once and that codes are unique. `--check` detects documentation/dictionary drift.
The client retains the latest 32 events, rejects malformed records and preserves
history across STATUS refreshes. The native read-only panel displays the code,
description, full decoded mask, original age/timestamps/sequence, prior state,
mode, boot timestamp and expandable recent history. Stale telemetry is still
displayed as stale; fault messages alone do not refresh connection freshness.

## Software measurements

The baseline memory figures are from the preceding successful Heartbeat fix
build. Individual frame measurements are compiler/ELF values, excluding all
callees, RTOS/interrupt use and runtime painted-stack minima.

| Item | Before this change | After |
| --- | ---: | ---: |
| Control full fault-capture frame | 704 bytes | 32 bytes |
| Compact ring capture frame | absent | 32 bytes |
| ControlTask entry frame | 32 bytes | 32 bytes |
| InputHandler frame | 336 bytes | 352 bytes |
| Heartbeat frame | 128 bytes | 128 bytes |
| FaultDiagnostics task frame | absent | 80 bytes |
| FaultDiagnostics command frame | absent | 80 bytes |
| Static RAM reported by PlatformIO | 57772 bytes | 57636 bytes |
| Flash reported by PlatformIO | 571177 bytes | 560373 bytes |
| Total application stack allocations | 35840 bytes | 39936 bytes |

The ring itself is 1024 bytes, plus a 32-byte first latch and small metadata/lock.
Other verbose-only static data is eliminated from the default image, explaining
the lower net static RAM/flash figures despite the new recorder. **Runtime heap
must additionally fund the new 4096-byte diagnostics stack and its TCB/allocation
overhead.** Static RAM improvement does not imply improved available runtime
heap. Existing task stack allocations are unchanged.

The existing deterministic real-Pitot/control integration model still reports
6999 us including modeled Modbus work and still detects deliberately excessive
execution. It does not advance its fake clock for CPU instructions, so this is
not a measurement of the recorder's CPU cost. No before/after ESP32 control-loop
timing or hardware stack headroom is available. The existing control execution,
jitter and deadline telemetry and stack high-water marks must be measured on
the bench; no microsecond performance claim is inferred from a host run.

## Verification

- ESP32 `waveshare_s3`, ESP-IDF 5.5.0 firmware: **build PASS**.
- Firmware host suite: **39/39 executable suites PASS**. Covers fresh snapshots,
  the exact 100 ms boundary, genuinely stale and future data, millisecond wrap,
  all fault/interlock mappings, first-cause retention and emergency promotion,
  ring overwrite, compact/default and on-demand console paths, physical START
  gating, MANUAL policy and genuine safety faults.
- A separate test publishes 20000 real `SafetyIO_Read()` snapshots concurrently
  with real `Control_ReadInputs()` calls using the modeled coherent queue
  contract. All fields/sequence remain consistent. This models FreeRTOS queue
  serialization, not the physical ESP32 scheduler. The separate final-peek
  regression exercises the exact failing interleaving and fails with the old
  ordering (recorded in `output/interlock-before-regression.log`).
- Recorder concurrency test: 10000 writes with concurrent readers, no torn
  record; overflow and latch checks PASS.
- Cross-compiler frame-budget checks: **9/9 PASS**, each application function
  in the tested input/control/recorder/diagnostics/Heartbeat variants remains
  within its 512-byte frame budget. This excludes callee stack consumption.
- Existing I/O configuration compiler checks: **7/7 PASS**.
- HMI: **122 tests PASS across 13 isolated modules**, including socket transport,
  malformed data, complete bitmask decoding, bounded history and offscreen
  native presentation of actual mode, code and trigger. Modules were isolated
  because combined discovery stalled with shared test state; no pass is claimed
  for that combined invocation. Missing declared HMI test dependencies were
  installed into the local `.venv`; no global environment was changed.
- Registry generation and `--check`: **PASS**.
- Python ESP32 discovery: acquisition ownership passes; the same four existing
  source-layout failures remain in DigitalOutputs, I2CBus, SafetyIO and
  StateMachine (section/prototype conventions, unrelated to freshness behavior).
  All newly added application files satisfy that layout check.
- `git diff --check`: **PASS**.

Logs: `output/interlock-compact-{build,host-tests,frame-tests,io-tests,python-tests}.log`,
`output/interlock-compact-hmi-summary.log` and per-module `output/interlock-hmi-test_*.log`.
Normal ELF: `ESP32/.pio/build/waveshare_s3/firmware.elf`, SHA-256
`501a29b27f2d7fa1620dc788c11524ac1d3e07d48c726cba445942f7cb32aaf5`.

## Required physical validation

Keep the motor isolated and output requests inactive; do not authorize physical
START for initial diagnosis. Cold boot repeatedly and confirm all eight tasks
initialize. Verify serial command input on the actual USB console. Capture
minimum free stack for InputHandler, Control, Heartbeat, main and FaultDiagnostics
and available/largest internal heap, including explicit full DETAIL formatting.
Exercise concurrent safety publication, I2C delays and communication load and
measure worst control execution, jitter, deadline misses and recorder overhead.

Confirm that healthy snapshots no longer produce an artificial UINT32_MAX age.
Controlled genuine stale, invalid, E-stop, relay, STOP and V20 faults must still
inhibit actuation; the timeout remains 100 ms and physical RESET/START semantics
remain unchanged. Verify one compact line per episode/escalation, ring overflow,
first-evidence preservation and HMI history/reconnect behavior. If a remaining
0302 has a valid age of at least 100 ms, investigate actual InputHandler/I2C
scheduling latency using its recorded timestamps rather than relaxing the limit.

No safety mapping, emergency-stop logic, V20 commissioning parameter, automatic
start setting or physical START requirement was changed. Source/host validation
does not establish safe operation or absence of resets on the real ESP32.
