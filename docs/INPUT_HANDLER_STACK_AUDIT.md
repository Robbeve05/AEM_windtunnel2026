# InputHandler startup stack audit — 16 November 2026

## Finding and limits

The maintainability defect is a full automatic `Telemetry current` in
`ESP32/src/InputHandlerTask.c`, on a 3072-byte task stack configured in
`ESP32/src/ApplicationTasks.c`. Telemetry embeds `FirstFaultSnapshot`,
`MotorStatus`, readiness diagnostics and fault history, although InputHandler
only needs state/fault and two safety flags for indicators. Its size therefore
grows with unrelated diagnostics. Commit `adcc1f9` expanded Modbus diagnostics;
`9e177b5` added triggering inputs, timestamped output/GPIO events and interlock
details to the embedded fault snapshot without increasing this task's stack.

The existing ELF identifies itself as `7c3b328-dirty`. Its InputHandler entry is
`entry a1, 0x670`: a 1648-byte frame, leaving only 1424 bytes of the allocation
before nested calls and RTOS context. This is strong evidence for insufficient
headroom, not proof of the precise crashing call. A dirty version string cannot
prove an exact match to the flashed image. The supplied backtrace contains only
`... |<-CORRUPTED`, with no PC/SP addresses, so no frames can be decoded.
Corrupted frames are compatible with stack exhaustion and cannot independently
distinguish an unrelated memory overwrite. Hardware confirmation is outstanding.

## Fix and inspection

- Keep the full queue destination as a function-local static object, private to
  the single InputHandler task. No runtime allocation, truncated queue copy or
  shared writable snapshot is introduced. A failed queue peek still selects
  the existing unavailable-status indication.
- Keep all task stack allocations, priorities and core assignments unchanged.
  No diagnostic stack increase was necessary to identify/remove this allocation.
- Preserve existing creation-result checks through a common helper that names
  failures and reports requested bytes, free internal 8-bit heap and largest
  free block. Successful creation reports the same memory diagnostics.
- Existing five-second main-task monitoring covers input, sensor, control, Pi,
  heartbeat and main stacks. Label its high-water marks explicitly as minimum
  free **bytes**, and warn when InputHandler falls below 512 bytes. Monitoring
  runs outside the input task. OTA tasks remain represented by lifecycle flags;
  no handles to self-deleting tasks are retained or dereferenced.
- Emit compiler `.su` frame-size reports for application functions using
  `-fstack-usage`. These reports exclude callees and are not runtime headroom.
  Correct the README's stack-unit heading to ESP-IDF bytes.
- Update host FreeRTOS/logging adapters and add a heap-diagnostics adapter.
  Synthetic heap values in host tests are not ESP32 measurements.

Inspection covered input debounce, safety publication, output status and the
output/I2C initialization, retry, write and readback paths. No unbounded string
copy or variable-length local array was found in these paths. Output I2C writes
use two-byte buffers and readback uses a byte destination; diagnostics indices
check the 7-bit address bound. The telemetry queue and destination both use the
complete `Telemetry` type. Smaller input locals include button state, two safety
snapshots, a motor request and output diagnostics. Nested output/I2C calls and
startup/error logging still require stack and must be exercised on hardware.
This targeted inspection does not exclude corruption elsewhere in firmware.

Safety mapping, emergency-stop logic, interlocks, commissioning values,
inactive output initialization and physical START authorization are unchanged.

## Software validation

| Item | Before | After |
| --- | ---: | ---: |
| InputHandler allocation (bytes) | 3072 | 3072 |
| InputHandler compiled frame (bytes) | 1648 | 336 |
| Allocation minus that frame (not measured headroom) | 1424 | 2736 |
| Static RAM reported by PlatformIO (bytes) | 55124 | 56444 |
| Flash reported by PlatformIO (bytes) | 569957 | 568929 |

The rebuilt ELF confirms `entry a1, 0x150` and the compiler `.su` report confirms
336 bytes. The frame reduction is 1312 bytes; static RAM increased by 1320 bytes.
All seven application tasks still request 35840 stack bytes in total, excluding
TCBs, system tasks and other heap allocations. Static RAM totals do not establish
available runtime heap.

- `platformio run -d ESP32`: PASS, ESP-IDF 5.5.0, `waveshare_s3`.
- `ESP32/tests/run_host_tests.ps1`: **32/32 executables pass**, including startup
  creation order/allocation assertions, real input initialization/readiness,
  fail-closed output failures, physical action gating and I2C contention.
- Python unittest discovery: acquisition ownership passes; source-layout check
  has four pre-existing failures in unchanged `DigitalOutputs.c`, `I2CBus.c`,
  `SafetyIO.c` and `StateMachine.c` (function-section/prototype conventions).
- `test_io_configuration.py` against the firmware compile database: **7/7**
  polarity and output-index acceptance/rejection cases pass.
- `git diff --check`: PASS.

Local logs are `output/input-stack-{build,host-tests,python-tests}.log`.
The original ELF is preserved locally at
`ESP32/.pio/stack-audit/baseline-7c3b328-dirty.elf`; the new ELF is
`ESP32/.pio/build/waveshare_s3/firmware.elf`. Neither is committed.

## Required hardware validation

No device was flashed or run during this audit. Successful initialization of all
tasks and resolution of the reboot loop are **not physically verified**.

With the motor isolated, capture a complete boot log, all seven creation reports,
readiness bits and repeated minimum-free-byte reports. Exercise cold boots,
inactive output initialization, missing/failing I2C devices, output retries,
logging, physical button sequences and normal communication load. Record minimum
InputHandler/other task headroom, free heap and largest internal block after all
tasks initialize and during sustained operation. Investigate warnings below 512
bytes; no measured acceptance margin is claimed here.

If a panic remains, retain the exact flashed ELF and full raw panic addresses.
Use `xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <PC addresses>` for
intact frames only; do not interpret corrupted frame chains as reliable calls.
For continued diagnosis, enable the ESP-IDF end-of-stack watchpoint and heap
poisoning in a separate diagnostic build; if needed temporarily double the
InputHandler stack to 6144 bytes and compare minimum-free readings. A larger
stack alone is not evidence that memory corruption is absent or a final fix.
