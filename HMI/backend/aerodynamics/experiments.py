import asyncio
import csv
import hashlib
import json
import math
import shutil
import time
from datetime import datetime, timezone
from pathlib import Path
from uuid import uuid4

from ..measurements import save_json
from ..reynolds import REYNOLDS_FIELDS, actual_reynolds, viscosity
from .acquisition import SimulationAdapter, UnconfiguredPhysicalAdapter
from .analysis import analyze
from .export import write_exports
from .models import AnalysisSettings, ExperimentConfig
from .comparison import compare

RAW_FIELDS = ['host_timestamp', 'sample_index', 'time_s', 'reference_timestamp_s', 'tap_id',
              'channel', 'pressure_pa', 'reference_pressure_pa', 'reference_airspeed_ms',
              'air_density_kg_m3', 'temperature_c', 'valid', 'sensor_status', 'source', *REYNOLDS_FIELDS]
EXPORT_FILES = {'experiment.json', 'raw_data.csv', 'processed_data.csv', 'cp_curve.csv',
                'coefficients.csv', 'analysis.json', 'cp_distribution.png', 'cp_distribution.svg',
                'analysis_report.pdf'}


async def disk_work(function, *args):
    """Do not close/cancel a write while its filesystem thread is still running."""
    task = asyncio.create_task(asyncio.to_thread(function, *args))
    try:
        return await asyncio.shield(task)
    except asyncio.CancelledError:
        await task
        raise


class AerodynamicExperiments:
    def __init__(self, measurements, density, guard, export_lock, reynolds_context=lambda: None):
        self.measurements = measurements
        self.density = density
        self.guard = guard
        self.export_lock = export_lock
        self.reynolds_context = reynolds_context
        self.physical_adapter = UnconfiguredPhysicalAdapter()
        self.simulation_adapter = SimulationAdapter()
        self.tasks = {}
        self.lock = asyncio.Lock()
        for metadata in self.list():
            if metadata['state'] in ('ACQUIRING', 'PROCESSING'):
                metadata.update(state='FAILED', analysis_status='INTERRUPTED',
                                error='Interrupted process; original raw data retained')
                self._save(metadata)

    def _save(self, metadata):
        save_json(self.measurements.root/metadata['id']/'experiment.json', metadata)

    def require_idle(self):
        if any(not task.done() for task in self.tasks.values()):
            raise ValueError('Wait for aerodynamic acquisition/analysis to finish')

    def folder(self, identifier):
        folder = self.measurements.folder_for(identifier)
        metadata = json.loads((folder/'experiment.json').read_text(encoding='utf-8'))
        if metadata.get('kind') != 'aerodynamic':
            raise ValueError('Select an aerodynamic experiment; legacy measurements remain in Measurements')
        return folder

    def metadata(self, identifier):
        return json.loads((self.folder(identifier)/'experiment.json').read_text(encoding='utf-8'))

    def list(self):
        return [metadata for metadata in self.measurements.list() if metadata.get('kind') == 'aerodynamic']

    def detail(self, identifier):
        metadata = self.metadata(identifier)
        path = self.folder(identifier)/'analysis.json'
        result = (json.loads(path.read_text(encoding='utf-8')) if path.is_file() and
                  metadata.get('analysis_status') in ('FULL', 'PARTIAL', 'INVALID') else None)
        return dict(metadata=metadata, analysis=result,
                    physical_available=bool(self.physical_adapter.available),
                    files=sorted(p.name for p in self.folder(identifier).iterdir()
                                 if p.is_file() and p.name in EXPORT_FILES))

    async def create(self, config):
        async with self.lock:
            now = datetime.now(timezone.utc)
            identifier = now.strftime('%Y-%m-%d_%H-%M-%S_%f_')+'aero_'+uuid4().hex[:8]
            folder = self.measurements.root/identifier
            await disk_work(folder.mkdir)
            metadata = dict(id=identifier, name=config.name, timestamp=now.isoformat(),
                kind='aerodynamic', state='PREPARED', analysis_status='NOT_ANALYZED',
                source=config.source, simulated=config.source == 'simulation',
                configuration=config.model_dump(), samples=0, duration_s=0,
                average_airspeed_ms=None, exported=False, progress=0, error=None,
                geometry_source='explicit synthetic fixture or operator-supplied coordinates' if config.source == 'simulation'
                else 'operator configuration; physical adapter must verify',
                air_density_kg_m3=self.density())
            await disk_work(self._save, metadata)
            return self.detail(identifier)

    async def update(self, identifier, config):
        async with self.lock:
            metadata = self.metadata(identifier)
            if metadata['state'] != 'PREPARED' or (self.folder(identifier)/'raw_data.csv').exists():
                raise ValueError('Only a prepared experiment without raw data may be reconfigured')
            metadata.update(configuration=config.model_dump(), name=config.name,
                            source=config.source, simulated=config.source == 'simulation')
            await disk_work(self._save, metadata)
            return self.detail(identifier)

    async def start(self, identifier):
        async with self.lock:
            metadata = self.metadata(identifier)
            if metadata['state'] != 'PREPARED':
                raise ValueError('Prepare a new experiment before starting acquisition')
            if any(not task.done() for task in self.tasks.values()):
                raise ValueError('Another aerodynamic acquisition or analysis is active')
            config = ExperimentConfig.model_validate(metadata['configuration'])
            self.guard(config)
            adapter = self.simulation_adapter if config.source == 'simulation' else self.physical_adapter
            if not adapter.available:
                raise ValueError('Physical pressure adapter is not configured; real acquisition is disabled')
            if bool(adapter.simulated) != (config.source == 'simulation'):
                raise ValueError('Adapter source mismatch; simulation must never substitute physical acquisition')
            if not config.taps or not any(tap.enabled for tap in config.taps):
                raise ValueError('Configure enabled pressure taps before acquisition')
            if config.source == 'physical' and not callable(getattr(adapter, 'validate', None)):
                raise ValueError('Physical adapter must validate sensors, calibration and reference conditions')
            if callable(getattr(adapter, 'validate', None)):
                await asyncio.to_thread(adapter.validate, config, self.density())
            metadata.update(state='ACQUIRING', progress=0, error=None, source=config.source,
                            started_at=datetime.now(timezone.utc).isoformat(),
                            reynolds_setpoint=self.reynolds_context())
            await disk_work(self._save, metadata)
            self.tasks[identifier] = asyncio.create_task(self._acquire(metadata, config, adapter))
            return self.detail(identifier)

    async def _acquire(self, metadata, config, adapter):
        identifier = metadata['id']
        folder = self.folder(identifier)
        def initialize():
            with (folder/'raw_data.csv').open('x', newline='', encoding='utf-8') as stream:
                csv.DictWriter(stream, fieldnames=RAW_FIELDS).writeheader()
        def append(rows):
            with (folder/'raw_data.csv').open('a', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=RAW_FIELDS)
                writer.writerows(rows)
        started = time.monotonic()
        try:
            await disk_work(initialize)
            count = max(1, math.ceil(config.averaging_s*config.sample_rate_hz))
            for index in range(count):
                self.guard(config)  # Loss of physical permissives aborts, never switches adapters.
                rows = await asyncio.to_thread(adapter.sample, config, index,
                                                index/config.sample_rate_hz, self.density())
                for row in rows:
                    if row.get('source') != config.source:
                        raise ValueError('Pressure observation source does not match selected acquisition source')
                    row['host_timestamp'] = datetime.now(timezone.utc).isoformat()
                    context = metadata.get('reynolds_setpoint') or {}
                    row.update({key: None for key in REYNOLDS_FIELDS})
                    row.update(chord_mm=config.chord_mm, chord_m=config.chord_mm/1000,
                        measured_airspeed_ms=row.get('reference_airspeed_ms'),
                        requested_reynolds=context.get('requested_reynolds'),
                        applied_airspeed_setpoint_ms=context.get('applied_airspeed_setpoint_ms'))
                    try:
                        mu = viscosity(row.get('temperature_c'))
                        achieved = actual_reynolds(row.get('reference_airspeed_ms'), config.chord_mm,
                                                  row['temperature_c'], row['air_density_kg_m3'])
                        row.update(actual_reynolds=achieved, reynolds_temperature_c=row['temperature_c'],
                            reynolds_air_density_kg_m3=row['air_density_kg_m3'], dynamic_viscosity_pa_s=mu)
                    except (ValueError, KeyError):
                        pass  # Pressure analysis remains possible; no default temperature/Re is invented.
                await disk_work(append, rows)
                metadata.update(samples=index+1, progress=round(100*(index+1)/count),
                                duration_s=min(config.averaging_s, (index+1)/config.sample_rate_hz))
                await disk_work(self._save, metadata)
                await asyncio.sleep(max(0, started+(index+1)/config.sample_rate_hz-time.monotonic()))
            metadata.update(state='COMPLETED', progress=100,
                            message='Acquisition complete; analysis has not yet been validated')
        except asyncio.CancelledError:
            metadata.update(state='CANCELLED', error='Acquisition cancelled; partial raw samples retained')
        except Exception as exc:
            metadata.update(state='FAILED', error=str(exc))
        finally:
            metadata['finished_at'] = datetime.now(timezone.utc).isoformat()
            await disk_work(self._save, metadata)

    async def stop(self, identifier):
        metadata = self.metadata(identifier)
        if metadata['state'] != 'ACQUIRING':
            raise ValueError('No acquisition is running for this experiment')
        task = self.tasks.get(identifier)
        if task:
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                metadata.update(state='CANCELLED', error='Acquisition cancelled before first observation')
                await disk_work(self._save, metadata)
        return self.detail(identifier)

    async def run_analysis(self, identifier, settings):
        async with self.lock:
            metadata = self.metadata(identifier)
            if metadata['state'] != 'COMPLETED':
                raise ValueError('Only a completed acquisition may be analyzed')
            if any(not task.done() for task in self.tasks.values()):
                raise ValueError('Another acquisition or analysis is active')
            async with self.export_lock:
                # Read again after an in-progress USB export may have updated metadata.
                metadata = self.metadata(identifier)
                metadata.update(state='PROCESSING', analysis_status='PROCESSING', error=None, exported=False, progress=0)
                await disk_work(self._save, metadata)
            self.tasks[identifier] = asyncio.create_task(self._process(metadata, settings))
            return self.detail(identifier)

    async def _process(self, metadata, settings):
        async with self.export_lock:
            try:
                result = await disk_work(self._analyze_and_publish, metadata, settings)
                metadata.update(state='COMPLETED', analysis_status=result['status'], progress=100,
                    average_airspeed_ms=result['summary']['mean_airspeed_ms'],
                    analysis_revision=result['revision'], analysis_version=result['analysis_version'],
                    message='Analysis finished: '+result['status'])
            except Exception as exc:
                metadata.update(state='COMPLETED', analysis_status='FAILED', error=str(exc))
            await disk_work(self._save, metadata)

    def _analyze_and_publish(self, metadata, settings):
        folder = self.folder(metadata['id'])
        result = analyze(folder, metadata, settings)
        result['raw_sha256'] = hashlib.sha256((folder/'raw_data.csv').read_bytes()).hexdigest()
        revision = metadata.get('analysis_revision', 0)+1
        while (folder/'analysis_history'/f'{revision:04d}').exists():
            revision += 1
        result['revision'] = revision
        destination = folder/'analysis_history'/f'{revision:04d}'
        write_exports(destination, result, metadata)
        # All derived revisions persist. Original raw_data.csv is never rewritten.
        for filename in EXPORT_FILES-{'raw_data.csv', 'experiment.json'}:
            source = destination/filename
            target = folder/filename
            if source.is_file():
                temporary = target.with_suffix(target.suffix+'.tmp')
                shutil.copyfile(source, temporary)
                temporary.replace(target)
            elif target.exists():
                target.unlink()
        return result

    def comparison(self, identifiers):
        results = []
        for identifier in dict.fromkeys(identifiers):
            detail = self.detail(identifier)
            if detail['metadata']['state'] != 'COMPLETED' or not detail['analysis']:
                raise ValueError('Comparison requires completed, analyzed experiments')
            results.append(detail['analysis'])
        return compare(results)

    def file(self, identifier, filename):
        if filename not in EXPORT_FILES:
            raise ValueError('Analysis file is not available')
        if self.metadata(identifier)['state'] in ('ACQUIRING', 'PROCESSING'):
            raise ValueError('Wait for acquisition/processing to complete before exporting')
        path = self.folder(identifier)/filename
        if not path.is_file():
            raise ValueError('Requested result is unavailable for this experiment')
        return path

    async def close(self):
        for identifier, task in list(self.tasks.items()):
            if not task.done():
                if self.metadata(identifier)['state'] == 'ACQUIRING':
                    task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    metadata = self.metadata(identifier)
                    metadata.update(state='CANCELLED', error='Acquisition cancelled on shutdown')
                    await disk_work(self._save, metadata)
