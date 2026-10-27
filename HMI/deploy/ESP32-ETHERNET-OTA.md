# ESP32-S3 Ethernet HMI and firmware update preparation

Author: R.E. van Eekelen

## HMI migration

The Raspberry Pi HMI now supports ESP32-S3 Ethernet as the only physical acquisition path. Simulation remains available. Arduino Uno is not used in the active HMI backend. The Pi opens a persistent TCP connection to the Waveshare W5500 ESP32 on port 5000 and reads newline-delimited `STATUS key=value` messages. It sends `PING` every 500 ms and reconnects on failure. If telemetry is stale, display UNKNOWN; **never infer that a disconnected fan is stopped**.

In `HMI/config/config.json`, configure `esp32_host` as the reachable ESP32 IP, `esp32_port=5000`, and `simulation=false`. The default `192.168.50.10` is only an example. The ESP32 currently gets its address through DHCP unless static IP is enabled separately. Check its boot log or router DHCP lease before changing the Pi setting. Connect both devices via a local Ethernet switch or compatible isolated LAN.

The browser shows state, fault, E-stop, drive frequency, and operational STOP. Physical START and RESET stay physical. Setpoints require fresh telemetry. TCP port 5000 is not encrypted or authenticated: isolate the machine LAN and do not expose it to the internet.

## Firmware file transfer (preparation implemented; remote flash NOT implemented)

After a one-time USB firmware flash containing an actual OTA receiver and an OTA partition table, an ESP32-S3 could accept future firmware images via Ethernet.

The current `esp32` firmware **does not implement an OTA receiver**. Merely copying a file onto the Pi cannot make the ESP32 execute it.

On the build machine:
```sh
cd ESP32
pio run -e waveshare_s3
```
Then copy the resulting `.pio/build/waveshare_s3/firmware.bin` to the Pi (for example via `scp`).

On the Pi, stage it and verify its image magic, length and SHA-256:
```sh
cd ~/AEM_windtunnel2026/HMI
python3 tools/stage_firmware.py /path/to/firmware.bin
cat ~/windtunnel-firmware-staged/firmware.json
```
**Staging does not transfer firmware to the ESP32 or change its flash.**

## Remaining engineering work for real OTA

1. Add an ESP-IDF partition table with `otadata` and two application slots (`ota_0`, `ota_1`), sized to accommodate the actual application, and a rollback policy. Re-partitioning may require a one-time wired flash that erases existing flash content.
2. Implement a dedicated, lower-priority OTA task on Core 0 using `esp_https_ota` / `esp_ota_ops`. It must not block or starve the Core 1 / 5 ms control task.
3. Require a **physical, maintained and verified maintenance key input**, no RUNNING/STOP state, independent isolation of motor power/enable, physical standstill inspection, fresh safety feedback, and explicit operator action. Do not accept an HMI-only check as proof that the motor is safe.
4. Serve the firmware through an authenticated, integrity-verified TLS channel with a pinned trust anchor or verified signed firmware. A standalone SHA-256 served alongside a binary is not authentication by itself.
5. Perform app validation on first boot, automatic rollback after watchdog, reset, failed readiness self-test or other crash, and ensure default state after every update is INIT / inhibited / requires physical Reset + Start.
6. Add hardware fault-injection and simulated update tests; verify power loss mid-write, corrupted image, version rollback, link loss, motor interlock, and recovery via wired port.
7. Only after all of these are verified add a Pi UI "Install update" action. Never allow remote START or automatic restart.

## Useful checks

```sh
python3 -m unittest HMI.tests.test_esp32_ethernet
```

From ESP32 project:
```sh
pio run -e waveshare_s3
pio run -e waveshare_s3_sim
```

Neither physical operation nor timing guarantees are established by source inspection alone.
