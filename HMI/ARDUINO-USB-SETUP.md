# Arduino Uno pitot -> Raspberry Pi HMI

## Current hardware mode

The HMI now uses one ASPD-4525 differential pressure sensor connected to an Arduino Uno. The Uno sends one machine-readable serial line over its normal USB cable. The Raspberry Pi auto-detects `/dev/ttyACM*` or `/dev/ttyUSB*`.

Available live values:
- differential / dynamic pressure [Pa]
- airspeed [m/s]
- airspeed [km/h]
- sensor temperature [degC]
- 5 s mean, standard deviation, min/max and Flow Stability indicator

Not yet available and explicitly marked **IN DEVELOPMENT**:
- static pressure as a separate absolute value
- Cp pressure-tap distribution
- angle of attack
- fan / VFD command and setpoint
- Reynolds number (can be enabled once chord and air-property assumptions are defined)

## Flow Stability

The indicator uses the last 5 seconds of pitot airspeed:

`CV = sigma_v / mean_v * 100%`

`Flow Stability = clamp(100% - CV, 0%, 100%)`

It is only shown above 1 m/s. This is a practical temporal stability indicator at one pitot location; it is **not** a turbulence-intensity or flow-uniformity certification.

## Raspberry Pi permissions

Run once:

```bash
sudo usermod -aG dialout windtunnel_pi
sudo reboot
```

After reboot, check the Uno:

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

The HMI is configured with `serial_port: "auto"`, so no fixed device name is needed.

## Important zero calibration

Opening the Arduino Uno USB serial port normally resets the Uno. The firmware performs zero calibration on startup. Keep the pitot tube still and out of airflow for roughly the first 3 seconds after connecting the USB cable or restarting the HMI service.

## HMI service checks

```bash
systemctl status windtunnel-hmi --no-pager
journalctl -u windtunnel-hmi -n 60 --no-pager
```
