# Siemens SINAMICS V20 — Modbus RTU

Technical integration reference for Siemens V20 `6SL3210-5BE25-5UV1` and the industrial ESP32-S3 controller. **This describes the software interface, not a commissioned installation.** Verify all drive settings against the physical V20, its current manual and the actual motor nameplate.

## Transport and wiring

- Built-in isolated RS-485 interface; ESP32 internal UART connections are TX GPIO17, RX GPIO18 and RTS GPIO21. Use the board's isolated A+/B- terminals, not raw ESP32 pins.
- Modbus **RTU** (not Modbus TCP), configured in the application for **115200 baud, 8E1, slave 1**. Verify the drive's actual P2010 encoding before using this baud rate.
- Check A/B polarity, reference connection, termination, shielding and the drive's documented terminals.
- The electrical emergency stop must remain independent of Modbus, the ESP32 and its software. OFF1 is an operational stop, **not** a safety-rated stop.

## Drive parameter checklist

| Parameter | Intended setup / verification |
| --- | --- |
| P0700 | 5 — command source USS/Modbus |
| P1000 | 5 — frequency setpoint USS/Modbus |
| P2023 | 2 — Modbus; perform required power cycle |
| P2021 | 1 — slave address |
| P2022 | 20 ms drive response processing; firmware receive timeout is configured separately |
| P2010[0] | Set per Siemens coding for **115200 baud**; verify on the real V20 |
| P2034 / P2035 | 2 / 1 — even parity and one stop bit (8E1) |
| P2000 | 50 Hz reference frequency |
| P2014[0] | 500 ms telegram watchdog; verify on drive and test physical communication loss |

Motor data, allowable frequency range, direction and drive acceleration/deceleration parameters P1120/P1121 need independent verification. Do not infer motor rated shaft power from the V20's 5.5 kW system rating.

## Modbus register contract

| Function | Holding register | PDU address | Meaning |
| --- | --- | --- | --- |
| STW | 40100 | 99 | Command word: 0x047E OFF1; 0x047F forward/run |
| HSW | 40101 | 100 | Setpoint 0–16384 for 0–100% of P2000 |
| ZSW / HIW | 40110 / 40111 | 109 / 110 | Status word and actual-frequency indication |
| Watchdog | 40001 | 0 | 500 ms |
| Reference | 40016 | 15 | 5000 = 50.00 Hz |

The driver uses FC16 for combined STW/HSW writes, FC03 for reads and FC06 for watchdog configuration. Modbus registers are big-endian with low-byte-first CRC transmission. Driver initialization requests OFF1 and checks the reference and status before permitting operation. A 0 Hz setpoint does not replace an explicit OFF1 stop.

Requests have bounded response handling, including frame timing and late-response quarantine. A write timeout does **not** prove that the V20 ignored a RUN command. The controller therefore avoids blind automatic RUN retries and attempts a stop following communication errors. The drive's independent telegram-watchdog response must be tested in hardware.

## Current controller architecture

The ESP32 uses an internal GPTimer and a **200 Hz / 5 ms** control cycle. V20 service is asynchronous; responses are not expected to complete inside each 5 ms control period.

- **MANUAL:** frequency setpoint through a separate fifth-order ramp.
- **AUTO:** Pitot airspeed feedback, a fifth-order reference, one-mass torque PID and an approximate inverse motor model that generates V20 frequency.
- There is **no separate legacy frequency-domain PI controller** in the current firmware.
- Real AUTO remains inhibited until motor and fan model parameters are independently validated.

See [ESP32 README](README.md) and [Torque control](TORQUE-CONTROL.md). The current software state and commissioning flags, not historical tuning figures, determine whether the machine can be operated.

## Build and validation

From `ESP32/`:

```bash
pio run -e waveshare_s3
pio run -e waveshare_s3_sim
```

Windows host regression tests: `./tests/run_host_tests.ps1`.

The host tests use hardware mocks. Physical commissioning must additionally verify the V20 parameter values, feedback scaling, initial OFF1 behavior, frequency tracking, fault handling, connection-loss watchdog, K2 operational enable, independent emergency stop and fan coast-down.

## Sources

- [Siemens V20 Operating Instructions (12/2024)](https://support.industry.siemens.com/cs/attachments/109977897/V20_op_instr_1224_en-US.pdf)
- [Siemens V20 Operating Instructions (03/2021)](https://support.industry.siemens.com/cs/attachments/109799545/V20_op_instr_0321_en-US.pdf)

Parameter/register interpretations must be confirmed for the firmware revision and actual installed drive before commissioning.
