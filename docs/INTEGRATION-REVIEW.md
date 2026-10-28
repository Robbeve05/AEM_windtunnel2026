# Integration branch — R.E. van Eekelen

- One authoritative installer: `install_windtunnel_pi.sh`; old duplicate launchers removed.
- Physical-only START, STOP, RESET and E-stop; network commands only for mode and setpoints.
- Manual V20 mode: 0–20 Hz reference with 2 Hz/s software ramp; change mode only in READY; does not bypass hardwired safety or physical START.
- Waveshare GPIO38 RGB heartbeat: RMT output status only, not safety-rated.
- Initial DO latch written to zero before output-direction configuration.
- OTA is **not commissioned** and **not released for real hardware**. Its existing condition requires a separate service input and remains inconsistent with the proposed E-stop condition. This must be reviewed, fixed and tested before OTA is enabled.
- The V20 50 Hz reference, output polarity, Modbus, ESP32 PlatformIO compilation, Pi integration and real physical commissioning remain UNVERIFIED. Keep `SAFETY_IO_COMMISSIONED=0` and `V20_RUN_COMMISSIONED=0`; never bypass these for testing.
- This is a review/bench branch; do not merge into deployed systems before successful tests.
