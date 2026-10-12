"""USB serial reader for the Arduino Uno pitot-tube node.

Expected firmware output (one line per sample):
pressure_pa=123.45,temperature_c=22.70,airspeed_ms=14.19,airspeed_kmh=51.08

The reader auto-detects /dev/ttyACM* and /dev/ttyUSB* when serial_port is "auto".
"""
from __future__ import annotations

import glob
import time
from pathlib import Path


class ArduinoPitotSerial:
    def __init__(self, port: str = "auto", baud_rate: int = 115200):
        try:
            import serial
        except ImportError:
            serial = None

        self.serial = serial
        self.requested_port = port
        self.baud_rate = baud_rate
        self.connection = None
        self.port = None
        self.last_error = "Not connected"
        self.connected_since = None

    def _candidates(self) -> list[str]:
        if self.requested_port and self.requested_port.lower() != "auto":
            return [self.requested_port]
        by_id = sorted(glob.glob('/dev/serial/by-id/*'))
        normal = sorted(glob.glob('/dev/ttyACM*')) + sorted(glob.glob('/dev/ttyUSB*'))
        # Preserve order while removing duplicates/resolved aliases.
        seen, result = set(), []
        for item in by_id + normal:
            try:
                key = str(Path(item).resolve())
            except OSError:
                key = item
            if key not in seen:
                seen.add(key)
                result.append(item)
        return result

    def connect(self) -> bool:
        self.close()
        if self.serial is None:
            self.last_error = 'pyserial is not installed; run pip install -r requirements.txt'
            return False
        candidates = self._candidates()
        if not candidates:
            self.last_error = "Arduino not found on USB"
            return False
        for port in candidates:
            try:
                self.connection = self.serial.Serial(port, self.baud_rate, timeout=0.6)
                self.port = port
                self.connected_since = time.monotonic()
                self.last_error = ""
                # Opening an Uno normally resets it. Give its zero calibration time to finish.
                time.sleep(0.25)
                self.connection.reset_input_buffer()
                return True
            except (OSError, self.serial.SerialException) as exc:
                self.last_error = f"{port}: {exc}"
                self.connection = None
                self.port = None
        return False

    def _parse(self, text: str) -> dict | None:
        values: dict[str, float] = {}
        for field in text.strip().split(','):
            if '=' not in field:
                continue
            key, raw = field.split('=', 1)
            key = key.strip()
            try:
                values[key] = float(raw.strip())
            except ValueError:
                return None
        required = {'pressure_pa', 'temperature_c', 'airspeed_ms', 'airspeed_kmh'}
        return values if required.issubset(values) else None

    def read(self) -> dict | None:
        if self.connection is None and not self.connect():
            time.sleep(0.5)
            return None
        try:
            line = self.connection.readline().decode('utf-8', errors='replace').strip()
            if not line:
                return None
            sample = self._parse(line)
            if sample is None:
                self.last_error = "Unexpected Arduino serial format"
            else:
                self.last_error = ""
            return sample
        except (OSError, self.serial.SerialException) as exc:
            self.last_error = str(exc)
            self.close()
            return None

    @property
    def connected(self) -> bool:
        return self.connection is not None and bool(getattr(self.connection, 'is_open', False))

    def close(self) -> None:
        if self.connection is not None:
            try:
                self.connection.close()
            except OSError:
                pass
        self.connection = None
        self.port = None
