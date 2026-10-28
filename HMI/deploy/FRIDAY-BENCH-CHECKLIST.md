# Friday bench verification — ESP32-S3 + native Raspberry Pi HMI

Author: R.E. van Eekelen

**This is a commissioning checklist, not evidence of a passed test. The VFD must stay independently inhibited while checking communication.**

## 1. Build and load

Use GitHub branch `feature/raspberry-pi-ethernet-hmi` for **both** ESP32 and Pi.

On the development PC (PlatformIO):
```sh
cd ESP32
pio run -e waveshare_s3_sim
pio run -e waveshare_s3
```
Do not assume an executable exists or is valid until the builds pass. The real firmware is intentionally set to `SAFETY_IO_COMMISSIONED=0`, `V20_RUN_COMMISSIONED=0`, and remote start disabled. These protections must not be bypassed to make a demonstration run.

On the Pi (for an existing checkout at ~/AEM_windtunnel2026):
```sh
cd ~/AEM_windtunnel2026
git fetch origin
git switch feature/raspberry-pi-ethernet-hmi
bash HMI/deploy/update-pi.sh
```
The update preserves measurements and migrates legacy configuration to ESP32 Ethernet.
A clean installation can use `HMI/deploy/setup-pi-from-github.sh`, but **it moves/backs up existing checkout and HMI paths**, so use the update route on an existing Pi.

Set `esp32_host` in `~/HMI/config/config.json` to the actual reachable ESP32 IP. The firmware uses DHCP by default; **192.168.50.10 is only a placeholder**. TCP port is 5000. Restart:
```sh
systemctl --user restart windtunnel-hmi.service
journalctl --user -u windtunnel-hmi.service -n 70 --no-pager
```
Open the existing desktop launcher; it starts **PySide6/Qt**, not Chromium. All live controls are on the right sidebar.

## 2. Simulated ESP32 and Pi test (motor isolated)

- With sim firmware, verify `state`, `fault`, `pv`, `sp`, `actual_hz`, `sample_seq`, `control_hz`, `overruns` and `missed_deadlines` in the native HMI.
- The physical START/RESET requirement is retained; simulated events are for test only.
- Check setpoint and operation STOP on the native Qt panel; STOP is an operational request, not an E-stop.
- Disconnect Ethernet: UI must display UNKNOWN and explicitly warn the fan may still rotate.
- Verify that `sample_seq` changes about 20 times/s; that the Pi logs each pitot sample only once; that recording and USB export work.

## 3. Real ESP32 checks with VFD independently inhibited

- Verify all electrical wiring/polarities and test emergency contactor circuit separately.
- Verify pitot zero calibration **with still air**: 200 valid pitot reads are required (around 1 s at 200 Hz).
- Verify 5 ms control timing: `control_hz` roughly 200, `execution_us < 5000`, and `overruns` and `missed_deadlines` remain stable at zero.
- Check Core 0 pressure tap task does not disturb Core 1 pitot, particularly under electrical noise.
- V20 protocol in the code is **9600 baud, 8E1, slave address 1**, matching the documented V20 settings. Check the actual inverter's active parameter set. P0700/P1000/P2023/P2021/P2010/P2034/P2035/P2000 must agree with the documentation.
- No commissioning or independent hardwired safety checks are completed by uploading code.

## Not yet validated

No successful real ESP-IDF firmware build, native Qt startup on the actual Pi, physical ESP32–Pi TCP test, Modbus transaction with the actual V20, or measured 200 Hz timing has been reported by this review. Existing ESP32 host test scripts still reference obsolete V20 source/API and need updating before they can serve as release evidence.

Do not interpret passing offline tests as permission to enable the 5.5 kW fan.
