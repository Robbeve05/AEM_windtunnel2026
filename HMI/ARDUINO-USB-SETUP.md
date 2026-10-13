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

## Arduino pressure low-pass filter

The HMI Settings page provides an Arduino pressure-filter cutoff from **0.1 to 10 Hz**
(default 5 Hz). The Pi sends the selected value over the same USB serial connection as
the live telemetry; the Uno applies a first-order low-pass filter to differential
pressure before calculating airspeed and reports its active cutoff in each data line.
Lower cutoffs smooth more noise but respond more slowly to real changes. This is
separate from the existing 10-reading sensor average and the Pi's 5-second statistics.

The updated firmware is `Arduino/Arduino_Uno_Pitot_HMI.ino`. Flash it once to enable
remote filter changes. To flash without opening the enclosure, the Pi must already be
connected to the Uno over USB and you must have shell access to the Pi. Install
Arduino CLI and the Uno board core once:

```bash
sudo apt update
sudo apt install arduino-cli
arduino-cli core update-index
arduino-cli core install arduino:avr
```

Then, from the repository checkout, run:

```bash
cd ~/AEM_windtunnel2026
bash HMI/deploy/flash-arduino.sh
```

The helper stops the HMI service to release the serial port, compiles and uploads the
repository firmware, then restarts the service. It refuses to guess if more than one
Arduino-compatible serial port is attached. Ensure airflow is off and leave the pitot
out of the airflow during upload and the Uno's zero calibration. If the Arduino USB
cable is not connected to the Pi, remote flashing is not possible without connecting
it first. The currently installed firmware continues streaming, but will not apply
the new setting until it has been flashed.

## Important zero calibration

Opening the Arduino Uno USB serial port normally resets the Uno. The firmware performs zero calibration on startup. Keep the pitot tube still and out of airflow for roughly the first 3 seconds after connecting the USB cable or restarting the HMI service.

## HMI service checks

```bash
systemctl status windtunnel-hmi --no-pager
journalctl -u windtunnel-hmi -n 60 --no-pager
```
