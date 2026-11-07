def compare(results):
    warnings = []
    if len({r['airfoil'].casefold() for r in results}) > 1:
        warnings.append('Different airfoils: direct aerodynamic comparison may be inappropriate.')
    if len({r['source'] for r in results}) > 1:
        warnings.append('Simulation and physical data are mixed; they are not interchangeable.')
    if len({r['chord_mm'] for r in results}) > 1:
        warnings.append('Chord lengths differ; Reynolds conditions may differ.')
    for key, tolerance, description in (('mean_airspeed_ms', .1, 'airspeed'),
                                         ('air_density_kg_m3', .05, 'density')):
        values = [r['summary'][key] for r in results if r['summary'].get(key)]
        if values and max(values)/min(values)-1 > tolerance:
            warnings.append(f'Measurement {description} differs significantly.')
    full = [r for r in results if r['coefficients']['cl']['status'] == 'FULL']
    polar = []
    for airfoil, source in {(r['airfoil'], r['source']) for r in full}:
        matching = [r for r in full if r['airfoil'] == airfoil and r['source'] == source]
        if len({r['alpha_deg'] for r in matching}) >= 2:
            polar.extend(dict(experiment_id=r['experiment_id'], airfoil=airfoil, alpha_deg=r['alpha_deg'],
                              cl=r['coefficients']['cl']['value'], source=r['source'])
                         for r in sorted(matching, key=lambda r: r['alpha_deg']))
    if not polar:
        warnings.append('Cl-alpha requires full Cl results at two or more distinct angles for the same airfoil.')
    return dict(experiments=results, cl_alpha=polar, warnings=warnings)
