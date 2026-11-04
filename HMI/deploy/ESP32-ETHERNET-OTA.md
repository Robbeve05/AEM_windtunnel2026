# ESP32-S3 Ethernet HMI and firmware update preparation

Author: R.E. van Eekelen

## HMI migration

The Raspberry Pi HMI now supports ESP32-S3 Ethernet as the only physical acquisition path. Simulation remains available. Arduino Uno is not used in the active HMI backend. The Pi opens a persistent TCP connection to the Waveshare W5500 ESP32 at `192.168.50.2:5000` and reads newline-delimited `STATUS key=value` messages. It sends `PING` every 500 ms and reconnects on failure. If telemetry is stale, display UNKNOWN; **never infer that a disconnected fan is stopped**.

The dedicated link uses Pi `eth0=192.168.50.1/24` and ESP32 `192.168.50.2/24`, with DHCP disabled and no gateway. Set `esp32_host=192.168.50.2`, `esp32_port=5000`, and `simulation=false` in `HMI/config/config.json`. The deployment preserves the Pi's site config, so verify `~/AEM_windtunnel2026/HMI/config/config.json` after updating; the config migrator converts the previous placeholder `.10` to `.2` while preserving other configured hosts. The ESP32 is the TCP listener; the Pi backend initiates and retries the connection. The Pi's WiFi, internet and Tailscale are not part of this link. The HMI reports ONLINE only with fresh STATUS and a fresh ESP32-observed Pi heartbeat; otherwise its controller status remains offline/unknown.

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
2. Implement a dedicated, lower-priority OTA task on Core 0 using `esp_https_ota` / `esp_ota_ops`. It must not block or starve the Core 1 / 10 ms control task.
3. Require a **physical, maintained and verified maintenance key input**, no RUNNING/STOP state, independent isolation of motor power/enable, physical standstill inspection, fresh safety feedback, and explicit operator action. Do not accept an HMI-only check as proof that the motor is safe.
4. Serve the firmware through an authenticated, integrity-verified TLS channel with a pinned trust anchor or verified signed firmware. A standalone SHA-256 served alongside a binary is not authentication by itself.
5. Perform app validation on first boot, automatic rollback after watchdog, reset, failed readiness self-test or other crash, and ensure default state after every update is INIT / inhibited / requires physical Reset + Start.
6. Add hardware fault-injection and simulated update tests; verify power loss mid-write, corrupted image, version rollback, link loss, motor interlock, and recovery via wired port.
7. Only after all of these are verified add a Pi UI "Install update" action. Never allow remote START or automatic restart.

## Useful checks

```sh
cd HMI
python3 -m unittest discover -s tests -p 'test_esp32_ethernet.py'
```

From ESP32 project:
```sh
pio run -e waveshare_s3
pio run -e waveshare_s3_sim
```

Neither physical operation nor timing guarantees are established by source inspection alone.
