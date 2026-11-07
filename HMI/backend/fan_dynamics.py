"""Fan dynamic identification utilities for the wind tunnel.

This module models the measured fan frequency-to-airspeed relationship and can be
used to synthesize a frequency PI feedforward model. The implementation keeps the
logic intentionally lightweight and validation-oriented so it can be used both in
Python tests and in the backend controller logic.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from math import isfinite, sqrt
from typing import Iterable, Sequence


@dataclass
class FanDynamicsModel:
    """Simple low-order frequency-to-airspeed model."""

    gain_ms_per_hz: float = 0.8
    time_constant_s: float = 1.0
    dead_time_s: float = 0.0
    offset_ms: float = 0.0
    max_airspeed_ms: float = 40.0
    max_frequency_hz: float = 50.0
    valid: bool = True

    def __post_init__(self):
        if not isfinite(self.gain_ms_per_hz) or self.gain_ms_per_hz <= 0:
            raise ValueError('gain_ms_per_hz must be finite and positive')
        if not isfinite(self.time_constant_s) or self.time_constant_s <= 0:
            raise ValueError('time_constant_s must be finite and positive')
        if not isfinite(self.dead_time_s) or self.dead_time_s < 0:
            raise ValueError('dead_time_s must be finite and non-negative')
        if not isfinite(self.offset_ms):
            raise ValueError('offset_ms must be finite')
        if not isfinite(self.max_airspeed_ms) or self.max_airspeed_ms <= 0:
            raise ValueError('max_airspeed_ms must be finite and positive')
        if not isfinite(self.max_frequency_hz) or self.max_frequency_hz <= 0:
            raise ValueError('max_frequency_hz must be finite and positive')
        self.valid = bool(self.valid)

    def airspeed_for(self, frequency_hz: float, *, dt_s: float = 0.0) -> float:
        if not isfinite(frequency_hz):
            raise ValueError('frequency_hz must be finite')
        if frequency_hz < 0:
            raise ValueError('frequency_hz must be non-negative')
        clamped = min(float(frequency_hz), self.max_frequency_hz)
        response = self.gain_ms_per_hz * clamped + self.offset_ms
        if dt_s < 0 or not isfinite(dt_s):
            raise ValueError('dt_s must be finite and non-negative')
        if dt_s > 0:
            response *= 1.0 / (1.0 + dt_s / max(self.time_constant_s, 1e-9))
        return max(0.0, min(response, self.max_airspeed_ms))


@dataclass
class FanDynamicIdentifier:
    """Estimate a first-order fan dynamic model from step/scan data."""

    max_frequency_hz: float = 50.0
    max_airspeed_ms: float = 40.0
    min_samples: int = 2

    def _points(self, samples: Sequence[tuple[float, float]]) -> list[tuple[float, float]]:
        if not samples:
            raise ValueError('No dynamic samples provided')
        cleaned: list[tuple[float, float]] = []
        for sample in samples:
            if len(sample) != 2:
                raise ValueError('Each sample must contain (frequency_hz, airspeed_ms)')
            frequency_hz, airspeed_ms = sample
            if not isfinite(frequency_hz) or not isfinite(airspeed_ms):
                raise ValueError('Sample values must be finite')
            if frequency_hz < 0 or airspeed_ms < 0:
                raise ValueError('Sample values must be non-negative')
            cleaned.append((float(frequency_hz), float(airspeed_ms)))
        if len(cleaned) < self.min_samples:
            raise ValueError('Not enough samples to identify the fan dynamics')
        cleaned.sort(key=lambda item: item[0])
        return cleaned

    def fit(self, samples: Sequence[tuple[float, float]]) -> FanDynamicsModel:
        points = self._points(samples)
        f_start, v_start = points[0]
        f_end, v_end = points[-1]
        if f_end <= f_start:
            raise ValueError('Frequency samples must be strictly increasing for identification')
        gain = (v_end - v_start) / (f_end - f_start)
        if gain <= 0:
            raise ValueError('Estimated fan gain must be positive')
        time_constant = max((f_end - f_start) / max(self.max_frequency_hz, 1e-6), 0.2)
        return FanDynamicsModel(
            gain_ms_per_hz=gain,
            time_constant_s=time_constant,
            dead_time_s=0.0,
            offset_ms=max(0.0, v_start - gain * f_start),
            max_airspeed_ms=min(self.max_airspeed_ms, max(v for _, v in points) * 1.25 or self.max_airspeed_ms),
            max_frequency_hz=min(self.max_frequency_hz, max(f for f, _ in points) or self.max_frequency_hz),
        )

    def estimate(self, samples: Sequence[tuple[float, float]]) -> FanDynamicsModel:
        return self.fit(samples)


def estimate_frequency_response(samples: Sequence[tuple[float, float]], *, max_frequency_hz: float = 50.0,
                                max_airspeed_ms: float = 40.0) -> FanDynamicsModel:
    return FanDynamicIdentifier(max_frequency_hz=max_frequency_hz, max_airspeed_ms=max_airspeed_ms).fit(samples)


def identify_fan_dynamics(samples: Sequence[tuple[float, float]], *, max_frequency_hz: float = 50.0,
                          max_airspeed_ms: float = 40.0) -> FanDynamicsModel:
    return estimate_frequency_response(samples, max_frequency_hz=max_frequency_hz, max_airspeed_ms=max_airspeed_ms)


FanDynamics = FanDynamicsModel
FanDynamicEstimator = FanDynamicIdentifier
