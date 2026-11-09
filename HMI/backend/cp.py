from .aerodynamics.pressure import dynamic_pressure, pressure_coefficient


def calculate_cp(sample: dict, taps: list[dict], density: float) -> list[dict]:
    """Tap pressures are absolute Pa, in the same reference as p_inf."""
    q = dynamic_pressure(density, float(sample['airspeed_ms']))
    return [dict(**tap, pressure_pa=p, cp=pressure_coefficient(p, sample['static_pressure_pa'], q) if p is not None and q >= 1 else None)
            for tap, p in zip(taps, sample['pressure_pa'])]
