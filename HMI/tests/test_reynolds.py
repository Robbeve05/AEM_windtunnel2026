import csv
import io
import json
import math
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import AsyncMock, patch

from fastapi.testclient import TestClient

import main
from backend.measurements import Measurements
from backend.models import Setpoint
from backend.reynolds import (
    actual_reynolds, airspeed_for_reynolds, annotate_sample, required_airspeed, viscosity,
)


class ReynoldsCalculationTests(unittest.TestCase):
    def test_sutherland_and_si_conversion_round_trip(self):
        self.assertAlmostEqual(viscosity(20), 1.813405882148824e-5, delta=1e-16)
        speed = required_airspeed(100000, 100, 20, 1.225)
        self.assertAlmostEqual(speed, 14.8033133237, places=8)
        self.assertAlmostEqual(actual_reynolds(speed, 100, 20, 1.225), 100000)
        self.assertEqual(actual_reynolds(0, 100, 20, 1.225), 0)
        self.assertGreater(viscosity(30), viscosity(20))

    def test_invalid_inputs_and_no_clamping(self):
        for value in (0, -1, math.nan, math.inf, True, None):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    required_airspeed(value, 100, 20, 1.225)
                with self.assertRaises(ValueError):
                    required_airspeed(100000, value, 20, 1.225)
        for temperature in (None, math.nan, math.inf, -273.15, 106, -26, True):
            with self.assertRaises(ValueError):
                viscosity(temperature)
        for density in (0, -1, None, math.nan, math.inf, True):
            with self.assertRaises(ValueError):
                required_airspeed(100000, 100, 20, density)
        self.assertGreater(airspeed_for_reynolds(1000000, 100, 20, 1.225), 40)
        with self.assertRaisesRegex(ValueError, 'maximum'):
            required_airspeed(1000000, 100, 20, 1.225)
        with self.assertRaisesRegex(ValueError, 'maximum is 10'):
            required_airspeed(100000, 100, 20, 1.225, 10)

    def test_model_requires_paired_finite_numeric_metadata(self):
        self.assertIsNone(Setpoint(airspeed_ms=10).requested_reynolds)
        for metadata in ({'requested_reynolds': 100000}, {'chord_mm': 100},
                         {'requested_reynolds': math.nan, 'chord_mm': 100},
                         {'requested_reynolds': True, 'chord_mm': 100}):
            with self.assertRaises(ValueError):
                Setpoint(airspeed_ms=10, **metadata)

    def test_actual_reynolds_uses_measurement_and_invalid_environment_stays_blank(self):
        context = dict(requested_reynolds=100000, chord_mm=100, chord_m=0.1,
                       applied_airspeed_setpoint_ms=14.804)
        env = dict(valid=True, temperature_c=20, air_density_kg_m3=1.225,
                   dynamic_viscosity_pa_s=viscosity(20))
        row = annotate_sample({'airspeed_ms': 8}, context, env)
        self.assertAlmostEqual(row['actual_reynolds'], actual_reynolds(8, 100, 20, 1.225))
        self.assertNotEqual(row['actual_reynolds'], row['requested_reynolds'])
        self.assertIsNone(annotate_sample({'airspeed_ms': 8}, context, {'valid': False})['actual_reynolds'])


class FakeESP32:
    fresh = True
    status_timeout = 1.0

    def __init__(self, frame):
        self.frame = frame
        self.command = AsyncMock()
        self.close = AsyncMock()

    def snapshot(self):
        return dict(self.frame, connected=self.fresh, stale=not self.fresh,
                    connection_status='ONLINE' if self.fresh else 'OFFLINE')


class ReynoldsAPITests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        (self.base/'config').mkdir()
        config = json.loads((main.BASE/'config/config.json').read_text())
        config.update(simulation=False, simulate_usb=True)
        (self.base/'config/config.json').write_text(json.dumps(config))
        self.controller = main.Controller(self.base)
        self.frame = dict(state='READY', mode='AUTO', sensor_ok=1, motor_ok=1, fault='NONE',
                          temperature_c=20.0, air_density=1.225, pv=10.0, sp=0.0,
                          sample_seq=1, pressure_pa=61.25)
        self.esp32 = FakeESP32(self.frame)
        self.controller.esp32 = self.esp32
        self.controller.esp32_frame = self.frame
        self.controller.sample = dict(temperature_c=20.0, airspeed_ms=10.0)
        self.controller.last_valid_sample_monotonic = time.monotonic()
        self.controller_patch = patch.object(main, 'Controller', return_value=self.controller)
        self.controller_patch.start()
        self.loop_patch = patch.object(type(self.controller), 'loop', new=AsyncMock())
        self.loop_patch.start()
        self.client = TestClient(main.app).__enter__()

    def tearDown(self):
        self.client.__exit__(None, None, None)
        self.loop_patch.stop()
        self.controller_patch.stop()
        self.temp.cleanup()

    def payload(self, reynolds=100000, chord=100, density=1.225, temperature=20):
        return dict(airspeed_ms=required_airspeed(reynolds, chord, temperature, density),
                    requested_reynolds=reynolds, chord_mm=chord)

    def apply(self, payload=None):
        return self.client.post('/api/tunnel/setpoint', json=payload or self.payload())

    def test_existing_endpoint_sends_only_setpoint_and_does_not_start_fan(self):
        result = self.apply()
        self.assertEqual(result.status_code, 200, result.text)
        self.esp32.command.assert_awaited_once_with('SETPOINT 14.803')
        self.assertEqual(self.controller.reynolds_setpoint['requested_reynolds'], 100000)
        self.assertEqual(self.frame['state'], 'READY')
        self.assertTrue(result.json()['reynolds']['environment']['valid'])
        self.assertEqual(self.client.post('/api/tunnel/setpoint', json={'airspeed_ms': 12}).status_code, 200)
        self.assertIsNone(self.controller.reynolds_setpoint)

    def test_fresh_reported_pitot_density_is_used_not_a_second_density_model(self):
        self.frame['air_density'] = 1.18
        result = self.apply(self.payload(density=1.18))
        self.assertEqual(result.status_code, 200, result.text)
        self.assertEqual(self.controller.config.air_density, 1.225)
        self.assertEqual(self.controller.reynolds_setpoint['air_density_kg_m3'], 1.18)
        self.assertEqual(result.json()['reynolds']['environment']['air_density_kg_m3'], 1.18)

    def test_client_cannot_override_temperature_or_density_and_legacy_config_density_remains_supported(self):
        self.frame.pop('air_density')
        payload = dict(self.payload(), temperature_c=50, air_density_kg_m3=0.01)
        result = self.apply(payload)
        self.assertEqual(result.status_code, 200, result.text)
        self.assertEqual(self.controller.reynolds_setpoint['temperature_c'], 20)
        self.assertEqual(self.controller.reynolds_setpoint['air_density_kg_m3'], 1.225)

    def test_simulated_sensor_values_do_not_start_simulator_or_send_hardware_commands(self):
        from backend.simulator import Simulator
        self.controller.simulation = True
        self.controller.esp32_mode = False
        self.controller.tunnel = Simulator(self.controller.config)
        initial = self.controller.tunnel.state
        self.assertEqual(self.apply().status_code, 200)
        self.assertEqual(self.controller.tunnel.state, initial)
        self.esp32.command.assert_not_awaited()

    def test_missing_invalid_and_frozen_temperature_never_substitute_zero(self):
        for value in (None, math.nan, math.inf, -300, 200):
            self.frame['temperature_c'] = value
            result = self.apply()
            self.assertEqual(result.status_code, 400, result.text)
        self.frame.pop('temperature_c')
        self.assertEqual(self.apply().status_code, 400)
        self.frame['temperature_c'] = 20
        self.controller.last_valid_sample_monotonic = time.monotonic()-1.1
        self.assertEqual(self.apply().status_code, 400)
        self.esp32.command.assert_not_awaited()
        sample = self.controller._hardware_sample({'airspeed_ms': 10, 'pressure_pa': 61.25}, time.monotonic())
        self.assertIsNone(sample['temperature_c'])

    def test_no_apply_on_stale_sensor_non_auto_or_non_ready(self):
        for key, value in (('sensor_ok', 0), ('mode', 'MANUAL'), ('state', 'RUNNING'),
                           ('state', 'STOP'), ('state', 'FAULT')):
            original = self.frame[key]
            self.frame[key] = value
            self.assertEqual(self.apply().status_code, 400)
            self.frame[key] = original
        self.esp32.fresh = False
        self.assertEqual(self.apply().status_code, 400)
        self.esp32.command.assert_not_awaited()

    def test_limits_mismatch_and_failed_ack_do_not_store_context(self):
        for payload in (dict(airspeed_ms=40, requested_reynolds=1000000, chord_mm=100),
                        dict(airspeed_ms=1, requested_reynolds=100000, chord_mm=100),
                        dict(airspeed_ms=40.01), dict(airspeed_ms=1, requested_reynolds=0, chord_mm=100)):
            self.assertIn(self.apply(payload).status_code, (400, 422))
        self.esp32.command.assert_not_awaited()
        self.esp32.command.side_effect = ValueError('Drive did not acknowledge setpoint')
        self.assertEqual(self.apply().status_code, 400)
        self.assertIsNone(self.controller.reynolds_setpoint)

    def test_calibration_lock_and_lower_configured_limit_are_preserved(self):
        self.controller.config.max_airspeed_ms = 10
        self.assertEqual(self.apply().status_code, 400)
        self.controller.config.max_airspeed_ms = 40
        with patch.object(self.controller.calibration, 'require_idle', side_effect=ValueError('Calibration active')):
            self.assertEqual(self.apply().status_code, 400)
        self.esp32.command.assert_not_awaited()

    def test_achieved_reynolds_tracks_new_temperature_without_changing_requested_setpoint(self):
        result = self.apply()
        self.assertEqual(result.status_code, 200, result.text)
        self.frame.update(temperature_c=30, pv=8, sample_seq=2)
        self.client.portal.call(self.controller._esp32_status, dict(self.frame))
        sample = self.controller.sample
        self.assertAlmostEqual(sample['actual_reynolds'], actual_reynolds(8, 100, 30, 1.225))
        self.assertEqual(sample['reynolds_temperature_c'], 30)
        self.assertEqual(sample['applied_airspeed_setpoint_ms'], 14.803)
        self.assertEqual(sample['requested_reynolds'], 100000)
        self.esp32.command.assert_awaited_once()

    def test_reynolds_columns_and_json_survive_history_and_usb_export(self):
        result = self.apply()
        self.assertEqual(result.status_code, 200, result.text)
        started = self.client.post('/api/measurement/start', json={'name': 'Reynolds_100k'}).json()
        self.frame['sp'] = 14.803
        self.client.portal.call(self.controller._esp32_status, dict(self.frame))
        stopped = self.client.post('/api/measurement/stop')
        self.assertEqual(stopped.status_code, 200, stopped.text)
        identifier = started['id']
        detail = self.client.get('/api/measurements/'+identifier).json()
        expected = actual_reynolds(10, 100, 20, 1.225)
        for filename in ('raw_data.csv', 'processed_data.csv'):
            response = self.client.get(f'/api/measurements/{identifier}/files/{filename}')
            self.assertEqual(response.status_code, 200)
            row = next(csv.DictReader(io.StringIO(response.text)))
            self.assertAlmostEqual(float(row['actual_reynolds']), expected)
            self.assertEqual(float(row['requested_reynolds']), 100000)
            self.assertEqual(float(row['chord_mm']), 100)
            self.assertEqual(float(row['chord_m']), 0.1)
            self.assertEqual(float(row['reynolds_temperature_c']), 20)
            self.assertEqual(float(row['reynolds_air_density_kg_m3']), 1.225)
            self.assertAlmostEqual(float(row['dynamic_viscosity_pa_s']), viscosity(20))
            self.assertEqual(float(row['applied_airspeed_setpoint_ms']), 14.803)
            self.assertEqual(float(row['measured_airspeed_ms']), 10)
        self.assertAlmostEqual(detail['metadata']['last_reynolds_sample']['actual_reynolds'], expected)
        self.assertEqual(detail['metadata']['reynolds_setpoint']['requested_reynolds'], 100000)
        self.assertEqual(self.client.post('/api/storage/export/'+identifier).status_code, 200)
        exported = self.controller.storage.usb/identifier
        metadata = json.loads((exported/'experiment.json').read_text())
        self.assertAlmostEqual(metadata['last_reynolds_sample']['actual_reynolds'], expected)
        reloaded = Measurements(self.controller.measurements.root, self.controller.config)
        self.assertEqual(reloaded.list()[0]['reynolds_setpoint']['chord_mm'], 100)
