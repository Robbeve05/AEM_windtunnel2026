import math
import random
import time


class Simulator:
    """Software-only input/state model. Does not validate physical safety."""
    def __init__(self, config):
        self.config = config
        self.state = 'INIT'
        self.fault = 'NONE'
        self.setpoint = 15.0
        self.speed = 0.0
        self.started = time.monotonic()
        self.inputs = {1: True, 2: False, 3: False, 4: True, 5: True}
        self.elapsed = 0.0
        self._buttons = {di: dict(raw=False, stable=False, armed=False, changed=0.0)
                         for di in (2, 3)}
        self._reset_pending = False
        self._reset_until = 0.0
        self._reset_accepted = False
        self._stop_elapsed = self._stop_duration = self._stop_start = 0.0

    def _fault(self, reason):
        if self.fault == 'NONE' or reason == 'EMERGENCY_STOP':
            self.fault = reason
        if self.state != 'FAULT':
            self._reset_pending = self._reset_accepted = False
        self.state = 'FAULT'

    def set_input(self, di, high):
        """Inject an electrical level: NC DI1/4/5 healthy HIGH, NO DI2/3 idle LOW."""
        if di not in self.inputs:
            raise ValueError('Expected DI1..DI5')
        previous = self.inputs[di]
        self.inputs[di] = bool(high)
        if di == 1 and not high:
            self._fault('EMERGENCY_STOP')
            self._reset_pending = False
        elif di == 5 and not high:
            self._fault('SAFETY_FEEDBACK')
        elif di == 4 and previous and not high:
            self.stop()

    def _pulse(self, di):
        self.set_input(di, False)
        self.read(.04)  # stable release before a fresh edge
        self.set_input(di, True)
        self.read(.04)
        self.set_input(di, False)
        self.read(.04)

    def start(self):
        if self.state != 'READY':
            raise ValueError('Fresh RESET and READY required before simulated START')
        self._pulse(3)

    def reset(self):
        self._pulse(2)

    def stop(self):
        if self.state == 'RUNNING':
            self.state = 'STOP'
            self._stop_start = self.speed
            self._stop_elapsed = 0.0
            # Same quintic factor and derivative duration bounds as SetpointCurve.
            self._stop_duration = max(.005, 1.875 * self.speed / 2.0,
                                      math.sqrt((10 / math.sqrt(3)) * self.speed / .5),
                                      (60 * self.speed / .5) ** (1 / 3))

    def _step_inputs(self, dt):
        self.elapsed += dt
        edges = {}
        for di, b in self._buttons.items():
            level = self.inputs[di]
            if level != b['raw']:
                b['raw'] = level
                b['changed'] = self.elapsed - dt
            if not self.inputs[1]:
                b['armed'] = False
            edges[di] = False
            if self.elapsed - b['changed'] >= .03 - 1e-9:
                edges[di] = level and not b['stable'] and b['armed'] and self.inputs[1]
                if not level:
                    b['armed'] = self.inputs[1]
                elif edges[di]:
                    b['armed'] = False
                b['stable'] = level
        healthy = self.inputs[1] and self.inputs[5]
        if not self.inputs[1]:
            self._fault('EMERGENCY_STOP')
            self._reset_pending = False
            return
        if not self.inputs[5]:
            self._fault('SAFETY_FEEDBACK')
        if edges[2] and self.state in ('WAIT', 'FAULT') and self.inputs[4]:
            self._reset_pending = True
            self._reset_until = self.elapsed + 5
            return
        if self.elapsed > self._reset_until:
            self._reset_pending = False
        if self.state == 'INIT':
            if not self.inputs[4]:
                self._fault('INTERLOCK')
            elif healthy:
                self.state = 'WAIT'
        elif self.state == 'FAULT':
            if self._reset_pending and healthy and self.inputs[4]:
                self.fault = 'NONE'
                self._reset_pending = False
                self._reset_accepted = True
                self.state = 'WAIT'
        elif self.state == 'WAIT':
            if self._reset_pending and healthy and self.inputs[4]:
                self._reset_pending = False
                self._reset_accepted = True
            if self._reset_accepted and healthy and self.inputs[4]:
                self.state = 'READY'
        elif self.state == 'READY':
            if edges[3] and not edges[2] and not self.inputs[2] and self.inputs[4] and healthy:
                self.state = 'RUNNING'
        elif self.state == 'RUNNING' and not self.inputs[4]:
            self.stop()

    def snapshot(self):
        return dict(state=self.state, fault=self.fault, stale=False,
                    estop_pressed=int(not self.inputs[1]), relay_ok=int(self.inputs[5]),
                    stop_ok=int(self.inputs[4]), reset_required=int(self.state in ('WAIT', 'FAULT')),
                    vfd_enable=self.state in ('RUNNING', 'STOP') and self.fault == 'NONE',
                    raw_levels=sum(int(level) << (di - 1) for di, level in self.inputs.items()))

    def read(self, dt: float) -> dict:
        if not math.isfinite(dt) or dt < 0:
            raise ValueError('Expected finite nonnegative dt')
        self._step_inputs(dt)
        if self.state == 'RUNNING':
            self.speed += (self.setpoint - self.speed) * (1 - math.exp(-dt / 1.5))
        elif self.state == 'STOP':
            self._stop_elapsed = min(self._stop_duration, self._stop_elapsed + dt)
            q = self._stop_elapsed / self._stop_duration
            self.speed = self._stop_start * (1 - q**3 * (10 + q * (-15 + 6*q)))
            if self._stop_elapsed >= self._stop_duration:
                self.speed = 0
                self.state = 'READY'
        else:
            # Illustrative coast only; FAULT removes enable immediately.
            self.speed *= math.exp(-dt / 1.5)
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
                    temperature_c=20.4 + random.gauss(0, 0.025),
                    fan_output_pct=(min(100, self.speed / self.config.max_airspeed_ms * 100)
                                    if self.snapshot()["vfd_enable"] else 0),
                    angle_of_attack_deg=5.0, pressure_pa=pressures,
                    errors=[] if self.fault == 'NONE' else [self.fault])
