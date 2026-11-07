"""Offline analysis of existing fan-calibration raw telemetry.

This module never actuates the V20, changes firmware or commissions gains.
It fits a simple first-order response to measured actual V20 frequency.
"""
import csv
import json
import math
from datetime import datetime
from pathlib import Path
from statistics import fmean


def _float(row, key):
    try:
        value = float(row[key])
        return value if math.isfinite(value) else None
    except (KeyError, ValueError, TypeError):
        return None


def identify(rows, points):
    """Return diagnostic estimates. Reject underidentified transitions."""
    data = []
    for row in rows:
        if str(row.get("valid", "")).lower() not in ("true", "1"):
            continue
        try:
            time_s = datetime.fromisoformat(str(row["timestamp"]).replace("Z", "+00:00")).timestamp()
        except (ValueError, KeyError, TypeError):
            continue
        hz, airspeed, target = (_float(row, k) for k in ("actual_hz", "pv", "target_hz"))
        if None in (hz, airspeed, target):
            continue
        data.append((time_s, hz, airspeed, target, row.get("phase")))
    report = []
    for index, point in enumerate(points):
        target = float(point["target_hz"])
        item = dict(target_hz=target, status="BASELINE_ONLY" if not index else "INSUFFICIENT_DATA",
                    K_ms_per_hz=None, tau_s=None, rmse_ms=None, r2=None)
        report.append(item)
        if not index:
            continue
        previous = points[index - 1]
        f0 = float(previous.get("mean_actual_hz", previous["target_hz"]))
        v0 = float(previous["mean_pv"])
        f1 = float(point.get("mean_actual_hz", point["target_hz"]))
        v1 = float(point["mean_pv"])
        if abs(f1 - f0) < 0.25:
            item["status"] = "INSUFFICIENT_EXCITATION"
            continue
        gain = (v1 - v0) / (f1 - f0)
        item["K_ms_per_hz"] = gain
        segment = [(t, f, v) for t, f, v, tgt, phase in data
                   if abs(tgt - target) < 0.001 and phase in
                   ("RAMPING", "STABILIZING", "MEASURING")]
        if gain <= 0 or len(segment) < 12:
            item["status"] = "INSUFFICIENT_DATA"
            continue
        segment.sort()
        if segment[-1][0] - segment[0][0] < 0.5 or any(
                segment[j][0] <= segment[j-1][0] for j in range(1, len(segment))):
            item["status"] = "INVALID_TIMESTAMPS"
            continue
        # Compare modeled output with raw measured airspeed. The 1 Hz Pitot
        # filter is already present in recorded pv; it is not filtered again.
        best = None
        for j in range(180):
            tau = 0.05 * 1.05 ** j
            predicted = v0
            squared_errors = []
            for (t0, f_prev, _), (t1, _, v_now) in zip(segment, segment[1:]):
                equilibrium = v0 + gain * (f_prev - f0)
                predicted = equilibrium + (predicted - equilibrium) * math.exp(-(t1-t0)/tau)
                squared_errors.append((predicted-v_now)**2)
            mse = fmean(squared_errors)
            if best is None or mse < best[0]:
                best = mse, tau
        mse, tau = best
        observed = [v for _, _, v in segment[1:]]
        variance = fmean((v-fmean(observed))**2 for v in observed)
        r2 = 1-mse/variance if variance > 1e-8 else None
        item.update(tau_s=round(tau, 4), rmse_ms=round(math.sqrt(mse), 4),
                    r2=round(r2, 4) if r2 is not None else None,
                    status="CANDIDATE" if r2 is not None and r2 >= 0.8 else "REVIEW_REQUIRED")
    return dict(schema_version=1, measurement="V20 actual Hz to filtered Pitot airspeed",
                advisory_only=True, message="Model is an uncommissioned candidate; no PI gains are activated.",
                transitions=report)


def analyze_calibration_folder(folder):
    """Read the unchanged four-file calibration export and write dynamics.json."""
    folder = Path(folder)
    with (folder / "raw_samples.csv").open(newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))
    with (folder / "characteristic.csv").open(newline="", encoding="utf-8") as file:
        points = list(csv.DictReader(file))
    result = identify(rows, points)
    (folder / "dynamics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result
