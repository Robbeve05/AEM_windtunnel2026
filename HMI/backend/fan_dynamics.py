"""Fan dynamics fitting and offline calibration-record analysis.

The fitting API estimates a simple static frequency-to-airspeed relationship.
The CSV analysis API estimates advisory first-order dynamics from existing
calibration records; neither API actuates the V20 or enables controller gains.
"""

from __future__ import annotations

import csv
import json
import math
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from statistics import fmean
from typing import Sequence


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
        if not math.isfinite(self.gain_ms_per_hz) or self.gain_ms_per_hz <= 0:
            raise ValueError('gain_ms_per_hz must be finite and positive')
        if not math.isfinite(self.time_constant_s) or self.time_constant_s <= 0:
            raise ValueError('time_constant_s must be finite and positive')
        if not math.isfinite(self.dead_time_s) or self.dead_time_s < 0:
            raise ValueError('dead_time_s must be finite and non-negative')
        if not math.isfinite(self.offset_ms):
            raise ValueError('offset_ms must be finite')
        if not math.isfinite(self.max_airspeed_ms) or self.max_airspeed_ms <= 0:
            raise ValueError('max_airspeed_ms must be finite and positive')
        if not math.isfinite(self.max_frequency_hz) or self.max_frequency_hz <= 0:
            raise ValueError('max_frequency_hz must be finite and positive')
        self.valid = bool(self.valid)

    def airspeed_for(self, frequency_hz: float, *, dt_s: float = 0.0) -> float:
        if not math.isfinite(frequency_hz):
            raise ValueError('frequency_hz must be finite')
        if frequency_hz < 0:
            raise ValueError('frequency_hz must be non-negative')
        if dt_s < 0 or not math.isfinite(dt_s):
            raise ValueError('dt_s must be finite and non-negative')

        clamped = min(float(frequency_hz), self.max_frequency_hz)
        response = self.gain_ms_per_hz * clamped + self.offset_ms
        if dt_s > 0:
            response *= 1.0 / (1.0 + dt_s / max(self.time_constant_s, 1e-9))
        return max(0.0, min(response, self.max_airspeed_ms))


@dataclass
class FanDynamicIdentifier:
    """Estimate a first-order fan model from frequency/airspeed samples."""

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
            if not math.isfinite(frequency_hz) or not math.isfinite(airspeed_ms):
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
            max_airspeed_ms=min(
                self.max_airspeed_ms,
                max(v for _, v in points) * 1.25 or self.max_airspeed_ms,
            ),
            max_frequency_hz=min(
                self.max_frequency_hz,
                max(f for f, _ in points) or self.max_frequency_hz,
            ),
        )

    def estimate(self, samples: Sequence[tuple[float, float]]) -> FanDynamicsModel:
        return self.fit(samples)


def estimate_frequency_response(
    samples: Sequence[tuple[float, float]],
    *,
    max_frequency_hz: float = 50.0,
    max_airspeed_ms: float = 40.0,
) -> FanDynamicsModel:
    return FanDynamicIdentifier(
        max_frequency_hz=max_frequency_hz,
        max_airspeed_ms=max_airspeed_ms,
    ).fit(samples)


def identify_fan_dynamics(
    samples: Sequence[tuple[float, float]],
    *,
    max_frequency_hz: float = 50.0,
    max_airspeed_ms: float = 40.0,
) -> FanDynamicsModel:
    return estimate_frequency_response(
        samples,
        max_frequency_hz=max_frequency_hz,
        max_airspeed_ms=max_airspeed_ms,
    )


FanDynamics = FanDynamicsModel
FanDynamicEstimator = FanDynamicIdentifier


def _float(row, key):
    try:
        value = float(row[key])
        return value if math.isfinite(value) else None
    except (KeyError, ValueError, TypeError):
        return None


def identify(rows, points):
    """Return advisory first-order estimates for CSV calibration transitions."""
    data = []
    for row in rows:
        if str(row.get("valid", "")).lower() not in ("true", "1"):
            continue
        try:
            time_s = datetime.fromisoformat(
                str(row["timestamp"]).replace("Z", "+00:00")
            ).timestamp()
        except (ValueError, KeyError, TypeError):
            continue
        frequency_hz, airspeed_ms, target_hz = (
            _float(row, key) for key in ("actual_hz", "pv", "target_hz")
        )
        if None in (frequency_hz, airspeed_ms, target_hz):
            continue
        data.append((time_s, frequency_hz, airspeed_ms, target_hz, row.get("phase")))

    transitions = []
    for index, point in enumerate(points):
        target_hz = float(point["target_hz"])
        item = dict(
            target_hz=target_hz,
            status="BASELINE_ONLY" if not index else "INSUFFICIENT_DATA",
            K_ms_per_hz=None,
            tau_s=None,
            rmse_ms=None,
            r2=None,
        )
        transitions.append(item)
        if not index:
            continue

        previous = points[index - 1]
        frequency_start = float(previous.get("mean_actual_hz", previous["target_hz"]))
        airspeed_start = float(previous["mean_pv"])
        frequency_end = float(point.get("mean_actual_hz", point["target_hz"]))
        airspeed_end = float(point["mean_pv"])
        if abs(frequency_end - frequency_start) < 0.25:
            item["status"] = "INSUFFICIENT_EXCITATION"
            continue

        gain = (airspeed_end - airspeed_start) / (frequency_end - frequency_start)
        item["K_ms_per_hz"] = gain
        segment = [
            (time_s, frequency_hz, airspeed_ms)
            for time_s, frequency_hz, airspeed_ms, actual_target, phase in data
            if abs(actual_target - target_hz) < 0.001
            and phase in ("RAMPING", "STABILIZING", "MEASURING")
        ]
        if gain <= 0 or len(segment) < 12:
            item["status"] = "INSUFFICIENT_DATA"
            continue
        segment.sort()
        if segment[-1][0] - segment[0][0] < 0.5 or any(
            segment[j][0] <= segment[j - 1][0] for j in range(1, len(segment))
        ):
            item["status"] = "INVALID_TIMESTAMPS"
            continue

        # Recorded pv already includes the configured 1 Hz Pitot filtering.
        best = None
        for step in range(180):
            tau_s = 0.05 * 1.05**step
            predicted = airspeed_start
            squared_errors = []
            for (time_prev, frequency_prev, _), (time_now, _, airspeed_now) in zip(
                segment, segment[1:]
            ):
                equilibrium = airspeed_start + gain * (frequency_prev - frequency_start)
                predicted = equilibrium + (predicted - equilibrium) * math.exp(
                    -(time_now - time_prev) / tau_s
                )
                squared_errors.append((predicted - airspeed_now) ** 2)
            mse = fmean(squared_errors)
            if best is None or mse < best[0]:
                best = mse, tau_s

        mse, tau_s = best
        observed = [airspeed for _, _, airspeed in segment[1:]]
        variance = fmean((value - fmean(observed)) ** 2 for value in observed)
        r_squared = 1 - mse / variance if variance > 1e-8 else None
        item.update(
            tau_s=round(tau_s, 4),
            rmse_ms=round(math.sqrt(mse), 4),
            r2=round(r_squared, 4) if r_squared is not None else None,
            status="CANDIDATE" if r_squared is not None and r_squared >= 0.8
            else "REVIEW_REQUIRED",
        )

    return dict(
        schema_version=1,
        measurement="V20 actual Hz to filtered Pitot airspeed",
        advisory_only=True,
        message="Model is an uncommissioned candidate; no PI gains are activated.",
        transitions=transitions,
    )


def analyze_calibration_folder(folder):
    """Read calibration CSV exports and write advisory dynamics.json output."""
    folder = Path(folder)
    with (folder / "raw_samples.csv").open(newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))
    with (folder / "characteristic.csv").open(newline="", encoding="utf-8") as file:
        points = list(csv.DictReader(file))
    result = identify(rows, points)
    (folder / "dynamics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result
