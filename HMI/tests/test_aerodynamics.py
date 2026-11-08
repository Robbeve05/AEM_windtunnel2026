import asyncio
import copy
import csv
import hashlib
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import AsyncMock, patch
import xml.etree.ElementTree as ET

from fastapi.testclient import TestClient
from pypdf import PdfReader
from PIL import Image

import main
from backend.aerodynamics.acquisition import SimulationAdapter
from backend.aerodynamics.analysis import analyze
from backend.aerodynamics.comparison import compare
from backend.aerodynamics.experiments import RAW_FIELDS, AerodynamicExperiments
from backend.aerodynamics.integration import integrate_coefficients, interpolate
from backend.aerodynamics.models import ExperimentConfig, AnalysisSettings
from backend.aerodynamics.pressure import dynamic_pressure, pressure_coefficient
from backend.measurements import Measurements, save_json
from backend.models import Config
from backend.storage import Storage


def config(**values):
    return ExperimentConfig(name='Wing_test', airfoil='Synthetic diamond', chord_mm=100,
                            alpha_deg=values.pop('alpha_deg', 0), source=values.pop('source', 'simulation'),
                            averaging_s=values.pop('averaging_s', .15), **values)


def constant_points(geometry=True, partial=False):
    return [dict(surface=s, x_c=x, cp=-1 if s == 'upper' else 1,
                 z_c=(1 if s == 'upper' else -1)*.2*min(x, 1-x) if geometry else None)
            for s in ('upper', 'lower') for x in ((.2, .37, .8) if partial else (0, .08, .37, .5, .93, 1))]


class PressureAndIntegrationTests(unittest.TestCase):
    def test_known_pressure_and_dynamic_pressure(self):
        self.assertEqual(dynamic_pressure(1.2, 10), 60)
        self.assertEqual(pressure_coefficient(100030, 100000, 60), .5)
        self.assertEqual(pressure_coefficient(99940, 100000, 60), -1)
        self.assertEqual(pressure_coefficient(100001, 100000, 1), 1)

    def test_invalid_and_low_q(self):
        for q in (0, .999, -1, float('nan'), float('inf')):
            with self.assertRaises(ValueError): pressure_coefficient(1, 0, q)
        for density, velocity in ((0, 10), (-1, 10), (1.2, -1), (1.2, float('nan'))):
            with self.assertRaises(ValueError): dynamic_pressure(density, velocity)
        self.assertEqual(dynamic_pressure(1.2, 0), 0)
        with self.assertRaises(ValueError): pressure_coefficient(float('nan'), 0, 1)

    def test_constant_nonuniform_full_chord_sign_and_moment(self):
        result = integrate_coefficients(constant_points(), 0)
        self.assertTrue(result['full_chord'])
        for key, expected in (('cn', 2), ('ca', 0), ('cl', 2), ('cm', -.5)):
            self.assertAlmostEqual(result['coefficients'][key]['value'], expected)
            self.assertEqual(result['coefficients'][key]['status'], 'FULL')
        self.assertIsNone(result['coefficients']['cd']['value'])

    def test_alpha_rotation_and_geometry_required(self):
        import math
        self.assertAlmostEqual(integrate_coefficients(constant_points(), 5)['coefficients']['cl']['value'], 2*math.cos(math.radians(5)))
        missing = integrate_coefficients(constant_points(False), 5)['coefficients']
        for key in ('ca', 'cm', 'cl'): self.assertIsNone(missing[key]['value'])
        self.assertEqual(integrate_coefficients(constant_points(False), 0)['coefficients']['cl']['value'], 2)

    def test_sloped_geometry_exact_panel_moment(self):
        points = [dict(surface=s, x_c=x, cp=-x if s == 'upper' else 0,
                       z_c=(1 if s == 'upper' else -1)*.2*min(x, 1-x))
                  for s in ('upper', 'lower') for x in (0, .5, 1)]
        result = integrate_coefficients(points, 0)['coefficients']
        self.assertAlmostEqual(result['cn']['value'], .5)
        self.assertAlmostEqual(result['ca']['value'], .05)
        self.assertAlmostEqual(result['cm']['value'], -31/150)

    def test_partial_missing_and_no_extrapolation(self):
        result = integrate_coefficients(constant_points(partial=True), 0)
        self.assertFalse(result['full_chord'])
        self.assertAlmostEqual(result['coefficients']['cn']['value'], 1.2)
        self.assertEqual(result['coefficients']['cn']['status'], 'PARTIAL')
        points = [p for p in constant_points() if p['surface'] == 'upper']
        self.assertIsNone(integrate_coefficients(points, 0)['coefficients']['cn']['value'])
        with self.assertRaises(ValueError): interpolate([dict(x_c=.2, cp=1), dict(x_c=.8, cp=1)], 0, 'cp')

    def test_duplicate_positions_and_invalid_configuration(self):
        points = constant_points(); points.append(points[0])
        with self.assertRaises(ValueError): integrate_coefficients(points, 0)
        for kwargs in ({'chord_mm': 0}, {'alpha_deg': 91}, {'averaging_s': float('nan')}):
            body = config().model_dump(); body.update(kwargs)
            with self.assertRaises(ValueError): ExperimentConfig.model_validate(body)
        body = config().model_dump(); body['taps'].append(body['taps'][0])
        with self.assertRaises(ValueError): ExperimentConfig.model_validate(body)


class AnalysisDataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.folder = Path(self.temp.name)
        self.config = config(simulation_noise_pa=0)
        self.metadata = dict(id='fixture', configuration=self.config.model_dump(), duration_s=.15)
        adapter = SimulationAdapter()
        self.rows = [dict(host_timestamp='test', **row) for i in range(6) for row in adapter.sample(self.config, i, i/20, 1.225)]

    def tearDown(self): self.temp.cleanup()

    def result(self, settings=None):
        with (self.folder/'raw_data.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=RAW_FIELDS); writer.writeheader(); writer.writerows(self.rows)
        return analyze(self.folder, self.metadata, settings or AnalysisSettings())

    def test_instantaneous_average_and_pressure_standard_deviation(self):
        # Every instantaneous Cp equals one despite alternating reference q.
        for row in self.rows:
            row['reference_airspeed_ms'] = 10 if row['sample_index'] % 2 else 20
            row['pressure_pa'] = row['reference_pressure_pa']+dynamic_pressure(1.225, row['reference_airspeed_ms'])
        result = self.result()
        for point in result['cp']:
            self.assertAlmostEqual(point['cp'], 1)
            self.assertAlmostEqual(point['cp_std'], 0)
            self.assertAlmostEqual(point['pressure_std_pa'], 91.875)
        self.assertEqual(result['summary']['valid_samples'], 6)
        self.assertEqual(result['status'], 'FULL')

    def test_missing_surface_invalid_samples_and_stale_reference(self):
        self.rows = [row for row in self.rows if not row['tap_id'].startswith('L')]
        self.rows[0]['pressure_pa'] = 'nan'
        self.rows[1]['reference_timestamp_s'] = -1
        result = self.result()
        self.assertEqual(result['status'], 'INVALID')
        self.assertIsNone(result['coefficients']['cl']['value'])
        self.assertEqual(result['quality']['rejected_observations'], 2)
        self.assertTrue(any('Missing' in w for w in result['quality']['warnings']))

    def test_duplicate_observation_rejected_and_raw_untouched(self):
        self.rows.append(dict(self.rows[0]))
        result = self.result()
        self.assertEqual(result['quality']['rejected_observations'], 1)
        self.assertEqual(result['cp'][0]['valid_sample_count'], 6)
        self.assertEqual(result['status'], 'PARTIAL')
        before = (self.folder/'raw_data.csv').read_bytes()
        analyze(self.folder, self.metadata, AnalysisSettings(minimum_samples_per_tap=10))
        self.assertEqual(before, (self.folder/'raw_data.csv').read_bytes())

    def test_missing_cycles_label_partial_even_if_remaining_taps_have_enough_samples(self):
        self.metadata['samples'] = 7
        result = self.result()
        self.assertEqual(result['status'], 'PARTIAL')
        self.assertAlmostEqual(result['quality']['data_completeness'], 6/7)
        self.assertEqual(result['coefficients']['cn']['status'], 'PARTIAL')

    def test_low_q_and_insufficient_samples_no_fabricated_results(self):
        for row in self.rows: row['reference_airspeed_ms'] = .1
        result = self.result()
        self.assertEqual(result['status'], 'INVALID')
        self.assertTrue(all(p['cp'] is None for p in result['cp']))

    def test_simulation_determinism_and_sequential_timestamps(self):
        adapter = SimulationAdapter()
        a = adapter.sample(self.config, 1, .05, 1.225)
        self.assertEqual(a, adapter.sample(self.config, 1, .05, 1.225))
        self.assertGreater(a[-1]['time_s'], a[0]['time_s'])
        self.assertTrue(all(row['time_s'] == row['reference_timestamp_s'] for row in a))


class ExperimentWorkflowTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.base = Path(self.temp.name)
        self.config = Config.model_validate_json((main.BASE/'config/config.json').read_text())
        self.config.simulate_usb = True
        self.measurements = Measurements(self.base/'measurements', self.config)
        self.manager = AerodynamicExperiments(self.measurements, lambda: 1.225, lambda config: None, asyncio.Lock())

    async def asyncTearDown(self):
        await self.manager.close(); self.temp.cleanup()

    async def acquire(self, **kwargs):
        detail = await self.manager.create(config(**kwargs)); identifier = detail['metadata']['id']
        self.assertEqual(detail['metadata']['state'], 'PREPARED')
        await self.manager.start(identifier); await self.manager.tasks[identifier]
        self.assertEqual(self.manager.metadata(identifier)['state'], 'COMPLETED')
        return identifier

    async def analyze(self, identifier):
        await self.manager.run_analysis(identifier, AnalysisSettings(minimum_samples_per_tap=2))
        await self.manager.tasks[identifier]
        detail = self.manager.detail(identifier)
        self.assertIsNotNone(detail['analysis'], detail['metadata'])
        return detail

    async def test_full_simulated_workflow_exports_and_restart(self):
        self.manager.reynolds_context = lambda: dict(requested_reynolds=100000, chord_mm=100,
            chord_m=.1, applied_airspeed_setpoint_ms=14.803)
        identifier = await self.acquire(alpha_deg=5)
        self.assertEqual(self.manager.metadata(identifier)['analysis_status'], 'NOT_ANALYZED')
        detail = await self.analyze(identifier)
        folder = self.manager.folder(identifier)
        for name in ('raw_data.csv', 'processed_data.csv', 'cp_curve.csv', 'coefficients.csv',
                     'analysis.json', 'cp_distribution.png', 'cp_distribution.svg', 'analysis_report.pdf'):
            self.assertTrue((folder/name).is_file(), name)
        with (folder/'cp_curve.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), 14); self.assertEqual(rows[0]['source'], 'simulation')
        with (folder/'raw_data.csv').open() as stream:
            raw = next(csv.DictReader(stream))
        from backend.reynolds import actual_reynolds
        self.assertEqual(float(raw['requested_reynolds']), 100000)
        self.assertAlmostEqual(float(raw['actual_reynolds']), actual_reynolds(float(raw['reference_airspeed_ms']), 100, 20, 1.225))
        self.assertNotAlmostEqual(float(raw['actual_reynolds']), float(raw['requested_reynolds']))
        with Image.open(folder/'cp_distribution.png') as image: self.assertGreater(image.width, 1500)
        svg = ET.parse(folder/'cp_distribution.svg'); self.assertTrue(svg.getroot().tag.endswith('svg'))
        text = '\n'.join(page.extract_text() for page in PdfReader(folder/'analysis_report.pdf').pages)
        self.assertIn('SIMULATION', text); self.assertIn('quality and limitations', text); self.assertIn('Synthetic diamond', text)
        digest = hashlib.sha256((folder/'raw_data.csv').read_bytes()).hexdigest()
        self.assertEqual(detail['analysis']['raw_sha256'], digest)
        await self.analyze(identifier)
        self.assertEqual(hashlib.sha256((folder/'raw_data.csv').read_bytes()).hexdigest(), digest)
        self.assertTrue((folder/'analysis_history/0001/analysis.json').is_file())
        self.assertTrue((folder/'analysis_history/0002/analysis.json').is_file())
        recovered = AerodynamicExperiments(self.measurements, lambda: 1.225, lambda config: None, asyncio.Lock())
        self.assertEqual(recovered.detail(identifier)['analysis']['revision'], 2)
        storage = Storage(self.base, self.config, self.measurements); storage.export(identifier)
        self.assertTrue((storage.usb/identifier/'analysis_history/0002/analysis_report.pdf').is_file())
        self.assertTrue((folder/'raw_data.csv').is_file())

    async def test_cancel_block_unfinished_export_and_preserve_partial_raw(self):
        detail = await self.manager.create(config(averaging_s=2)); identifier = detail['metadata']['id']
        await self.manager.start(identifier); await asyncio.sleep(.08)
        with self.assertRaises(ValueError): self.manager.file(identifier, 'raw_data.csv')
        result = await self.manager.stop(identifier)
        self.assertEqual(result['metadata']['state'], 'CANCELLED')
        self.assertTrue((self.manager.folder(identifier)/'raw_data.csv').is_file())
        with self.assertRaises(ValueError): await self.manager.run_analysis(identifier, AnalysisSettings())
        storage = Storage(self.base, self.config, self.measurements)
        with self.assertRaises(ValueError): storage.export(identifier)

    async def test_physical_unavailable_no_fallback_and_paths(self):
        detail = await self.manager.create(config(source='physical')); identifier = detail['metadata']['id']
        with self.assertRaisesRegex(ValueError, 'not configured'): await self.manager.start(identifier)
        self.assertFalse((self.manager.folder(identifier)/'raw_data.csv').exists())
        for name in ('../secret', 'cp_distribution.svg/..', 'bad.json'):
            with self.assertRaises(ValueError): self.manager.file(identifier, name)
        with self.assertRaises(ValueError): self.manager.detail('../escape')

    async def test_update_only_before_acquisition_and_parallel_guard(self):
        detail = await self.manager.create(config()); identifier = detail['metadata']['id']
        await self.manager.update(identifier, config(alpha_deg=8))
        self.assertEqual(self.manager.metadata(identifier)['configuration']['alpha_deg'], 8)
        await self.manager.start(identifier)
        with self.assertRaises(ValueError): self.manager.require_idle()
        other = await self.manager.create(config())
        with self.assertRaises(ValueError): await self.manager.start(other['metadata']['id'])
        await self.manager.tasks[identifier]
        with self.assertRaises(ValueError): await self.manager.update(identifier, config())

    async def test_failed_adapter_retains_error_no_success(self):
        class Failing:
            available = simulated = True
            def sample(self, *args): raise ValueError('Sensor timeout')
        self.manager.simulation_adapter = Failing()
        detail = await self.manager.create(config()); identifier = detail['metadata']['id']
        await self.manager.start(identifier); await self.manager.tasks[identifier]
        self.assertEqual(self.manager.metadata(identifier)['state'], 'FAILED')
        self.assertIn('timeout', self.manager.metadata(identifier)['error'])

    async def test_recovery_marks_interrupted_analysis_and_legacy_preserved(self):
        self.measurements.start('legacy', 25); self.measurements.stop()
        detail = await self.manager.create(config()); metadata = detail['metadata']
        metadata['state'] = 'PROCESSING'; self.manager._save(metadata)
        recovered = AerodynamicExperiments(self.measurements, lambda: 1.225, lambda config: None, asyncio.Lock())
        self.assertEqual(recovered.metadata(metadata['id'])['state'], 'FAILED')
        self.assertEqual(len(self.measurements.list()), 2)
        self.assertEqual(len(recovered.list()), 1)


class ComparisonTests(unittest.TestCase):
    def result(self, alpha=0, speed=10, source='simulation'):
        return dict(experiment_id=str(alpha), airfoil='Wing', source=source, alpha_deg=alpha, chord_mm=100,
                    coefficients=integrate_coefficients(constant_points(), alpha)['coefficients'],
                    summary=dict(mean_airspeed_ms=speed, air_density_kg_m3=1.2))

    def test_requires_distinct_angles_same_source_and_full_coefficients(self):
        self.assertEqual(compare([self.result()])['cl_alpha'], [])
        self.assertEqual(len(compare([self.result(), self.result(5)])['cl_alpha']), 2)
        self.assertEqual(compare([self.result(), self.result(5, source='physical')])['cl_alpha'], [])
        result = self.result(5); result['coefficients']['cl']['status'] = 'PARTIAL'
        self.assertEqual(compare([self.result(), result])['cl_alpha'], [])

    def test_condition_warnings(self):
        warnings = compare([self.result(), self.result(5, speed=20)])['warnings']
        self.assertTrue(any('airspeed differs' in w for w in warnings))


class AerodynamicAPITests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.base = Path(self.temp.name); (self.base/'config').mkdir()
        body = json.loads((main.BASE/'config/config.json').read_text()); body.update(simulation=True, simulate_usb=True)
        (self.base/'config/config.json').write_text(json.dumps(body))
        self.controller = main.Controller(self.base)
        self.patches = [patch.object(main, 'Controller', return_value=self.controller),
                        patch.object(main.Controller, 'loop', new=AsyncMock())]
        # Patch class method before replacing the constructor.
        self.patches.reverse()
        for item in self.patches: item.start()
        self.client = TestClient(main.app).__enter__()

    def tearDown(self):
        self.client.__exit__(None, None, None)
        for item in reversed(self.patches): item.stop()
        self.temp.cleanup()

    def test_api_crud_status_acquisition_analysis_download_and_filters(self):
        response = self.client.post('/api/aerodynamics/experiments', json=config(alpha_deg=5).model_dump())
        self.assertEqual(response.status_code, 200, response.text); identifier = response.json()['metadata']['id']
        base = '/api/aerodynamics/experiments/'+identifier
        self.assertEqual(self.client.put(base, json=config(alpha_deg=5).model_dump()).status_code, 200)
        self.assertFalse(self.client.get('/api/aerodynamics/status').json()['physical_available'])
        self.assertEqual(self.client.post(base+'/start').status_code, 200)
        self.assertEqual(self.client.post('/api/measurement/start', json={'name': 'conflict'}).status_code, 400)
        self.assertEqual(self.client.post('/api/calibration/fan/prepare', json={}).status_code, 400)
        # A completed metadata write can become visible just before its worker
        # finishes. Wait for completion as well as state, including on busy CI.
        deadline = time.monotonic()+15
        while time.monotonic() < deadline:
            detail = self.client.get(base).json()
            if (detail['metadata']['state'] != 'ACQUIRING'
                    and self.controller.aerodynamics.tasks[identifier].done()):
                break
            time.sleep(.03)
        self.assertEqual(detail['metadata']['state'], 'COMPLETED', detail['metadata'])
        self.assertEqual(self.client.post(base+'/analyze', json={'minimum_samples_per_tap': 2}).status_code, 200)
        deadline = time.monotonic()+30
        while self.client.get(base).json()['metadata']['state'] == 'PROCESSING' and time.monotonic() < deadline: time.sleep(.05)
        detail = self.client.get(base).json(); self.assertEqual(detail['metadata']['analysis_status'], 'FULL', detail['metadata'])
        self.assertEqual(len(self.client.get(base+'/cp').json()['points']), 14)
        self.assertIsNone(self.client.get(base+'/coefficients').json()['coefficients']['cd']['value'])
        self.assertTrue(self.client.get(base+'/files/analysis_report.pdf').content.startswith(b'%PDF'))
        self.assertEqual(self.client.get(base+'/files/secret.txt').status_code, 400)
        self.assertEqual(len(self.client.get('/api/aerodynamics/experiments?airfoil=Synthetic&alpha_min=4&alpha_max=6').json()), 1)
        self.assertEqual(self.client.get('/api/aerodynamics/experiments?name=missing').json(), [])
        self.assertEqual(self.client.post('/api/aerodynamics/compare', json={'identifiers': [identifier]}).status_code, 200)
        self.assertEqual(self.client.get('/api/measurements').status_code, 200)
        self.assertEqual(self.client.get('/api/measurements/'+identifier).status_code, 200)
        self.assertEqual(self.controller.tunnel.state, 'STOPPED')

    def test_invalid_and_physical_config_are_not_silently_simulated(self):
        payload = config(source='physical').model_dump()
        created = self.client.post('/api/aerodynamics/experiments', json=payload).json()
        result = self.client.post('/api/aerodynamics/experiments/'+created['metadata']['id']+'/start')
        self.assertEqual(result.status_code, 400); self.assertIn('not configured', result.text)
        payload['chord_mm'] = 0
        self.assertEqual(self.client.post('/api/aerodynamics/experiments', json=payload).status_code, 422)


if __name__ == '__main__': unittest.main()
