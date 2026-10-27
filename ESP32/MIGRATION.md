# Waveshare industrial controller migration

## Existing architecture and retained behavior

The starting `esp32` branch used Arduino-ESP32 2.0.17/IDF 4.4, an external GPIO4 clock, UART1 Pi text commands, a placeholder real sensor driver and a working Siemens RTU driver. The Pi actually acquired an ASPD-4525 through an Arduino Uno; its unused ESP32 adapter expected incompatible JSON. Both web and native Qt HMI had fan controls disabled. Cp calculation helpers exist, but recording/recalculation of Cp was already disabled pending the pressure PCB.

The migration retains Regelaar.c/h (including Tustin gains, anti-windup and 1 ms sample time), SetpointCurve.c/h, V20 register/scaling logic, command syntax, queue-based sharing, measurement files, export workflow, Arduino fallback and Cp helper. No control gains were retuned. The default 30 m/s setpoint limit remains. The plant parameters are still assumptions requiring commissioning.

Changed: build/entry point, AppConfig and centralized BoardConfig, timer trigger and diagnostics, task placement, sensor HAL, Ethernet transport, V20 UART transport, and corresponding Pi adapter/API/desktop/web views. New PressureSensors.c supplies the real pitot driver and gated future tap acquisition. SafetyTask monitors the configured operational DI and stale control requests.

## Build and tasks

`pio run -e waveshare_s3` builds real hardware. `pio run -e waveshare_s3_sim` builds simulation: no real motor or sensor I/O. Both use pinned PlatformIO espressif32 6.12.0 / ESP-IDF 5.5.0. Arduino was only the entry-point wrapper; IDF 5 is necessary for GPTimer. Windows IDF rejects spaces in project paths: build from a short local path without spaces, not the UNC share. PSRAM is deliberately unused; the board is N16R8 and flash image setting is 16 MB.

| Task | Core / priority | Timing / work |
|---|---|---|
| ControlTask | 1 / 4 | 1 MHz GPTimer, alarm 1000, reload 0: nominal 1 kHz; fixed 1 ms algorithm |
| SensorTask | 0 / 3 | Nominal 25 Hz, bounded I2C, ten pitot readings and pressure filtering |
| ActuatorTask (Modbus) | 0 / 3 | Nominal 10 Hz; never catch up with bursts after timeout |
| PiCommunicationTask (Ethernet) | 0 / 2 | Single TCP client, bounded reads/writes, telemetry every 20 ms |
| SafetyTask | 0 / 5 | 10 ms operational interlock and control freshness check |

The ISR only calls vTaskNotifyGiveFromISR; returning its wake flag asks the IDF GPTimer driver to perform the ISR yield. ControlTask waits on ulTaskNotifyTake. No external timer pin, I2C or network operations occur there. Queues copy complete sensor/motor snapshots. SensorTask exclusively owns I2C, ActuatorTask owns RS485, EthernetTask owns its TCP socket; filter settings use a short critical section.

Diagnostics in STATUS include measured task frequency, execution time, maximum task wake-interval jitter (not oscilloscope-measured ISR latency), missed notifications, overruns, sensor/Modbus error and timeout counts, drive status/fault, actual and desired Hz, connection status, and control/network stack high-water marks. Hardware period is nominal: actual jitter, stack margin and stability need bench measurement.

## Wiring and address plan

| Connection | Board connection / internal mapping | Destination / constraints |
|---|---|---|
| Pi 3B | RJ45 W5500 Ethernet | Pi RJ45 via local switch/DHCP network; TCP port 5000. No UART connection needed |
| V20 | Isolated RS485 A+ / B- screw terminals | V20 RS485 P+ / N- per drive terminal diagram; verify polarity, reference, shield and end termination |
| Onboard RS485 | GPIO17 TX, GPIO18 RX, GPIO21 RTS | Internal isolated transceiver; no external transceiver |
| Pitot ASPD-4525 | Expansion SDA GPIO42, SCL GPIO41, GND | I2C address 0x28. Module supply 4–5.5 V, nominal 5 V; check actual module pull-ups/level shifting so ESP32 bus never rises to 5 V |
| Sensor PCB | Same SDA/SCL and common logic ground | Two TCA9548A, planned 0x70 and 0x71; eight ABP2 channels each; 100 kHz configurable |
| DO1–DO8 | TCA9554 address 0x20, bits 0–7 / EXIO1–8 | Isolated sinking outputs, not GPIO pins or voltage sources. Initialized inactive, not assigned to the VFD |
| DI1–DI8 | GPIO4,5,6,7,8,9,10,11 | Industrial terminals. Optional `INTERLOCK_DI_INDEX` 0–7 and verified OK polarity; -1 leaves unassigned |
| Emergency stop | Independent safety circuit | Must stop the machine independently of MCU, Ethernet and Modbus; operational DI/STOP is not a safety function |

Reserved: W5500 MOSI13/MISO14/SCLK15/CS16/INT12/RESET39; RTC 0x51 with INT40; RGB38; buzzer46; CAN2/3; TF45/47/48; BOOT0; USB19/20; UART debug43/44. Do not reuse these or flash/PSRAM pins. Verify expansion header orientation and voltage on the actual revision; a numbered connector pin layout is intentionally not guessed.

The official pinout and vendor demo verify mappings and onboard addresses. The current official Resources page/demo archive did not include a board schematic. Therefore schematic-level verification of expansion connector numbering, output polarity circuitry and the purchased revision remains a commissioning item.

**Address conflict:** default ABP2 0x28 conflicts with the upstream ASPD-4525 even when one mux channel is selected. Order/configure a genuinely different ABP2 address (confirmed by its exact part number), or revise PCB isolation. Simply selecting one of the two muxes does not isolate an upstream pitot. The enabled-tap build rejects equal addresses. Mux addresses must also be distinct and avoid onboard addresses.

## Sensors and future PCB

The pitot conversion uses the existing Uno contract: 0x28, 14-bit type-A counts 10–90%, -1 to +1 psi, 6894.757 Pa/psi; temperature 11-bit, -50..150 C. Non-normal status, range saturation, I2C failure or incomplete ten-reading batches are invalid. The ten-reading average, 5 Pa deadband, absolute differential pressure speed calculation and density 1.225 kg/m3 are retained. Startup averages 200 valid readings while motor requests remain stopped; keep both pitot ports in still air. There is no automatic re-zero after a runtime disconnect. The preserved absolute-pressure convention does not detect reversed hoses.

The first-order pressure filter uses actual elapsed time and starts at 1 Hz. The retained HMI filter setting sends `FILTER hz` (0.1–10) and confirms the reported value. The acquisition target is 25 Hz rather than 50 because the existing ten-reading average alone uses about 20 ms. Future full tap scans will add bus time; measure achieved scan rate before choosing settings.

`PRESSURE_TAPS_ENABLED=0` until the PCB exists. No simulated tap pressures are reported as valid. To enable, supply the actual ABP2 address, ABP2_MIN_PA, ABP2_MAX_PA, ABP2_OUTPUT_MIN and ABP2_OUTPUT_MAX from the ordered part, then set PRESSURE_TAPS_ENABLED=1. No ABP2 range/calibration was present in the repository and none is invented. The driver triggers all channels, waits at least 5 ms, checks busy/power/memory/math status, and decodes 24-bit readings. Both muxes are disabled before each selection and after scanning. A 16-bit validity mask identifies missing taps; missing taps do not invalidate the independent pitot. Stalled I2C can age the pitot and stop control. Cp remains deferred until pressure-reference plumbing and calibration are commissioned; the original Cp helper is unchanged.

## V20 configuration and fault response

See V20_MODBUS.md for register mapping and parameter contract. UART is now 9600 8E1/slave 1, configurable in AppConfig; set V20 P2010[0]=6, P2034=2, P2035=1, P2021=1 to match. Keep P0700/P1000=5, P2023=2, P2000=50 Hz and the verified 500 ms drive telegram watchdog. Motor limits, ramps and independent emergency-stop wiring must be commissioned separately. Firmware never acknowledges drive faults or starts automatically.

No automatic RUN retries: a timeout might mean RUN was executed. V20 driver attempts STOP, reports failure and requires a successful stopped cycle plus a new operator START. On initialization failure correct the drive and restart the controller. The 80 ms receive deadline includes response/frame time at 9600 baud; late frames are quarantined. If the link cannot deliver STOP, the commissioned drive watchdog must produce the documented F72 response. Test it physically.

Sensor failure/staleness, motor fault/staleness, TCP disconnect and 2 s command silence stop control. Timing faults latch until explicit STOP followed by a new START. Pending commands are cleared when TCP disconnects/reconnects. A stale control request independently makes ActuatorTask send OFF1. Control and Safety tasks register with the 3 s panic/reset task watchdog; IDF idle/interrupt watchdogs remain enabled. A whole-controller lockup relies on the VFD's much shorter watchdog before MCU reset. Never configure automatic VFD fault restart.

## Protocol and Pi

Existing newline commands remain: START, STOP, SETPOINT m/s, PID Kp Ki Kd, PING. FILTER hz is additive. Responses remain OK/ERR and STATUS key=value lines. Existing STATUS fields remain, with diagnostics, physical pressure, temperature, drive feedback, sample sequence and p0..p15 plus taps_valid appended. out is percent; desired_hz is out*0.5 (P2000=50 Hz). A queued reply is not proof that the motor started; inspect running/reason and drive feedback. Telemetry repeats latest snapshots; Pi logs each sensor sequence only once.

Use the `RaspberryPi` branch for the paired HMI. See HMI/deploy/ESP32-ETHERNET.md there. TCP accepts one controller client and is intended for a trusted local machine network; do not expose port 5000 to the internet. DHCP address is logged to serial; configure a DHCP reservation and set the same host in the HMI. Direct Pi-to-ESP wiring requires DHCP service on the Pi or another configured network.

## Ordered commissioning procedure

1. Keep the motor power/enable independently inhibited. Inspect board revision, wiring, bus voltage, insulation, polarity and emergency-stop circuit.
2. Flash simulation build first. Check boot is stopped; STATUS sim=1, control_hz near 1000, execution below 1000 us, zero growing missed/overrun counters. Stress TCP with split frames and disconnects; inspect stack margins. No GPIO4 clock input is used.
3. Flash real build with motor still inhibited. Keep pitot in still air through calibration; verify sim=0, pressure near zero, correct pressure/temperature response, sensor_ok after calibration. Unplug sensor: invalid/stale measurements must inhibit START and STOP running control.
4. Verify Ethernet DHCP, one Pi client, STOP on connection, bounded command parsing, PING heartbeat and STATUS fields. Restart Pi/unplug Ethernet: controller stops; reconnect must never resume RUN. Record/export a sample run and compare raw sample sequence/actual recording rate.
5. With motor inhibited, configure V20 parameters and confirm initial OFF1, P2000 check, actual frequency/status and no fault resets. Pull RS485: check error counters and real watchdog F72 response. Restore: no automatic start.
6. Assign/test any operational DI interlock and polarity. Verify DO outputs remain inactive. Test independent emergency stop with the MCU disconnected.
7. Only after these pass enable HMI fan_control_enabled and release the independently controlled motor inhibit. Start at a low setpoint; confirm direction, frequency scaling, acceleration and STOP ramp. Measure control stability; the inherited gains and slower acquisition must be validated on this plant.
8. Force timing overload in a bench/test build: outputs must stop, counters increment, STOP/new START required. Verify watchdog reset returns to stopped state.
9. When the PCB arrives, verify distinct addresses, exact ABP2 ranges and pressure reference, compile the enabled driver and check all 16 channels separately, including missing/stuck sensors and mux isolation. Calibrate and validate Cp before enabling Cp workflows.

## Primary references

- [Waveshare pinout](https://docs.waveshare.com/ESP32-S3-POE-ETH-8DI-8DO)
- [Waveshare resources](https://docs.waveshare.com/ESP32-S3-POE-ETH-8DI-8DO/Resources-And-Documents) and official Demo.zip: WS_GPIO, WS_TCA9554PWR, WS_PCF85063.
- [Espressif GPTimer](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/gptimer.html)
- [Matek ASPD-4525 specification](https://www.mateksys.com/?portfolio=aspd-4525)
- [Honeywell ABP2 datasheet](https://prod-edam.honeywell.com/content/dam/honeywell-edam/sps/siot/en-us/products/sensors/pressure-sensors/board-mount-pressure-sensors/basic-abp2-series/documents/sps-siot-abp2-series-datasheet-32350268-en.pdf)
- [Siemens V20 03/2021](https://support.industry.siemens.com/cs/attachments/109799545/V20_op_instr_0321_en-US.pdf), section 7.2; retained implementation was originally verified against 12/2024 (that PDF returned HTTP 403 during this migration).

## Development validation (2026-10-27)

- ESP-IDF 5.5.0 hardware and simulation builds passed. The enabled ABP2 path also compiled using a temporary, synthetic calibration fixture; that fixture is not a production configuration and must not be flashed.
- Existing MSVC controller/protocol, V20 and quintic-ramp host suites passed; FILTER parsing is covered.
- Paired HMI: 16 unit/workflow tests passed, including fragmented TCP, malformed frames, missing telemetry, no START replay, duplicate/invalid sample suppression and commissioning gates.
- Python compile checks and JavaScript module syntax check passed. The native Qt interface was rendered with Ethernet controls and checked with the commissioning gate both enabled and disabled.
- No board was flashed, no physical timing or I2C/Modbus measurements were performed, and no motor was operated. Software tests cannot certify hardware timing, drive settings or plant stability.
