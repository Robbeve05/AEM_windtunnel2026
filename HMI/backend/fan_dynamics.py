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
from bisect import bisect_right
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
            valid=False,  # Static points cannot validate a dynamic time constant.
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


def simc_parameters(gain, tau_s, delay_s, lambda_s):
    """Advisory ISA PI values. Does not send commands or mark a model validated."""
    if not all(math.isfinite(v) for v in (gain, tau_s, delay_s, lambda_s)) or min(gain, tau_s, lambda_s) <= 0 or delay_s < 0:
        raise ValueError("SIMC requires positive K/tau/lambda and nonnegative L")
    return dict(kp=tau_s/(gain*(lambda_s+delay_s)),
                ti_s=min(tau_s,4*(lambda_s+delay_s)), td_s=0.0, tf_s=0.0,
                lambda_s=lambda_s, units="Kp: Hz/(m/s); Ti: s", advisory_only=True)


def simulate_candidate(gain, tau_s, delay_s, parameters):
    """Unit airspeed step on the effective filtered-output model, offline only.

    Unity static feedforward; ISA/Tustin feedback; actuator limits 0..50 Hz,
    a conservative 2 Hz/s V20 ramp, and conditional integration. Actual V20
    ramps must be checked during commissioning; this is not a hardware proof.
    """
    dt = 0.01
    count = min(20000, max(1000, int(10*(tau_s+delay_s)/dt)))
    y = integral = previous_error = actual_hz = 0.0
    history = [0.0]*(int(delay_s/dt)+1)
    trace = []
    for n in range(count):
        error = 1-y
        increment = parameters["kp"]*dt/(2*parameters["ti_s"])*(error+previous_error) if n else 0
        raw = 1/gain + parameters["kp"]*error + integral + increment
        limited = max(0,min(50,raw))
        if ((increment > 0 and (raw > 50 or limited > actual_hz+0.2)) or
                (increment < 0 and (raw < 0 or limited < actual_hz-0.2))):
            increment = 0
            raw = 1/gain + parameters["kp"]*error + integral
            limited = max(0,min(50,raw))
        integral += increment
        previous_error = error
        actual_hz += max(-2*dt,min(2*dt,limited-actual_hz))
        history.append(actual_hz)
        delayed_hz = history[n]
        y = gain*delayed_hz + (y-gain*delayed_hz)*math.exp(-dt/tau_s)
        if n % 10 == 0:
            trace.append(dict(time_s=round(n*dt,2), airspeed_ms=y, actual_hz=actual_hz))
    return dict(reference_ms=1.0, ramp_hz_per_s=2.0, advisory_only=True,
                final_error_ms=1-y, trace=trace)


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
            L_s=None,
            model_valid=False,
            hardware_commissioned=False,
            warnings=[],
            data_completeness=0.0,
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

        # The fitted output is the recorded filtered Pitot signal. Its filter
        # dynamics remain in this effective model; do not filter it a second time.
        times = [sample[0] for sample in segment]
        duration = times[-1] - times[0]
        intervals = [b-a for a, b in zip(times, times[1:])]
        typical_dt = sorted(intervals)[len(intervals)//2]
        completeness = min(1.0, (len(segment)-1)*typical_dt/duration)
        max_delay = min(3.0, duration/4)
        delays = [max_delay*j/20 for j in range(21)]

        def input_intervals(delay_s):
            intervals = []
            for j in range(1, len(segment)):
                left, right = times[j-1], times[j]
                first = bisect_right(times, left-delay_s)
                last = bisect_right(times, right-delay_s)
                cuts = [left] + [t+delay_s for t in times[first:last]
                                 if left < t+delay_s < right] + [right]
                intervals.append([
                    (end-start, airspeed_start+gain*((segment[index][1] if index >= 0
                                                    else frequency_start)-frequency_start))
                    for start, end in zip(cuts, cuts[1:])
                    for index in [bisect_right(times, start-delay_s+1e-9)-1]
                ])
            return intervals

        def response(tau_s, inputs):
            predicted = airspeed_start
            values = [predicted]
            for intervals in inputs:
                # Zero-order hold of the measured V20 trajectory, including delay.
                # Split each interval at delayed input changes, so ramps are never
                # replaced by an ideal target-frequency step.
                for dt_s, equilibrium in intervals:
                    predicted = equilibrium + (predicted-equilibrium)*math.exp(-dt_s/tau_s)
                values.append(predicted)
            return values

        best = None
        for delay_s in delays:
            inputs = input_intervals(delay_s)
            for step in range(140):
                tau_s = 0.05*1.06**step
                values = response(tau_s, inputs)
                mse = fmean((v-sample[2])**2 for v, sample in zip(values[1:], segment[1:]))
                if best is None or mse < best[0]:
                    best = mse, tau_s, delay_s, values
        mse, tau_s, delay_s, predicted = best
        observed = [sample[2] for sample in segment[1:]]
        mean_observed = fmean(observed)
        variance = fmean((value-mean_observed)**2 for value in observed)
        r_squared = 1-mse/variance if variance > 1e-8 else None
        residuals = [sample[2]-value for sample, value in zip(segment[1:], predicted[1:])]
        bias = fmean(residuals)
        denominator = sum(value*value for value in residuals)
        correlation = (sum(a*b for a,b in zip(residuals, residuals[1:]))/denominator
                       if denominator > 1e-10 else 0.0)
        warnings = ["Effective model includes the existing Pitot filter; no independent filter removal.",
                    "Candidate only: independent validation and hardware commissioning are required."]
        if completeness < 0.9 or max(intervals) > max(1.0, 5*typical_dt):
            warnings.append("Incomplete or gapped time series.")
        if delay_s == max_delay:
            warnings.append("Delay reached search boundary; L is not identifiable in this record.")
        if duration < 4*tau_s:
            warnings.append("Record is too short to establish steady-state dynamics.")
        if abs(correlation) > 0.8:
            warnings.append("Strong residual correlation; inspect unmodeled dynamics.")
        fit_ok = (r_squared is not None and r_squared >= 0.8 and completeness >= 0.9
                  and duration >= 4*tau_s and delay_s != max_delay)
        item.update(tau_s=round(tau_s,4), L_s=round(delay_s,4),
            delay_identifiable=delay_s != max_delay, rmse_ms=round(math.sqrt(mse),4),
            r2=round(r_squared,4) if r_squared is not None else None,
            residual_bias_ms=round(bias,4), residual_lag1=round(correlation,4),
            data_completeness=round(completeness,4), fit_acceptable=fit_ok,
            warnings=warnings, status="CANDIDATE" if fit_ok else "REVIEW_REQUIRED",
            trace=[dict(time_s=round(t-times[0],4), actual_hz=f, measured_ms=v, model_ms=y)
                   for (t,f,v),y in zip(segment,predicted)])
        item["candidate_pi"] = simc_parameters(gain, tau_s, delay_s, 1.0)
        item["simulation"] = simulate_candidate(gain, tau_s, delay_s, item["candidate_pi"])

    return dict(
        schema_version=2,
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
    fields = ("target_hz", "status", "K_ms_per_hz", "tau_s", "L_s", "rmse_ms", "r2",
              "data_completeness", "model_valid", "hardware_commissioned", "warnings")
    with (folder / "dynamic_transitions.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(result["transitions"])
    return result
