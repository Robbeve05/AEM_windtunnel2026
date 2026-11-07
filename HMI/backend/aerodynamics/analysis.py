import csv
import math
import statistics
from datetime import datetime, timezone

from .models import AnalysisSettings, ExperimentConfig
from .pressure import dynamic_pressure, pressure_coefficient
from .integration import integrate_coefficients
from .quality import assess_quality
from ..reynolds import actual_reynolds, viscosity

ANALYSIS_VERSION = '1.0.0'


def analyze(folder, metadata, settings: AnalysisSettings):
    config = ExperimentConfig.model_validate(metadata['configuration'])
    taps = {tap.tap_id: tap for tap in config.taps if tap.enabled}
    observations = {key: [] for key in taps}
    total = rejected = 0
    seen = set()
    with (folder/'raw_data.csv').open(newline='', encoding='utf-8') as stream:
        for row in csv.DictReader(stream):
            total += 1
            try:
                valid_statuses = ('OK', 'VALID', 'SIMULATED') if config.source == 'simulation' else ('OK', 'VALID')
                if (row.get('tap_id') not in taps or str(row.get('valid')).lower() not in ('true', '1')
                        or row.get('sensor_status') not in valid_statuses):
                    raise ValueError('Invalid tap observation')
                values = {key: float(row[key]) for key in ('time_s', 'reference_timestamp_s',
                    'pressure_pa', 'reference_pressure_pa', 'reference_airspeed_ms', 'air_density_kg_m3')}
                if not all(math.isfinite(v) for v in values.values()):
                    raise ValueError('Nonfinite observation')
                if abs(values['time_s']-values['reference_timestamp_s']) > settings.max_reference_age_s:
                    raise ValueError('Pressure/reference timing mismatch')
                identity = (row['tap_id'], row['sample_index'])
                if identity in seen or row.get('source') != config.source or row.get('channel') != taps[row['tap_id']].channel:
                    raise ValueError('Duplicate observation or source/channel mismatch')
                q = dynamic_pressure(values['air_density_kg_m3'], values['reference_airspeed_ms'])
                cp = pressure_coefficient(values['pressure_pa'], values['reference_pressure_pa'], q,
                                          settings.minimum_q_pa)
                values.update(cp=cp, dynamic_pressure_pa=q, sample_index=row['sample_index'])
                for key in ('actual_reynolds', 'requested_reynolds', 'temperature_c',
                            'dynamic_viscosity_pa_s', 'applied_airspeed_setpoint_ms'):
                    try:
                        value = float(row[key])
                        values[key] = value if math.isfinite(value) else None
                    except (KeyError, ValueError, TypeError):
                        values[key] = None
                try:
                    values['dynamic_viscosity_pa_s'] = viscosity(values['temperature_c'])
                    values['actual_reynolds'] = actual_reynolds(values['reference_airspeed_ms'], config.chord_mm,
                        values['temperature_c'], values['air_density_kg_m3'])
                except ValueError:
                    values['actual_reynolds'] = values['dynamic_viscosity_pa_s'] = None
                observations[row['tap_id']].append(values)
                seen.add(identity)
            except (KeyError, ValueError, TypeError, OverflowError):
                rejected += 1
    points = []
    accepted_values = []
    for key, tap in taps.items():
        samples = observations[key]
        accepted_values.extend(samples)
        enough = len(samples) >= settings.minimum_samples_per_tap
        mean = lambda name: statistics.fmean(item[name] for item in samples) if samples else None
        points.append(dict(tap_id=key, source=config.source, surface=tap.surface, x_c=tap.x_c, z_c=tap.z_c,
            mean_pressure_pa=mean('pressure_pa'), reference_pressure_pa=mean('reference_pressure_pa'),
            dynamic_pressure_pa=mean('dynamic_pressure_pa'), cp=mean('cp') if enough else None,
            pressure_std_pa=statistics.pstdev(item['pressure_pa'] for item in samples) if samples else None,
            cp_std=statistics.pstdev(item['cp'] for item in samples) if enough else None,
            valid_sample_count=len(samples), valid=enough))
    integration = integrate_coefficients(points, config.alpha_deg, settings.moment_reference_x_c)
    quality = assess_quality(config, points, len(accepted_values), total, rejected, integration)
    cycles = max(metadata.get('samples', 0), max((len(values) for values in observations.values()), default=0))
    expected = cycles*len(taps)
    quality['expected_observations'] = expected
    quality['data_completeness'] = min(1, len(accepted_values)/expected) if expected else 0
    if len(accepted_values) < expected:
        quality['warnings'].append(f'{expected-len(accepted_values)} expected observations are missing or rejected.')
        if quality['status'] == 'FULL':
            quality['status'] = 'PARTIAL'
    if quality['status'] == 'PARTIAL':
        for coefficient in integration['coefficients'].values():
            if coefficient['status'] == 'FULL':
                coefficient.update(status='PARTIAL', reason=coefficient['reason']+'; incomplete/invalid observations excluded')
    mean = lambda key: statistics.fmean(item[key] for item in accepted_values) if accepted_values else None
    speed, density = mean('reference_airspeed_ms'), mean('air_density_kg_m3')
    summary = dict(mean_airspeed_ms=speed, mean_dynamic_pressure_pa=mean('dynamic_pressure_pa'),
        air_density_kg_m3=density, valid_samples=len({item['sample_index'] for item in accepted_values}),
        tap_count=len(taps), duration_s=metadata.get('duration_s', 0),
        observed_span_s=max(item['time_s'] for item in accepted_values)-min(item['time_s'] for item in accepted_values)
            if accepted_values else 0)
    for key in ('actual_reynolds', 'requested_reynolds', 'temperature_c', 'dynamic_viscosity_pa_s', 'applied_airspeed_setpoint_ms'):
        values = [item[key] for item in accepted_values if item.get(key) is not None]
        summary[key] = statistics.fmean(values) if values else None
    if accepted_values and speed:
        std = statistics.pstdev(item['reference_airspeed_ms'] for item in accepted_values)
        summary['airspeed_std_ms'] = std
        if std/speed > .05:
            quality['warnings'].append('Reference airspeed varied by more than 5%; inspect averaging conditions.')
    return dict(analysis_version=ANALYSIS_VERSION, generated_at=datetime.now(timezone.utc).isoformat(),
        experiment_id=metadata['id'], experiment_name=config.name, airfoil=config.airfoil,
        alpha_deg=config.alpha_deg, chord_mm=config.chord_mm, source=config.source,
        simulated=config.source == 'simulation', status=quality['status'], settings=settings.model_dump(),
        summary=summary, quality=quality, cp=points, **integration,
        averaging='Arithmetic mean of instantaneous Cp from each time-aligned pressure/reference pair; population standard deviation.',
        assumptions=['Piecewise-linear segments between measured taps; no invented taps or endpoint extrapolation.',
                     '2D sectional pressure coefficients only; total drag is unavailable without viscous/independent measurements.'])
