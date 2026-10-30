# Windtunnel final firmware integration

Author: R.E. van Eekelen

Baseline: origin/main b6753ad7e843fcc7da5d5ecfb465e4cd1ac7b240.
The refactored ESP32 architecture and complete main HMI are retained.
Bench sources supply incremental pressure acquisition, authenticated OTA and WS2812 heartbeat.

## Protocol and operation

Newline-delimited TCP on port 5000 supports PING, SETPOINT <m/s>, PID <kp> <ki> <kd>,
FILTER <Hz>, MODE AUTO, MODE MANUAL, FREQ <Hz>, STOP.
START and RESET are rejected; only fresh physical actions may start or recover the machine.
MODE is rejected while RUNNING or STOP and never starts the motor; switching clears frequency demand
and controller history. FREQ is valid only in MANUAL and in 0..50 Hz. SETPOINT stays in 0..30 m/s.
Queued command acknowledgements report queue admission; STATUS reports the authoritative accepted mode/demand.
The consumer rechecks state/mode to prevent authorization races with physical inputs.

AUTO retains the existing PI gains and airspeed quintic curve. MANUAL bypasses the PI and uses a
separate SetpointCurve instance. The duration is the maximum of the minimum interval,
1.875*abs(delta)/2, sqrt((10/sqrt(3))*abs(delta)/0.5), and cbrt(60*abs(delta)/0.5).
The frequency limits are 2 Hz/s, 0.5 Hz/s2 and 0.5 Hz/s3. A 0..50 Hz transition takes at least
46.875 s. Active trajectories finish before the latest requested target begins, preserving C2 continuity.
Initial frequency comes from valid fresh VFD feedback; invalid, stale or out-of-range feedback faults
before manual run authorization. The final control output and V20 request boundary enforce 50 Hz.
No frequency-to-airspeed conversion is used for actual measurement or control.

STOP disarms and requests the existing OFF1; drive feedback confirms controlled deceleration.
FAULT cancels the trajectory and run authorization immediately and retains the physical recovery policy.
K2 remains the existing DO1 operational output. Independent safety hardware remains authoritative.
SAFETY_IO_COMMISSIONED and V20_RUN_COMMISSIONED remain disabled by default.

## Acquisition and tasks

A shared GPTimer has alternating 2.5 ms notification phases: sensors on Core 0 and control on Core 1.
Each task receives a 5 ms cadence; the control loop remains 200 Hz. The timer starts after the existing
startup barrier. No ISR or control function accesses I2C. Missed notifications are not replayed.
SensorTask owns incremental pitot and tap acquisition; sensor mutex acquisition never waits, and each
bus transaction retains I2C_TIMEOUT_MS. One pitot read and at most one tap transaction occur per slot.
Ten valid pitot reads produce one 20 Hz averaged sample; 200 readings establish startup zero calibration.
Filtering uses elapsed measurement time. Failed reads do not publish a valid zero or renew the last
successful sample timestamp; SENSOR_TIMEOUT_MS inhibits operation on prolonged loss.
Tap mux selection, conversion, delayed read and mux reset occupy separate slots, with optional sensor
flags and address-collision checks retained. Pressure taps remain optional/uncommissioned by default.

Heartbeat uses the existing StateMachine telemetry at priority 1, below control and input handling.
OTA and rollback validation also run at priority 1. OTA remains disabled, uses a fresh HMAC challenge,
SHA256 firmware integrity verification and inactive-slot installation. Physical maintenance, independent
motor isolation, contactor feedback, emergency-stop and safety feedback are required throughout reception.
OtaLocalConfig.h is optional and ignored by Git; no key or default password is supplied.
The partition table requires a one-time USB installation; see OTA-SETUP.md.

STATUS includes state, mode, manual_hz (requested), applied_hz (ramped), actual_hz (VFD feedback),
pv (pitot), sp (AUTO target), sensor_ok, motor_ok, safety_relay_ok, estop_pressed, reset_required,
pi_connected and the existing diagnostic fields. Invalid/stale measurements are marked through validity
and timestamps; the HMI shows UNKNOWN when disconnected and rejects stale commands.

## Software validation

Run ESP32/tests/run_host_tests.ps1 with Visual Studio C++ tools. It compiles production sources with
host-only RTOS/bus mocks using /W4 /WX, including both commissioned and inhibited V20 variants,
protocol, quintic derivatives, manual/automatic control, physical START freshness, startup, fault,
OFF1, timeouts, phased acquisition, calibration and OTA authorization checks.
Run python -m unittest discover -s tests -v from HMI with its requirements installed.
Run pio run -e waveshare_s3 and pio run -e waveshare_s3_sim from ESP32.
These are software checks; none constitute physical installation acceptance.

## Remaining physical commissioning

Verify Ruck EL 710 D4 02 article 146531 motor and fan nameplates, V20 current/voltage/frequency,
50 Hz reference and maximum, OFF1 deceleration and watchdog. Verify all GPIO polarities and wiring,
independent emergency stop, reset feedback, fresh physical START, K2 DO1 and actual drive feedback.
Calibrate pitot in still air and validate 2.5 ms sensor phase, I2C timeout load and control deadlines
on the real board. Verify optional pressure sensor part/address/scaling before enabling it.
Provision and physically test OTA authorization, isolation, abort and rollback before enabling OTA.
No live motor operation or physical safety claim follows from these software tests.

Native and browser HMI controls both enforce 0..50 Hz. Per-tap timestamps expire after 2 s.
The existing simulated measurement export/recalculation regression is restored only for recordings
explicitly marked simulation in their saved metadata. Real pressure-tap Cp processing remains unavailable.
