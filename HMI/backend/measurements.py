import csv
import json
import os
import re
import time
from datetime import datetime, timezone
from pathlib import Path
from uuid import uuid4
from .cp import calculate_cp


def save_json(path: Path, value: dict) -> None:
    temp = path.with_suffix('.tmp')
    with temp.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.flush()
        os.fsync(stream.fileno())
    temp.replace(path)


class Measurements:
    def __init__(self, root: Path, config):
        self.root, self.config = root, config
        root.mkdir(parents=True, exist_ok=True)
        self.state = 'IDLE'
        self.meta = None
        self.files = []
        self.elapsed = 0.0
        self.total_speed = 0.0
        self.first_recorded_monotonic = None
        self.last_recorded_monotonic = None
        self.last_flush_monotonic = None
        self.error = None
        # A previous process may have ended before closing a run.
        for path in root.glob('*/experiment.json'):
            metadata = json.loads(path.read_text(encoding='utf-8'))
            if metadata.get('state') in ('RECORDING', 'SAVING'):
                metadata.update(state='ERROR', error='Interrupted process; CSV files retained for recovery')
                save_json(path, metadata)

    def status(self):
        elapsed = time.monotonic() - self.started if self.state == 'RECORDING' else self.elapsed
        samples = self.meta['samples'] if self.meta else 0
        return dict(
            state=self.state,
            name=self.meta['name'] if self.meta else '',
            samples=samples,
            elapsed_s=elapsed,
            sample_rate_hz=self.config.sample_rate_hz,
            recording_rate_hz=self.meta.get('recording_rate_hz') if self.meta else None,
            actual_sample_rate_hz=self._actual_sample_rate_hz(),
            error=self.error,
            file_status='Writing locally' if self.state == 'RECORDING' else self.state,
        )

    def start(self, name: str, recording_rate_hz: int = 25):
        if self.state in ('RECORDING', 'SAVING'):
            raise ValueError('A measurement is already active')
        if not 1 <= recording_rate_hz <= 100:
            raise ValueError('Recording sample frequency must be between 1 and 100 Hz')
        clean = re.sub(r'[^A-Za-z0-9_-]+', '_', name).strip('_')[:80]
        if not clean:
            raise ValueError('Enter a valid measurement name')
        now = datetime.now(timezone.utc)
        identifier = now.strftime('%Y-%m-%d_%H-%M-%S_%f_') + clean + '_' + uuid4().hex[:6]
        self.folder = self.root / identifier
        self.folder.mkdir()
        self.meta = dict(id=identifier, name=name, timestamp=now.isoformat(), state='RECORDING',
                         samples=0, duration_s=0, average_airspeed_ms=0, exported=False,
                         recording_rate_hz=recording_rate_hz,
                         pressure_reference='pitot differential Pa', sensor='ASPD-4525 via Arduino Uno', config=self.config.model_dump())
        self.started = time.monotonic()
        self.first_recorded_monotonic = None
        self.last_recorded_monotonic = None
        self.last_flush_monotonic = self.started
        self.elapsed = self.total_speed = 0.0
        self.error = None
        try:
            save_json(self.folder / 'experiment.json', self.meta)
            raw = self._writer('raw_data.csv', ['host_timestamp', 'timestamp_ms', 'state', 'airspeed_ms', 'airspeed_kmh',
                               'differential_pressure_pa', 'dynamic_pressure_pa', 'temperature_c',
                               'flow_stability_pct', 'flow_mean_ms', 'flow_std_ms', 'flow_min_ms', 'flow_max_ms',
                               'static_pressure_pa', 'fan_output_pct', 'angle_of_attack_deg', 'pressure_pa', 'errors'])
            processed = self._writer('processed_data.csv', ['host_timestamp', 'timestamp_ms', 'airspeed_ms', 'airspeed_kmh',
                                     'dynamic_pressure_pa', 'temperature_c', 'flow_stability_pct', 'flow_mean_ms',
                                     'flow_std_ms', 'flow_min_ms', 'flow_max_ms'])
            self.raw, self.processed = raw, processed
            self.state = 'RECORDING'
        except Exception as exc:
            self.fail(str(exc))
            raise
        return self.meta

    def _writer(self, filename, fields):
        stream = (self.folder / filename).open('w', newline='', encoding='utf-8')
        self.files.append(stream)
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        return writer

    def append(self, sample: dict, received_at: float | None = None):
        if self.state != 'RECORDING':
            return False
        now = time.monotonic() if received_at is None else received_at
        minimum_interval = 1.0 / self.meta['recording_rate_hz']
        if (
            self.last_recorded_monotonic is not None
            and now - self.last_recorded_monotonic + 1e-9 < minimum_interval
        ):
            return False
        row = {key: sample.get(key) for key in self.raw.fieldnames}
        row['pressure_pa'] = json.dumps(sample.get('pressure_pa', []))
        row['errors'] = json.dumps(sample.get('errors', []))
        self.raw.writerow(row)
        self.processed.writerow({key: sample.get(key) for key in self.processed.fieldnames})
        self.meta['samples'] += 1
        self.total_speed += sample['airspeed_ms']
        if self.first_recorded_monotonic is None:
            self.first_recorded_monotonic = now
        self.last_recorded_monotonic = now
        if now - self.last_flush_monotonic >= 1.0:
            for stream in self.files:
                stream.flush()
            save_json(self.folder / 'experiment.json', self.meta)
            self.last_flush_monotonic = now
        return True

    def _actual_sample_rate_hz(self) -> float:
        if (
            not self.meta
            or self.meta['samples'] < 2
            or self.first_recorded_monotonic is None
            or self.last_recorded_monotonic is None
        ):
            return 0.0
        sample_span = self.last_recorded_monotonic - self.first_recorded_monotonic
        return (self.meta['samples'] - 1) / sample_span if sample_span > 0 else 0.0

    def _close(self):
        failure = None
        for stream in self.files:
            try:
                stream.flush()
                os.fsync(stream.fileno())
            except OSError as exc:
                failure = exc
            finally:
                stream.close()
        self.files = []
        if failure:
            raise failure

    def fail(self, reason: str):
        self.state, self.error = 'ERROR', reason
        try:
            self._close()
        finally:
            if self.meta:
                self.meta.update(state='ERROR', error=reason)
                save_json(self.folder / 'experiment.json', self.meta)

    def stop(self):
        if self.state != 'RECORDING':
            raise ValueError('No measurement is recording')
        self.state = 'SAVING'
        try:
            self._close()
            self.elapsed = time.monotonic() - self.started
            self.meta.update(state='COMPLETED', duration_s=self.elapsed,
                             average_airspeed_ms=self.total_speed / max(1, self.meta['samples']),
                             actual_sample_rate_hz=self._actual_sample_rate_hz())
            save_json(self.folder / 'experiment.json', self.meta)
            # Cp processing is intentionally deferred until the pressure-tap hardware is commissioned.
            self.state = 'COMPLETED'
        except Exception as exc:
            self.fail(str(exc))
            raise
        return self.meta

    def folder_for(self, identifier: str) -> Path:
        if Path(identifier).name != identifier or '/' in identifier or '\\' in identifier:
            raise ValueError('Invalid measurement identifier')
        folder = (self.root / identifier).resolve()
        if folder.parent != self.root.resolve() or not (folder / 'experiment.json').is_file():
            raise ValueError('Measurement not found')
        return folder

    def list(self):
        return sorted([json.loads(p.read_text(encoding='utf-8')) for p in self.root.glob('*/experiment.json')],
                      key=lambda m: m['timestamp'], reverse=True)

    def detail(self, identifier):
        folder = self.folder_for(identifier)
        result = dict(metadata=json.loads((folder / 'experiment.json').read_text(encoding='utf-8')))
        for key, filename in [('raw', 'raw_data.csv'), ('processed', 'processed_data.csv'), ('cp', 'cp_curve.csv')]:
            result[key] = []
            path = folder / filename
            if path.exists():
                with path.open(encoding='utf-8', newline='') as stream:
                    for i, row in enumerate(csv.DictReader(stream)):
                        if i >= 200:
                            break
                        result[key].append(row)
        return result

    def recalculate(self, identifier: str, density: float):
        raise ValueError('Cp calculation is in development until the pressure-tap system is connected')
