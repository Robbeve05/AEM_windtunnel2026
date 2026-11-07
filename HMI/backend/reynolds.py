"""SI Reynolds calculations using measured temperature and existing air density.

Sutherland constants: US Standard Atmosphere 1976, equation 51:
https://www.ngdc.noaa.gov/stp/space-weather/online-publications/miscellaneous/us-standard-atmosphere-1976/us-standard-atmosphere_st76-1562_noaa.pdf
"""
import math


REYNOLDS_FIELDS = (
    'requested_reynolds', 'actual_reynolds', 'chord_mm', 'chord_m',
    'reynolds_temperature_c', 'reynolds_air_density_kg_m3',
    'dynamic_viscosity_pa_s', 'applied_airspeed_setpoint_ms', 'measured_airspeed_ms',
)


def finite_number(value, label):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f'{label} must be a finite number')
    return float(value)


def viscosity(temperature_c):
    kelvin = finite_number(temperature_c, 'Measured air temperature') + 273.15
    # Conservative MS4525DO operating range (TE Connectivity), not storage limits.
    if not 248.15 <= kelvin <= 378.15:
        raise ValueError('Measured air temperature is outside the operating range (-25 to 105 °C)')
    result = 1.458e-6 * kelvin**1.5 / (kelvin + 110.4)
    if not math.isfinite(result) or result <= 0:
        raise ValueError('Calculated viscosity is invalid')
    return result


def density_value(density):
    value = finite_number(density, 'Existing air density')
    if value <= 0:
        raise ValueError('Existing air density must be positive')
    return value


def chord_metres(chord_mm):
    value = finite_number(chord_mm, 'Airfoil chord length') / 1000.0
    if value <= 0:
        raise ValueError('Airfoil chord length must be greater than zero')
    return value


def airspeed_for_reynolds(reynolds, chord_mm, temperature_c, density):
    requested = finite_number(reynolds, 'Desired Reynolds number')
    if requested <= 0:
        raise ValueError('Desired Reynolds number must be greater than zero')
    speed = requested * viscosity(temperature_c) / (density_value(density) * chord_metres(chord_mm))
    if not math.isfinite(speed) or speed <= 0:
        raise ValueError('Calculated airspeed is invalid')
    return speed


def required_airspeed(reynolds, chord_mm, temperature_c, density, maximum=40.0):
    speed = airspeed_for_reynolds(reynolds, chord_mm, temperature_c, density)
    limit = min(40.0, finite_number(maximum, 'Configured maximum airspeed'))
    if limit <= 0 or speed > limit:
        raise ValueError(f'Reynolds setpoint requires {speed:g} m/s; maximum is {limit:g} m/s')
    return speed


def actual_reynolds(airspeed_ms, chord_mm, temperature_c, density):
    speed = finite_number(airspeed_ms, 'Measured airspeed')
    if speed < 0:
        raise ValueError('Measured airspeed must not be negative')
    result = density_value(density) * speed * chord_metres(chord_mm) / viscosity(temperature_c)
    if not math.isfinite(result):
        raise ValueError('Calculated actual Reynolds number is invalid')
    return result


def annotate_sample(sample, context, environment):
    """Actual Re uses this sample's measured speed, never its requested speed."""
    result = {key: None for key in REYNOLDS_FIELDS}
    result['measured_airspeed_ms'] = sample.get('airspeed_ms')
    if not context:
        return result
    result.update(requested_reynolds=context['requested_reynolds'],
                  chord_mm=context['chord_mm'], chord_m=context['chord_m'],
                  applied_airspeed_setpoint_ms=context['applied_airspeed_setpoint_ms'])
    if environment.get('valid'):
        result.update(reynolds_temperature_c=environment['temperature_c'],
                      reynolds_air_density_kg_m3=environment['air_density_kg_m3'],
                      dynamic_viscosity_pa_s=environment['dynamic_viscosity_pa_s'])
        try:
            result['actual_reynolds'] = actual_reynolds(
                sample.get('airspeed_ms'), context['chord_mm'], environment['temperature_c'],
                environment['air_density_kg_m3'])
        except ValueError:
            pass
    return result
