# Integration review — R.E. van Eekelen

Branch: `integration/windtunnel-bench-ready`. This is an **uncommissioned** development snapshot, not permission to energize the V20.

## Code review

- Physical-only START/STOP/RESET/E-stop; no remote motor starting.
- Manual V20 request limited to 20 Hz, with a nominal 2 Hz/s ramp; verify the actual V20 P2000 before use.
- OTA precondition corrected: E-stop pressed, safety relay dropped, separately verified isolation input, contactor feedback fitted and low, physical safety commissioning. OTA additionally requires a local maintenance signal and authenticated request. OTA remains disabled by default.
- Digital output latch (0x01) and direction (0x03) are now read back after writing zero. **Verify the expander register map and output polarity against the exact board revision**.
- Raspberry Pi installer tracks this integration branch, preserving config and measurements; firmware upload is opt-in only.
- Physical safety circuit must independently inhibit motor power/permissive, irrespective of firmware.

## Test limitations

- Both PlatformIO targets (`waveshare_s3_sim`, `waveshare_s3`) require a PlatformIO/ESP-IDF environment with downloaded packages; compilation has **not been verified** in this change.
- Raspberry Pi dependency-based tests and native Qt GUI checks have **not been verified** in this change.
- Ethernet, GPIO38 WS2812, physical buttons, I2C expander, pressure sensors, RS-485 V20 and OTA rollback require isolated bench verification.
- The service installer requires a real Raspberry Pi systemd user session and must be checked on-device.

## Before Friday's physical commissioning

1. Run `cd ESP32 && pio run -e waveshare_s3_sim && pio run -e waveshare_s3`.
2. Run `cd HMI && python -m unittest discover -s tests -v` in a dependency-equipped virtual environment.
3. Verify NC E-stop gives a safe low voltage on actuation; independently prove relay latching and no auto-restart.
4. Verify all actual input polarities, output inactive polarity, register map and power-up behavior (with the VFD inhibited).
5. Verify V20 P2000, control/status words, Modbus scaling, OFF1 and fault monitoring via simulation before physical RUN tests.
6. Verify physical isolated feedback for OTA, HMAC exchange, partition/rollback, sensor freshness, watchdog and network loss.
7. Keep `SAFETY_IO_COMMISSIONED=0` and `V20_RUN_COMMISSIONED=0` until signed-off physical checks. Do not energize the fan from this branch.
