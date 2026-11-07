import math


def dynamic_pressure(density, velocity):
    if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
           for v in (density, velocity)) or density <= 0 or velocity < 0:
        raise ValueError('Reference density and airspeed must be valid SI measurements')
    q = .5*density*velocity*velocity
    if not math.isfinite(q):
        raise ValueError('Invalid reference dynamic pressure')
    return q


def pressure_coefficient(pressure, reference, q, minimum_q_pa=1):
    if any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
           for v in (pressure, reference, q, minimum_q_pa)):
        raise ValueError('Pressure/reference must be finite')
    if minimum_q_pa < 1 or q < minimum_q_pa:
        raise ValueError('Dynamic pressure is too low for reliable Cp')
    cp = (pressure-reference)/q
    if not math.isfinite(cp):
        raise ValueError('Invalid Cp')
    return cp
