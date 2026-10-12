import math
import random
import time


class Simulator:
    def __init__(self, config):
        self.config = config
        self.state = 'STOPPED'
        self.setpoint = 15.0
        self.speed = 0.0
        self.started = time.monotonic()

    def start(self):
        if self.state == 'FAULT':
            raise ValueError('Resolve the fault before starting')
        if self.state in ('STOPPED', 'STOPPING'):
            self.state = 'STARTING'

    def stop(self):
        self.state = 'STOPPING' if self.speed > 0.1 else 'STOPPED'

    def read(self, dt: float) -> dict:
        active = self.state in ('STARTING', 'RUNNING')
        target = self.setpoint if active else 0
        self.speed += (target - self.speed) * (1 - math.exp(-dt / 1.5))
        if self.state == 'STARTING' and abs(self.speed - target) < 0.3:
            self.state = 'RUNNING'
        if self.state == 'STOPPING' and self.speed < 0.08:
            self.speed = 0
            self.state = 'STOPPED'
        speed = max(0, self.speed + (random.gauss(0, 0.035) if self.speed > 0.1 else 0))
        q = 0.5 * self.config.air_density * speed ** 2
        static = 101325 + random.gauss(0, 1.5)
        pressures = []
        for tap in self.config.pressure_taps:
            coefficient = (-1.35 * math.exp(-tap.x_c * 2.7) if tap.surface == 'upper'
                           else 0.42 * (1 - tap.x_c))
            pressures.append(static + coefficient * q + random.gauss(0, 0.15))
        return dict(timestamp_ms=int((time.monotonic() - self.started) * 1000), state=self.state,
                    airspeed_ms=speed, dynamic_pressure_pa=q, static_pressure_pa=static,
                    temperature_c=20.4 + random.gauss(0, 0.025), fan_output_pct=min(100, self.speed / self.config.max_airspeed_ms * 100),
                    angle_of_attack_deg=5.0, pressure_pa=pressures, errors=[])
