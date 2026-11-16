# Heartbeat startup crash investigation — 16 November 2026

## Exact crash image and decoded addresses

The preserved ELF is `ESP32/.pio/stack-audit/baseline-2ae976b-dirty.elf`.
Its full SHA-256 is
`90cea315086ddb469c2fec3152ed4bad1d3380f17fa78b75abd72f4354e65c32`,
matching the boot/panic prefix `90cea3150` supplied by the operator. This is the
image used for decoding, not the subsequently rebuilt ELF.

| PC | Decoded location in ESP-IDF 5.5.0 |
| --- | --- |
| `0x40378311` | `panic_abort`, `esp_system/panic.c:469` |
| `0x4037da51` | `esp_system_abort`, `esp_system/port/esp_system_chip.c:87` |
| `0x4037eba6` | `vApplicationStackOverflowHook`, `freertos/FreeRTOS-Kernel/portable/xtensa/port.c:563` |
| `0x4037fe8f` | `vTaskSwitchContext`, `freertos/FreeRTOS-Kernel/tasks.c:3698` |
| `0x4037ec6c` | `_frxt_dispatch`, `freertos/FreeRTOS-Kernel/portable/xtensa/portasm.S:451` |
| `0x4037ec62` | `_frxt_int_exit`, `freertos/FreeRTOS-Kernel/portable/xtensa/portasm.S:246` |
| saved restart PC `0x40378250` | `esp_restart_noos`, `esp_system/port/soc/esp32s3/system_internal.c:162` |

These frames identify detection during a context switch, not the original
overwriting instruction. Disregard the invalid/misaligned final SP values
(`0x00000020`, `0x00000000`, etc.). Values such as `0x42011fd4` in the SP column
are not additional PCs to decode. The repeating panic path does not prove a
particular RMT function failed.

## Source-level cause and confidence

`HeartbeatTask` repeated the InputHandler defect: `Telemetry state = {0}` was
an automatic, full-sized queue destination. Telemetry now occupies 1320 bytes
and contains nested fault, Modbus and interlock diagnostics, although the LED
uses only a few status fields. Diagnostic growth in commits including
`adcc1f9` and `9e177b5` increased all automatic consumers of this type.
The previous InputHandler correction missed Heartbeat.

The exact crashing ELF starts Heartbeat with `entry a1, 0x5b0`: **1456 bytes**
are reserved on entry, including the loop's telemetry destination. The
allocation is already present during RMT initialization and the startup log,
even though the telemetry declaration appears later in source. This leaves
only 1616 of the 3072 allocated bytes for callees and RTOS context. In the same
ELF, `esp_log` uses 96 bytes, `vprintf` 48 and `_vfprintf_r` 800, before further
stdio, console, USB and synchronization calls. Individual RMT entry frames
are 48–64 bytes, excluding their nested allocation, driver and error paths.
Simply looking at the three-byte LED payload misses this cumulative usage.

Repeated hardware logs show overflow during or immediately after the LED
initialization message, sometimes with that message cut mid-string. The large
entry frame plus the formatted-log call chain is the leading explanation for
the startup failure. Removing this concrete stack-heavy allocation is the
targeted correction. A context-switch canary panic alone cannot exclude a
write from another task; stable hardware operation is still required before
declaring the reboot defect resolved.

The operator's logs report roughly **263232–263632 free internal heap bytes**,
largest free block **221184 bytes**, after Heartbeat creation. This argues
against heap allocation exhaustion, but is not stack headroom and does not
prove heap integrity. No hardware stack high-water mark was captured before
the crash.

## Complete path and memory review

- Heartbeat creates its RMT TX channel, creates the bytes encoder, enables the
  channel, logs initialization, then peeks the telemetry queue every 100 ms.
  Status interpretation calls `StatusOutput_Get` and `StatusIndicators_Get`;
  control-cycle progress adds a blue pulse. It sends exactly three GRB bytes,
  waits up to 25 ms and delays until the next period. Driver initialization
  errors prevent transmission; channel/encoder handles start at NULL.
- There are no variable-length arrays, recursion, string copies or large text
  buffers in Heartbeat. LED bit timings, channel config and transmit config
  have fixed sizes. Driver configs are consumed/copied by initialization;
  the task owns its handles. RMT's asynchronous encoder reads its payload;
  it does not write a telemetry queue destination.
- A separate driver-contract violation existed: `grb[3]` was loop-local and
  `rmt_tx_wait_all_done` errors were ignored. A timeout could leave the RMT ISR
  reading an expired/reused payload. This is corrected, but does not explain
  a panic cutting off the earlier initialization log, before the first send.
- Queues and event groups are fully created and checked before application
  tasks become runnable. Telemetry's queue item is `sizeof(Telemetry)` and
  every inspected destination uses that full type. The single Control task
  publishes a copied item; readers use independent copies through FreeRTOS
  queue synchronization. An initially empty queue returns false and selects
  the existing unknown/red status. Heartbeat needs no startup-ready barrier
  and issues no motor/output commands. Task priorities/cores are unchanged.
- Control's large context is static. InputHandler and main already have private
  static telemetry copies. PiCommunication has an 8192-byte stack, bounded
  input assembly and a static 12288-byte output buffer with capacity checks;
  no startup evidence points to its later connected-client formatting path.
  OTAValid still has a local telemetry copy on a 3072-byte stack; its branch
  that uses it is delayed 12 seconds and is not reached by this 183 ms panic.
  It remains a separate stack-budget review item, not silently increased here.
- Active task handles are retained only for permanent tasks. OTA tasks use
  lifecycle flags rather than handles that could dangle after self-deletion.
  Shared telemetry string pointers refer to existing constant diagnostic text;
  this change introduces no pointers to another task's local objects.

No concrete cross-task overwrite was found in these inspected paths. This is a
targeted audit, not proof that unrelated firmware cannot corrupt memory.

## Changes

- `HeartbeatTask.c`: task-private static telemetry snapshot, persistent
  three-byte GRB payload, checked transmit/wait results. On a send/wait failure,
  disable further RGB submissions and leave the payload untouched for any
  delayed ISR. The non-safety task continues running; no retry rewrites memory
  still owned by RMT. Allocations on initialization failure are not repeatedly
  retried or freed while hardware could be using them.
- `AppConfig.h` and `ApplicationTasks.c`: default Heartbeat stack remains 3072
  bytes; `HEARTBEAT_STACK_BYTES` permits a controlled bench-only override.
  `HEARTBEAT_DIAGNOSTIC_MODE=1` runs only the task delay; mode 2 retains telemetry
  and status computation without RMT; mode 0 remains normal/default.
  `HEARTBEAT_DIAGNOSTICS=1` adds per-cycle heap-integrity checks and stack logs
  in modes 0/2. These diagnostics add overhead and are disabled by default.
- `sdkconfig.defaults`: retain canary detection, enable the end-of-stack
  watchpoint and light heap poisoning. The generated local
  `sdkconfig.waveshare_s3` was updated as well, and the actual generated
  firmware header was checked. Existing generated SDK configs elsewhere need
  these settings applied with menuconfig; defaults do not override saved values.
- `main.c`: heap-integrity checks after application creation and every five
  seconds, plus a Heartbeat minimum-free warning below 512 bytes. Existing
  monitoring reports input/sensor/control/Pi/heartbeat/main stack minima in
  ESP-IDF bytes. Checks panic on corruption rather than continuing silently.
- Host RMT/heap adapters, real Heartbeat tests, controlled stack-creation test,
  and target frame-budget compiler checks were added/updated.

No safety input mapping, safety chain, emergency stop, interlock, commissioning
value, motor start authorization or fail-safe output initialization was changed.
No hardware commands, flashing or motor activation were performed.

## Measurements and validation

| Measurement | Crashing image | Rebuilt normal image |
| --- | ---: | ---: |
| Heartbeat allocated stack | 3072 bytes | 3072 bytes |
| Heartbeat compiled entry frame | 1456 bytes | 128 bytes |
| Allocation minus entry frame (not runtime headroom) | 1616 bytes | 2944 bytes |
| Static RAM, PlatformIO | 56444 bytes | 57772 bytes |
| Flash, PlatformIO | 568929 bytes | 571177 bytes |

The normal task frame is reduced by **1328 bytes**; no task's default stack
allocation increased. Target-compiled isolation frames are 32 bytes (delay-only)
and 48 bytes (telemetry-only with diagnostics). The normal diagnostic/6144-byte
variant retains a 128-byte frame. These are compiler measurements, not measured
hardware headroom. Heap poisoning adds per-allocation overhead not represented
by static RAM totals; monitor the actual runtime heap on the bench.

- ESP-IDF 5.5.0 `waveshare_s3` firmware build: **PASS**.
- Firmware C host tests: **36/36 executables PASS**, including the real
  Heartbeat normal/error paths, both isolation modes, and startup with only
  Heartbeat's requested stack changed to 6144. Host RMT tests retain a pending
  payload on timeout and verify subsequent cycles do not overwrite or resubmit
  it. Host heap values are synthetic, not ESP32 measurements.
- Target compiler frame budget: **6/6 PASS**, for InputHandler, Heartbeat,
  delay-only, telemetry-only, diagnostic/6144 and 6144 task creation. The check
  rejects frames above 512 bytes; it does not bound callee stack use.
- Existing I/O compiler configuration checks: **7/7 PASS**.
- Python unittest discovery: acquisition ownership passes; four existing
  source-layout subtest failures remain in unchanged DigitalOutputs, I2CBus,
  SafetyIO and StateMachine files. No unrelated safety source formatting was
  changed to suppress those failures.
- `git diff --check`: **PASS**.

Build/test logs: `output/heartbeat-{stack-build,host-tests,frame-tests,io-tests,python-tests}.log`.
Normal output: `ESP32/.pio/build/waveshare_s3/firmware.{elf,bin}`. New ELF SHA-256:
`2c63a0a401ac0930686e6a05c3ee686019c3e583aeed5df0516e89768e63cb0a`.

## Physical validation still required

**No post-change hardware boot test has been performed. Stable startup and the
absence of repeated resets are not established.** Host tests and cross-compiles
cannot reproduce ESP32 interrupt/stdio stack depth or prove isolation makes a
physical boot stable.

Keep the motor/VFD independently isolated and all safety/actuation outputs
inactive during bench tests. Do not authorize physical START. Preserve each
tested BIN/ELF and SHA before rebuilding. Capture cold boots and sustained runs,
all seven creation reports, ready bits, actual minimum-free bytes and heap
integrity results. Exercise RGB initialization, unavailable and changing
telemetry, communication load and RMT failure paths. Record InputHandler,
Heartbeat and other monitored task minima, free internal heap and largest block.

Use one changed variable at a time, keeping the same SDK diagnostics:

1. Rebuilt normal mode, 3072-byte Heartbeat: verify whether the targeted change
   eliminates the observed startup panic and provides sustained margin.
2. Delay-only mode (`HEARTBEAT_DIAGNOSTIC_MODE=1`), then telemetry-only mode
   (`=2`), each with the same stack: distinguish the RMT/logging path from
   task/queue activity. Boot stability for these modes is currently unknown.
3. Normal mode, only Heartbeat changed to 6144 bytes: compare minimum-free
   readings against the 3072-byte build. Similar used-stack estimates support
   stack pressure; persistent poisoning failures or resets require investigation
   of the first faulting write. An increase that masks a panic does not exclude
   corruption. Reproducing the original local-snapshot build at 6144, with the
   motor isolated, would provide an additional direct comparison; that build
   has not been run here. Return to 3072 after the comparison.

For example, in PowerShell with PlatformIO available, build (without uploading)
the controlled stack variant:

```powershell
$env:PLATFORMIO_BUILD_FLAGS = '-Wall -Wextra -Iinclude -DHEARTBEAT_STACK_BYTES=6144 -DHEARTBEAT_DIAGNOSTICS=1'
pio run -d ESP32
# Preserve the output before selecting another variant.
Remove-Item Env:PLATFORMIO_BUILD_FLAGS
pio run -d ESP32
```

For isolation, substitute `-DHEARTBEAT_DIAGNOSTIC_MODE=1` or `=2` for the stack
override. Rebuild with the variable removed for normal operation. If a previous
`PLATFORMIO_BUILD_FLAGS` value exists, save and restore it instead of removing it.
Light poisoning checks allocation boundaries; the watchpoint covers the current
task's stack boundary and cannot catch every arbitrary cross-task overwrite.
If corruption remains, use a separate comprehensive-poisoning diagnostic build
and decode its first exception with that build's own ELF. Do not reuse the old
panic's addresses with a newly linked image.
