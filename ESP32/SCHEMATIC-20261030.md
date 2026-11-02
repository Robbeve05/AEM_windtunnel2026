# 30 October 2026 electrical schematic update

Scope: the DI1..DI5 assignments supplied for the revised schematic. Software-only
verification; no physical wiring, shutdown timing, relay operation or safety validation
is claimed. No firmware was uploaded.

| Terminal | GPIO | Contact / active level | Action |
| --- | --- | --- | --- |
| DI1 Emergency Stop | 4 | NC / LOW | Latch FAULT, remove VFD enable |
| DI2 Reset | 5 | NO / HIGH | Debounced rising edge requests recovery |
| DI3 Start | 6 | NO / HIGH | Debounced rising edge, accepted only in READY |
| DI4 Stop | 7 | NC / LOW | Falling edge starts controlled deceleration |
| DI5 Safety | 8 | NC / LOW | Latch FAULT, remove VFD enable |

The existing `*_OK_ACTIVE_LEVEL` names describe the **healthy** level: Safety and
Stop OK therefore remain HIGH. Emergency Stop's pressed/tripped level is LOW.
`MachinePins.h` owns pins and `AppConfig.h` owns polarities. Optional DI6/DI7,
outputs, buses and independent hardware shutdown wiring are unchanged.

INIT validates fresh input data and closed DI1/DI4/DI5 circuits. Open Emergency
Stop or Safety latches its corresponding fault; open Stop during INIT latches
INTERLOCK. Unknown/stale input data cannot complete initialization. WAIT requires
a new Reset press; Reset recovery never arms the VFD. Held buttons at boot,
release edges and stale/coalesced Start notifications cannot start the motor.
After recovery the path remains FAULT -> WAIT -> READY, with a new Start required.
A held Reset blocks Start. Safety input pulses captured by the ISR remain latched
after the contact returns healthy, until physical Reset recovery acknowledges them.

Normal operation remains INIT -> WAIT -> READY -> RUNNING -> STOP -> READY.
STOP seeds a separate instance of the existing fifth-order `SetpointCurve` from
the current frequency command, immediately replacing any unfinished running
profile. It decelerates to zero in AUTO and MANUAL with the existing frequency
slew, acceleration and jerk limits. Command value is continuous at entry; this
zero-endpoint-derivative profile does not promise derivative continuity when
interrupting an accelerating segment. New operating points and Start during STOP
cannot retarget the stop. The existing drive arm and K2 permissive are retained
only while healthy; reaching zero disarms and sends OFF1. READY requires fresh
OFF1/zero-speed confirmation. Stop release does not cancel the ramp or restart.

At 50 Hz and the existing 2 Hz/s limit, the quintic needs 46.875 seconds.
The total STOP timeout is now 80 seconds to include drive confirmation; changes
to profile limits require rechecking that bound. Emergency Stop, Safety and other
faults bypass the profile, disarm the V20 and publish zero demand/no permissive.
The input task's direct inhibition check remains independent of the control
request, while actual emergency shutdown must remain independent of firmware,
I2C, Ethernet and Modbus. Commissioning gates remain disabled by default.

The native HMI distinguishes controlled STOP from latched Safety faults and
shows the revised Start/Reset/Stop terminals. The Python simulator uses the six
firmware states and exposes `set_input(di, high)` for electrical-level injection.
Its Start and Reset helpers generate simulated debounced presses; the Reset HTTP
route is simulation-only and cannot reset hardware. Fault status is surfaced in
simulation as well as live mode. Simulated dynamics and drive confirmation are
illustrative and are not a motor or safety validation model.

## Hardware-independent verification

- `ESP32/tests/run_host_tests.ps1`: production GPIO decoding and ISR pulse latches;
  all six states and normal/recovery transitions; NC inputs during INIT; faults
  from every state; stale/held/bouncing buttons; AUTO/MANUAL full-range quintic
  stops; no early OFF1/confirmation; faults during deceleration; no restart.
  Existing V20, output, startup, controller, calibration and sensor regressions
  remain in the suite. All cases compile with MSVC `/W4 /WX`.
- From HMI: `python -m unittest discover -s tests -v`, including simulated input
  sequences, simulation-only Reset authorization and offscreen native HMI messages.
- From ESP32 in a path without spaces: `pio run -e waveshare_s3 -e waveshare_s3_sim`.
  Compilation only; no upload or commissioning.

Results on 2 November 2026: all 19 C host configurations passed, all 50 HMI tests
passed (including native Qt offscreen tests), and both PlatformIO environments
compiled successfully. The HMI run used WindowsSelectorEventLoopPolicy for the
suite's loopback TCP tests after the Windows Proactor runner stalled during a
repeat run; no application networking behavior was changed. Existing dependency
deprecation/async teardown warnings are not physical validation results.
