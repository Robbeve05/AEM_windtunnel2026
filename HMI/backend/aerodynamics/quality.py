def assess_quality(config, points, accepted, total, rejected, integration):
    enabled = [tap for tap in config.taps if tap.enabled]
    valid = [point for point in points if point['cp'] is not None]
    warnings = []
    if config.source == 'simulation':
        warnings.append('SIMULATION: synthetic pressure/geometry fixtures are not validated airfoil performance.')
    missing = [point['tap_id'] for point in points if point['cp'] is None]
    if missing:
        warnings.append('Missing or insufficient pressure samples: '+', '.join(missing))
    if rejected:
        warnings.append(f'{rejected} invalid, low-q or time-misaligned observations were excluded.')
    if not integration['full_chord']:
        warnings.append('Integration does not cover both complete chord surfaces; no endpoint extrapolation was applied.')
    if any(tap.z_c is None for tap in enabled):
        warnings.append('Actual surface geometry is incomplete; Ca/Cm and nonzero-incidence Cl may be unavailable.')
    cn = integration['coefficients']['cn']
    status = ('INVALID' if cn['value'] is None else
              'PARTIAL' if cn['status'] != 'FULL' or missing or rejected else 'FULL')
    return dict(status=status, valid_taps=len(valid), configured_taps=len(enabled),
                valid_observations=accepted, raw_observations=total, rejected_observations=rejected,
                data_completeness=accepted/total if total else 0,
                sensor_validity='SIMULATED' if config.source == 'simulation' else
                    'VALID' if accepted and not rejected else 'REVIEW_REQUIRED', warnings=warnings)
