# Windtunnel ESP32-S3 controller

Firmware branch: `esp32`. Paired Pi HMI branch: `RaspberryPi`.

The industrial Waveshare migration retains the existing Tustin PI, quintic setpoint ramp, text protocol and V20 Modbus register implementation. It replaces the external clock with a 200 Hz internal GPTimer, Pi UART with W5500 TCP Ethernet, and the placeholder sensor with the existing ASPD-4525 conversion/filtering. Future 16-channel ABP2 acquisition is gated until PCB addresses and ranges are verified.

Read [MIGRATION.md](MIGRATION.md) for architecture analysis, verified pin assignments, configuration, address conflict, wiring, fault behavior and ordered commissioning procedure. Read [V20_MODBUS.md](V20_MODBUS.md) for drive settings.

Build in a local path without spaces:

```
pio run -e waveshare_s3
pio run -e waveshare_s3_sim
```

Run Windows host regression tests with `./tests/run_host_tests.ps1`. Hardware is never started by flashing; the pitot must finish calibration, the drive must be ready and an explicit START is required. No physical commissioning or motor test is implied by a successful build.


Source layout: application/driver implementations live in src/, public headers
in include/, and host tests with hardware mocks in tests/. Button validation
is private to InputHandlerTask.c; timing accounting is private to ControlTask.c.

The host suite compiles the current V20 driver with a mocked UART, both with and
without the test-only run commissioning flag. It covers asynchronous responses,
OFF1, stop confirmation, faults and explicit recovery. The production V20 driver
does not use request epochs as an independent authorization check: ControlTask
publishes state epochs and owns arm/disarm transitions. Tests verify that a new
epoch cannot re-arm a disarmed driver. The default hardware build remains
uncommissioned.

Startup ready bits describe successful local initialization, not sensor validity
or a connected Pi. The bounded startup barrier keeps output disabled on missing
readiness; no timer starts in that failure path. A failed initial output write or
sensor initialization does not publish readiness.
