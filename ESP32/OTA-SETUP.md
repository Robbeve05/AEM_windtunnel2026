# Maintenance-gated OTA setup (Waveshare ESP32-S3 / W5500)

Author: R.E. van Eekelen

## Status and critical limitations

OTA receiver and Pi uploader are implemented in code, but **have not yet been compiled or validated on the physical Waveshare, the Raspberry Pi or the Siemens V20**. The OTA endpoint is **disabled by default**, and hardware isolation must be commissioned independently.

The PoE port supplies power and Ethernet network access; it does **not** implement OTA by itself. You still need a **one-time USB flash**, including the new partition table, before an Ethernet update can work.

Do not deploy OTA while the drive can run. Writing flash competes with CPU time, interrupt service and networking. This system does not claim a safety-rated OTA or emergency stop.

## 1. Safety interlock and secret provisioning

Physically implement and verify two **independent inputs**:
- **maintenance switch** indicating service mode;
- **motor isolated feedback** from a verified independent isolation device (not an ESP32 software output or VFD status word).

Both must remain asserted throughout OTA. Physical energy isolation must also be verified separately according to the lab's lockout procedure. The software checks are supplementary, never a substitute for that procedure.

Select actual safe, spare GPIO pins **only after checking Waveshare pin mappings and available connectors**. Current board GPIOs have assignments for Ethernet, safety DI, I2C and Modbus; do not reuse occupied pins. Do not wire 24 V directly to ESP32 GPIOs: use appropriate rated isolation and level shifting. If no available safe spare GPIOs exist, OTA maintenance inputs need a properly designed isolated interface; the code must be adapted and validated before enabling OTA.

Generate a per-device secret on a trusted workstation:
```sh
openssl rand -hex 32
```
It is a 64-character hexadecimal string. Store this exact ASCII string as a private file on the Pi at:
```sh
mkdir -p ~/.config/windtunnel
printf '%s' 'REPLACE_WITH_THE_GENERATED_64_CHAR_HEX' > ~/.config/windtunnel/ota_secret
chmod 600 ~/.config/windtunnel/ota_secret
```
Use the **same** ASCII string in local file `ESP32/include/OtaLocalConfig.h` (this file is excluded by .gitignore):
```c
#pragma once
#define OTA_MAINTENANCE_ENABLED 1
#define OTA_MAINTENANCE_GPIO (-1) /* REPLACE with verified spare isolated input */
#define OTA_MAINTENANCE_ACTIVE_LEVEL 1
#define OTA_MOTOR_ISOLATED_GPIO (-1) /* REPLACE with verified spare isolated feedback */
#define OTA_MOTOR_ISOLATED_ACTIVE_LEVEL 1
#define OTA_SHARED_SECRET "REPLACE_WITH_THE_GENERATED_64_CHAR_HEX"
```
**The -1 placeholders intentionally fail compilation.** Never enable this until the actual wiring and input polarity are confirmed by test. Keep the secret outside Git.

For a Pi-initiated build, copy the **private** local header into the Pi repository at the same path, with permissions 600, after the inputs are verified. It is not included in Git updates.

## 2. First flash with USB

On a computer with PlatformIO installed:
```sh
cd ESP32
pio run -e waveshare_s3
pio run -e waveshare_s3 -t upload
```
This uses `ESP32/partitions.csv` with one factory app and two OTA application slots. **The first upload updates bootloader, partition table and application; previous flash partition contents may be lost.** Back up calibration/configuration first.

Do not try an OTA update to install the new partition table; application OTA cannot change a partition table.

## 3. Update the Pi's native desktop

Run from the checked-out repository:
```sh
bash install_windtunnel_pi.sh
```
This updates the Pi HMI without modifying the ESP32.

Update both, including firmware, **only after physical interlocks, first USB flash and build have passed**:
```sh
bash install_windtunnel_pi.sh --ota
```
The script builds with PlatformIO on the Pi, requires an explicit UPDATE confirmation, authenticates with HMAC-SHA256, checks image SHA-256 on the ESP32 and writes the inactive OTA slot. It does not remotely START/RESET the fan.

The network must be isolated or trusted: this custom protocol uses challenge-response integrity/authentication, **not TLS encryption**. Anyone sniffing traffic can see the firmware image. The private key must be kept secure.

## 4. Verify and recover

- Confirm the Pi can connect to ESP32 port 5000 and the OTA server (port 5001) is accessible **only in the commissioned service mode**.
- Confirm `control_hz` near 200 Hz; no climbing `overruns` or `missed_deadlines` after the reboot.
- Keep the motor isolated until boot and all physical safety tests pass.
- Rollback: after an OTA boot, firmware waits 12 seconds and accepts it only when control frequency is 185–215 Hz and timing counters remain zero; otherwise it requests bootloader rollback. This is basic boot validation, **not full system acceptance**.
- If firmware or network is corrupted or recovery fails, use USB reflash; do not force outputs.
- No secret key is distributed in Git. Only use known tested images and review code before remote firmware updates.
