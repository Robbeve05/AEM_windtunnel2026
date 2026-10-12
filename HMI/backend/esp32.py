"""Future newline-delimited JSON transport. Hardware integration is deliberately gated.

Firmware must define acknowledgements, watchdog, units and safety semantics before enabling it.
The HMI simulator uses absolute tap pressures; convert gauge pressures in this adapter.
"""
import json


class ESP32Serial:
    def __init__(self, port: str, baud_rate: int):
        import serial
        self.connection = serial.Serial(port, baud_rate, timeout=0.25, write_timeout=0.25)

    def send(self, command: str, **values) -> None:
        self.connection.write((json.dumps({'command': command, **values}) + '\n').encode())

    def read(self) -> dict | None:
        line = self.connection.readline()
        return json.loads(line) if line else None

    def close(self) -> None:
        self.connection.close()
