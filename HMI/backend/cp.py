def calculate_cp(sample: dict, taps: list[dict], density: float) -> list[dict]:
    """Tap pressures are absolute Pa, in the same reference as p_inf."""
    q = 0.5 * density * float(sample['airspeed_ms']) ** 2
    return [dict(**tap, pressure_pa=p, cp=(p - sample['static_pressure_pa']) / q if q >= 1 else None)
            for tap, p in zip(taps, sample['pressure_pa'])]
