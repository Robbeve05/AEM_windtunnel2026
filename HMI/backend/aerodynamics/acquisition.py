"""Adapters supply timestamped pressure/reference pairs; no machine commands."""
from typing import Protocol
import math
import random

from .pressure import dynamic_pressure


class PressureAcquisitionAdapter(Protocol):
    available: bool
    simulated: bool

    def validate(self, config, density: float) -> None: ...

    def sample(self, config, sample_index: int, time_s: float, density: float) -> list[dict]: ...


class SimulationAdapter:
    available = True
    simulated = True

    def validate(self, config, density):
        if dynamic_pressure(density, config.simulation_airspeed_ms) < 1:
            raise ValueError('Reference dynamic pressure must be at least 1 Pa')

    def sample(self, config, sample_index, time_s, density):
        enabled = [tap for tap in config.taps if tap.enabled]
        random_source = random.Random(config.simulation_seed+sample_index*1009)
        rows = []
        for index, tap in enumerate(enabled):
            # Sequential channel timing is explicit, even in the synthetic fixture.
            timestamp = time_s+index/(2*config.sample_rate_hz*max(1, len(enabled)))
            velocity = config.simulation_airspeed_ms*(1+.002*math.sin(timestamp))
            q = dynamic_pressure(density, velocity)
            difference = 2*math.pi*math.radians(config.alpha_deg)*6*tap.x_c*(1-tap.x_c)
            common = -.6*4*tap.x_c*(1-tap.x_c)
            cp = common+(-.5 if tap.surface == 'upper' else .5)*difference
            pressure = 101325+q*cp+random_source.gauss(0, config.simulation_noise_pa)
            rows.append(dict(sample_index=sample_index, time_s=timestamp,
                reference_timestamp_s=timestamp, tap_id=tap.tap_id, channel=tap.channel,
                pressure_pa=pressure, reference_pressure_pa=101325,
                reference_airspeed_ms=velocity, air_density_kg_m3=density,
                temperature_c=20.0, valid=True, sensor_status='SIMULATED', source='simulation'))
        return rows


class UnconfiguredPhysicalAdapter:
    available = False
    simulated = False

    def sample(self, *args, **kwargs):
        raise ValueError('Physical pressure acquisition is unavailable: configure and validate a real adapter first')
