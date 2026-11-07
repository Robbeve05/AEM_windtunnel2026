"""Pressure forces: x/c LE->TE, z/c up; Cm positive nose-up about x_ref.

Piecewise-linear Cp and supplied surface geometry only; no endpoint extrapolation.
"""
import math
from bisect import bisect_right


def interpolate(points, x, key):
    xs = [p['x_c'] for p in points]
    if x < xs[0]-1e-12 or x > xs[-1]+1e-12:
        raise ValueError('Pressure integration cannot extrapolate outside measured regions')
    index = max(0, min(len(points)-2, bisect_right(xs, x)-1))
    a, b = points[index:index+2]
    return a[key]+(b[key]-a[key])*(x-a['x_c'])/(b['x_c']-a['x_c'])


def integrate_coefficients(points, alpha_deg, moment_reference=.25):
    empty = lambda reason: dict(value=None, status='UNAVAILABLE', reason=reason)
    result = {key: empty('Both surfaces need at least two valid pressure taps')
              for key in ('cn', 'ca', 'cl', 'cm')}
    result['cd'] = empty('Total drag requires viscous/shear or independent force/wake measurements')
    surfaces = {s: sorted([p for p in points if p['surface'] == s and p.get('cp') is not None],
                         key=lambda p: p['x_c']) for s in ('upper', 'lower')}
    for data in surfaces.values():
        if len({p['x_c'] for p in data}) != len(data):
            raise ValueError('Duplicate pressure tap positions on a surface')
    if any(len(data) < 2 for data in surfaces.values()):
        return dict(coefficients=result, full_chord=False, coverage=None,
                    method='piecewise-linear trapezoidal integration; no extrapolation')
    upper, lower = surfaces['upper'], surfaces['lower']
    start, end = max(upper[0]['x_c'], lower[0]['x_c']), min(upper[-1]['x_c'], lower[-1]['x_c'])
    if start >= end:
        return dict(coefficients=result, full_chord=False, coverage=None,
                    method='no common measured interval')
    grid = sorted({start, end, *(p['x_c'] for p in upper+lower if start <= p['x_c'] <= end)})
    full = abs(start) < 1e-9 and abs(end-1) < 1e-9
    status = 'FULL' if full else 'PARTIAL'
    reason = 'Full measured chord' if full else f'Measured interval only: x/c {start:g} to {end:g}'
    difference = [interpolate(lower, x, 'cp')-interpolate(upper, x, 'cp') for x in grid]
    cn = sum((b-a)*(v+w)/2 for a, b, v, w in zip(grid, grid[1:], difference, difference[1:]))
    result['cn'] = dict(value=cn, status=status, reason=reason)
    geometry = all(p.get('z_c') is not None for p in upper+lower)
    closed = geometry and all(abs(interpolate(upper, x, 'z_c')-interpolate(lower, x, 'z_c')) < 1e-8
                              for x in (start, end))
    if geometry:
        ca = cm = 0.0
        for surface, sign in ((upper, 1), (lower, -1)):
            for x0, x1 in zip(grid, grid[1:]):
                c0, c1 = (interpolate(surface, x, 'cp') for x in (x0, x1))
                z0, z1 = (interpolate(surface, x, 'z_c') for x in (x0, x1))
                dx, dz = x1-x0, z1-z0
                ca += sign*(c0+c1)/2*dz
                # Exact integral of linear pressure times linear lever arm on each panel.
                weighted = lambda r0, r1: (2*c0*r0+c0*r1+c1*r0+2*c1*r1)/6
                cm += sign*(dz*weighted(z0, z1)+dx*weighted(x0-moment_reference, x1-moment_reference))
        geom_status = status if closed and full else 'PARTIAL'
        geom_reason = reason if closed and full else reason+'; unmeasured boundary/end-face contributions excluded'
        result['ca'] = dict(value=ca, status=geom_status, reason=geom_reason)
        result['cm'] = dict(value=cm, status=geom_status, reason=geom_reason)
    else:
        for key in ('ca', 'cm'):
            result[key] = empty('Actual surface coordinates z/c are required; no flat-plate geometry assumed')
    alpha = math.radians(alpha_deg)
    if result['ca']['value'] is not None:
        result['cl'] = dict(value=cn*math.cos(alpha)-result['ca']['value']*math.sin(alpha),
                            status=result['ca']['status'], reason=result['ca']['reason'])
    elif abs(math.sin(alpha)) < 1e-12:
        result['cl'] = dict(value=cn*math.cos(alpha), status=status,
                            reason=reason+'; Ca cancels exactly at zero incidence')
    else:
        result['cl'] = empty('Cl requires Ca at nonzero incidence; actual surface geometry is unavailable')
    return dict(coefficients=result, full_chord=full, coverage=[start, end],
                method='piecewise-linear Cp and supplied geometry; exact panel moments; no extrapolation',
                coordinates='x/c downstream from LE, z/c upward; Cn up, Ca downstream, Cm nose-up')
