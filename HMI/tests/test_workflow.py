import csv
import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from fastapi.testclient import TestClient
import main
from backend.cp import calculate_cp
from backend.measurements import Measurements


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        (self.base / 'config').mkdir()
        config = json.loads((main.BASE / 'config/config.json').read_text())
        config['simulation'] = True
        config['simulate_usb'] = True
        (self.base / 'config/config.json').write_text(json.dumps(config))
        self.controller = main.Controller(self.base)
        self.patch = patch.object(main, 'Controller', return_value=self.controller)
        self.patch.start()
        self.client = TestClient(main.app).__enter__()

    def tearDown(self):
        self.client.__exit__(None, None, None)
        self.patch.stop()
        self.temp.cleanup()

    def post(self, path, body=None):
        result = self.client.post('/api/' + path, json=body or {})
        self.assertEqual(result.status_code, 200, result.text)
        return result.json()

    def test_full_workflow_and_independent_controls(self):
        self.controller.simulation = False
        self.assertEqual(self.client.post('/api/measurement/start', json={'name':'Test'}).status_code, 400)
        self.assertEqual(self.client.post('/api/tunnel/setpoint', json={'airspeed_ms':31}).status_code, 400)
        self.assertEqual(self.client.post('/api/measurement/start', json={'name':'../bad'}).status_code, 422)
        self.controller.simulation = True
        self.post('tunnel/setpoint', {'airspeed_ms':1})
        self.post('tunnel/start')
        # Drive the physics deterministically rather than waiting for spin-up.
        self.controller.tunnel.read(20)
        self.assertEqual(self.controller.tunnel.state, 'RUNNING')
        self.post('tunnel/setpoint', {'airspeed_ms':15})
        self.controller.tunnel.read(20)
        started = self.post('measurement/start', {'name':'Verification_Run_01'})
        self.assertEqual(self.client.post('/api/measurement/start', json={'name':'Duplicate'}).status_code, 400)
        time.sleep(.25)
        # Physical-only STOP applies in both live and simulation API routes.
        self.assertEqual(self.client.post('/api/tunnel/stop').status_code, 400)
        self.assertEqual(self.controller.measurements.state, 'RECORDING')
        result = self.post('measurement/stop')
        self.assertGreater(result['samples'], 0)
        folder = self.controller.measurements.folder_for(started['id'])
        self.assertEqual({p.name for p in folder.iterdir()}, {'raw_data.csv','processed_data.csv','cp_curve.csv','experiment.json'})
        with (folder / 'raw_data.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(len(rows), result['samples'])
        self.assertIn('host_timestamp', rows[0])
        curve = self.post(f"measurements/{started['id']}/cp", {'air_density':1.2})
        self.assertTrue(all(p['cp'] is not None for p in curve))
        self.post('storage/export/' + started['id'])
        self.assertTrue((self.controller.storage.usb / started['id'] / 'raw_data.csv').exists())
        self.assertTrue(folder.exists())
        self.assertTrue(self.client.get('/api/measurements').json()[0]['exported'])
        self.post('storage/eject')
        self.assertEqual(self.client.post('/api/storage/export/' + started['id']).status_code, 400)
        self.post('storage/reconnect')
        self.post('storage/export-all')
        self.assertEqual(self.client.get('/api/measurements/' + started['id']).status_code,200)

    def test_no_remote_stop(self):
        self.assertEqual(self.client.post('/api/tunnel/stop').status_code, 400)

    def test_websocket_reconnect(self):
        for _ in range(2):
            with self.client.websocket_connect('/ws/live') as socket:
                frame = socket.receive_json()
                self.assertIn('measurement',frame)
                self.assertTrue(frame['simulation'])

    def test_pressure_filter_setting_persists_and_validates(self):
        result = self.post('settings/pressure-filter', {'cutoff_hz': 2.5})
        self.assertEqual(result, {'cutoff_hz': 2.5, 'command_sent': False, 'device_confirmed': False})
        self.assertEqual(self.controller.config.filter_cutoff_hz, 2.5)
        saved = json.loads((self.base / 'config/config.json').read_text())
        self.assertEqual(saved['filter_cutoff_hz'], 2.5)
        self.assertEqual(self.client.get('/api/status').json()['pressure_filter']['configured_cutoff_hz'], 2.5)
        self.assertEqual(self.client.post('/api/settings/pressure-filter',
                        json={'cutoff_hz': 10.1}).status_code, 422)

    def test_recording_rate_limits_only_saved_samples(self):
        measurements = self.controller.measurements
        measurements.start('Rate_Limited', 1)
        sample = {'airspeed_ms': 2.0, 'pressure_pa': [], 'errors': []}
        self.assertTrue(measurements.append(sample, 10.0))
        self.assertFalse(measurements.append(sample, 10.5))
        self.assertTrue(measurements.append(sample, 11.0))
        result = measurements.stop()
        self.assertEqual(result['samples'], 2)
        self.assertEqual(result['recording_rate_hz'], 1)
        self.assertAlmostEqual(result['actual_sample_rate_hz'], 1.0)

    def test_complete_csv_download_includes_more_than_preview_rows(self):
        measurements = self.controller.measurements
        started = measurements.start('Complete_CSV', 100)
        sample = {'airspeed_ms': 2.0, 'pressure_pa': [], 'errors': []}
        timestamp = time.monotonic()
        for index in range(205):
            self.assertTrue(measurements.append(sample, timestamp + index * 0.04))
        result = measurements.stop()
        self.assertAlmostEqual(result['actual_sample_rate_hz'], 25.0)

        response = self.client.get(
            f"/api/measurements/{started['id']}/files/raw_data.csv"
        )
        self.assertEqual(response.status_code, 200, response.text)
        self.assertEqual(len(response.text.splitlines()), 206)
        self.controller.storage.export(started['id'])
        exported = self.controller.storage.usb / started['id'] / 'raw_data.csv'
        self.assertEqual(len(exported.read_text(encoding='utf-8').splitlines()), 206)

    def test_recording_rate_validation(self):
        self.assertEqual(
            self.client.post(
                '/api/measurement/start',
                json={'name': 'Invalid_Rate', 'recording_rate_hz': 101},
            ).status_code,
            422,
        )

    def test_cp_and_recovery(self):
        tap = [{'name':'P1','surface':'upper','x_c':.2}]
        sample = dict(airspeed_ms=10,static_pressure_pa=100000,pressure_pa=[99950])
        self.assertEqual(calculate_cp(sample,tap,1)[0]['cp'],-1)
        sample['airspeed_ms'] = 0
        self.assertIsNone(calculate_cp(sample,tap,1)[0]['cp'])
        run = self.controller.measurements.start('Interrupted')
        self.controller.measurements._close()
        recovered = Measurements(self.controller.measurements.root,self.controller.config)
        self.assertEqual(recovered.list()[0]['state'],'ERROR')
        self.controller.measurements.state = 'ERROR'
        with self.assertRaises(ValueError):
            recovered.folder_for('../outside')


if __name__ == '__main__':
    unittest.main()
