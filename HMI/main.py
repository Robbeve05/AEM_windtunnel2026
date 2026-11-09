import asyncio
import logging
import math
import statistics
import subprocess
import time
from collections import deque
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse

from backend.fan_calibration import FanCharacteristicCalibrator, CalibrationConfig
from backend.esp32 import ESP32Client
from backend.measurements import Measurements
from backend.models import Config, FlowMetricsSetting, GraphAirspeedSetting, MeasurementRequest, Recalculate, Setpoint, PressureFilterSetting, ControlMode, ManualFrequency
from backend.simulator import Simulator
from backend.storage import Storage
from backend.aerodynamics.experiments import AerodynamicExperiments
from backend.aerodynamics.models import ExperimentConfig, AnalysisSettings, CompareRequest
from backend.reynolds import (
    annotate_sample, chord_metres, density_value, finite_number, required_airspeed, viscosity,
)

BASE = Path(__file__).resolve().parent


class Controller:
    def __init__(self, base=BASE):
        self.base = Path(base)
        self.config = Config.model_validate_json((base / 'config/config.json').read_text(encoding='utf-8'))
        self.simulation = self.config.simulation
        self.esp32_mode = not self.simulation
        self.esp32 = (ESP32Client(
            self.config.esp32_host, self.config.esp32_port,
            max_airspeed_ms=self.config.max_airspeed_ms,
            max_frequency_hz=self.config.max_manual_frequency_hz,
        ) if self.esp32_mode else None)
        self.calibration = FanCharacteristicCalibrator(self.esp32, self.base / "data/calibrations", self.config)
        self.esp32_frame = {}
        self._last_pitot_sequence = None
        self._last_taps_sequence = None
        self.tunnel = Simulator(self.config) if self.simulation else None
        self.measurements = Measurements(base / self.config.data_directory, self.config)
        self.storage = Storage(base, self.config, self.measurements)
        self.events = deque(maxlen=80)
        self.sample = {}
        self.reynolds_setpoint = None
        self.running = True
        self.lock = asyncio.Lock()
        self.export_lock = asyncio.Lock()
        self.aerodynamics = AerodynamicExperiments(self.measurements, self._aero_density,
                                                   self._aero_guard, self.export_lock,
                                                   lambda: self.reynolds_setpoint)
        self.speed_window = deque()  # (monotonic_time, velocity)
        self.last_valid_sample_monotonic = None
        self.started_monotonic = time.monotonic()
        if self.simulation:
            self.event('Simulation mode active.')
        elif self.esp32_mode:
            self.event('ESP32 Ethernet mode active. Physical START and RESET required.')

    def _aero_density(self):
        return density_value(self.esp32_frame.get('air_density', self.config.air_density))

    def _aero_guard(self, config):
        self.calibration.require_idle()
        if self.measurements.state in ('RECORDING', 'SAVING'):
            raise ValueError('Stop general recording before aerodynamic acquisition')
        if config.source == 'physical':
            if not self.aerodynamics.physical_adapter.available:
                raise ValueError('Physical pressure adapter is not configured')
            frame = self.esp32.snapshot() if self.esp32 else {}
            if (not self.sensor_fresh() or self.last_valid_sample_monotonic is None
                    or time.monotonic()-self.last_valid_sample_monotonic > self.esp32.status_timeout
                    or frame.get('state') != 'RUNNING' or frame.get('fault') != 'NONE'):
                raise ValueError('Physical acquisition requires fresh, fault-free RUNNING telemetry')

    def event(self, message, level='info'):
        self.events.appendleft(dict(timestamp=datetime.now(timezone.utc).isoformat(), message=message, level=level))

    def _flow_metrics(self, velocity: float, now: float) -> dict:
        if not self.config.flow_metrics_enabled:
            self.speed_window.clear()
            return dict(flow_stability_pct=None, flow_mean_ms=None, flow_std_ms=None, flow_min_ms=None, flow_max_ms=None)
        self.speed_window.append((now, velocity))
        while self.speed_window and self.speed_window[0][0] < now - self.config.flow_statistics_window_s:
            self.speed_window.popleft()
        values = [v for _, v in self.speed_window]
        if not values:
            return dict(flow_stability_pct=None, flow_mean_ms=None, flow_std_ms=None, flow_min_ms=None, flow_max_ms=None)
        mean = statistics.fmean(values)
        std = statistics.pstdev(values) if len(values) > 1 else 0.0
        stability = None if mean < 1.0 else max(0.0, min(100.0, 100.0 - (std / mean * 100.0)))
        return dict(flow_stability_pct=stability, flow_mean_ms=mean, flow_std_ms=std,
                    flow_min_ms=min(values), flow_max_ms=max(values))

    def _hardware_sample(self, raw: dict, now: float) -> dict:
        velocity = max(0.0, float(raw['airspeed_ms']))
        differential = float(raw['pressure_pa'])
        metrics = self._flow_metrics(velocity, now)
        measurement = self.measurements.status()
        return dict(
            timestamp_ms=int((now - self.started_monotonic) * 1000),
            state='MEASURING',
            airspeed_ms=velocity,
            airspeed_kmh=float(raw.get('airspeed_kmh', velocity * 3.6)),
            differential_pressure_pa=differential,
            dynamic_pressure_pa=abs(differential),
            static_pressure_pa=None,
            temperature_c=(float(raw['temperature_c']) if isinstance(raw.get('temperature_c'), (float, int))
                           and not isinstance(raw['temperature_c'], bool)
                           and math.isfinite(raw['temperature_c']) else None),
            fan_output_pct=None,
            angle_of_attack_deg=None,
            pressure_pa=[],
            errors=[],
            **metrics,
        )

    def sensor_fresh(self) -> bool:
        return bool(self.simulation or (self.esp32_mode and self.esp32 is not None and
                    self.esp32.fresh and self.esp32_frame.get('sensor_ok') == 1))

    def reynolds_environment(self):
        """Read existing telemetry/config; no substituted temperature or new density model."""
        environment = dict(valid=False, reason='', temperature_c=None,
                           air_density_kg_m3=None, dynamic_viscosity_pa_s=None)
        try:
            reported_density = (self.esp32_frame.get('air_density', self.config.air_density)
                                if self.esp32_mode else self.config.air_density)
            environment['air_density_kg_m3'] = density_value(reported_density)
            timeout = self.esp32.status_timeout if self.esp32_mode and self.esp32 else 1.0
            if (not self.sensor_fresh() or self.last_valid_sample_monotonic is None or
                    time.monotonic()-self.last_valid_sample_monotonic > timeout):
                raise ValueError('Fresh temperature telemetry unavailable; wait for a new valid sensor sample')
            # Checking the frame as well prevents an old sample masking a newly missing temperature.
            temperature = (self.esp32_frame.get('temperature_c') if self.esp32_mode else
                           self.sample.get('temperature_c'))
            environment['temperature_c'] = finite_number(temperature, 'Measured air temperature')
            environment['dynamic_viscosity_pa_s'] = viscosity(temperature)
            environment['valid'] = True
        except ValueError as exc:
            environment['reason'] = str(exc)
        return environment

    @property
    def setpoint(self):
        return self.tunnel.setpoint if self.simulation else self.esp32.snapshot().get('sp')

    @property
    def tunnel_state(self):
        return self.tunnel.state if self.simulation else self.esp32.snapshot()['state']

    def status(self):
        age = None if self.last_valid_sample_monotonic is None else max(0.0, time.monotonic() - self.last_valid_sample_monotonic)
        if self.simulation:
            system = {'host': 'OK', 'connection': 'SIMULATION', 'pitot': 'OK',
                      'pressure_sensors': 'SIMULATED', 'vfd': 'SIMULATED', 'logger': self.measurements.state, 'storage': 'LOCAL'}
            warnings = ['Simulation mode active']
        elif self.esp32_mode:
            frame = self.esp32.snapshot()
            link = frame.get('ethernet_link')
            ipv4 = frame.get('ipv4_configured')
            heartbeat_received = frame.get('pi_heartbeat_received')
            heartbeat_fresh = frame.get('pi_heartbeat_fresh')
            system = {'host':'OK',
                      'connection':frame.get('connection_status', 'OFFLINE'),
                      'ethernet_link':('UP' if link == 1 else 'DOWN' if link == 0 else 'UNKNOWN'),
                      'ipv4':('CONFIGURED' if ipv4 == 1 else 'NO IP' if ipv4 == 0 else 'UNKNOWN'),
                      'tcp':('CONNECTED' if frame.get('tcp_connected') else 'DISCONNECTED'),
                      'heartbeat':('FRESH' if heartbeat_fresh == 1 else
                                   'TIMEOUT' if heartbeat_fresh == 0 else 'UNKNOWN'),
                      'heartbeat_received':('YES' if heartbeat_received == 1 else
                                            'NO' if heartbeat_received == 0 else 'UNKNOWN'),
                      'pitot':'OK' if self.sensor_fresh() else 'NO DATA',
                      'pressure_sensors':'ONLINE' if frame.get('taps_valid') else 'NOT COMMISSIONED',
                      'vfd':'OK' if frame.get('motor_ok') else 'UNAVAILABLE',
                      'logger':self.measurements.state, 'storage':'LOCAL'}
            connection_status = frame.get('connection_status', 'OFFLINE')
            if not self.esp32.fresh:
                warnings = ['ESP32 STATUS stale or unavailable - machine state UNKNOWN; fan may still be rotating.']
            elif connection_status != 'ONLINE':
                warnings = [f"ESP32 application health: {connection_status}; controller is not ONLINE."]
            else:
                warnings = []
            if frame.get('estop_pressed') is True or frame.get('estop_pressed') == 1:
                warnings.append('EMERGENCY STOP PRESSED - fan may continue to coast.')
            elif frame.get('fault') == 'EMERGENCY_STOP':
                warnings.append('Emergency stop released; physical RESET required. Fan may still rotate.')
            if not frame.get('commissioned'):
                warnings.append('Physical safety and VFD commissioning is incomplete.')
        reported_filter = self.esp32_frame.get('filter_hz') if self.esp32_mode and self.esp32.fresh else None
        pressure_filter = {'configured_cutoff_hz': self.config.filter_cutoff_hz, 'reported_cutoff_hz': reported_filter, 'confirmed': reported_filter is not None and math.isclose(reported_filter, self.config.filter_cutoff_hz, abs_tol=0.06)}
        measurement = self.measurements.status()
        return dict(
            calibration=self.calibration.status(),
            reynolds=dict(environment=self.reynolds_environment(), applied=self.reynolds_setpoint),
            sample=self.sample,
            setpoint=self.setpoint,
            tunnel_state=self.tunnel_state,
            measurement=measurement,
            simulation=self.simulation,
            hardware_mode=not self.simulation,
            pressure_filter=pressure_filter,
            pitot_diagnostics=({k: v for k, v in self.esp32_frame.items() if k.startswith('pitot_') or k == 'sample_seq'}
                               if self.esp32_mode and not self.simulation and self.esp32_frame.get("sim") == 0 else {}),
            pressure_taps=({
                'sequence': self.esp32_frame.get('taps_seq'),
                'age_ms': self.esp32_frame.get('taps_age_ms'),
                'valid_mask': self.esp32_frame.get('taps_valid', 0) if self.esp32.fresh else 0,
                'updated_mask': self.esp32_frame.get('taps_updated', 0),
                'errors': self.esp32_frame.get('taps_errors'),
                'channel_age_ms': [self.esp32_frame.get(f'tap_age{i}_ms') for i in range(16)],
                'values_pa': [self.esp32_frame.get(f'p{i}') if self.esp32.fresh and
                              self.esp32_frame.get('taps_valid', 0) & (1 << i) else None for i in range(16)],
            } if self.esp32_mode else {}),
            pitot_transport_fresh=bool(self.esp32_mode and self.esp32.fresh),
            sensor_fresh=self.sensor_fresh() if not self.simulation else True,
            sample_age_s=age,
            events=list(self.events),
            cp=[],
            system=system,
            warnings=warnings,
            esp32=self.esp32.snapshot() if self.esp32_mode else None,
            device_mode='simulation' if self.simulation else 'esp32_tcp',
            errors=[self.measurements.error] if self.measurements.error else [],
        )

    async def _esp32_status(self, frame: dict):
        now = time.monotonic()
        self.esp32_frame = frame
        self.calibration.publish(frame)
        if frame.get('sensor_ok') != 1:
            return
        # STATUS may repeat or skip 100 Hz Pitot publications.
        # Only record/plot a new measurement when its sequence increments.
        sequence = frame.get('sample_seq')
        if sequence is not None and sequence == self._last_pitot_sequence:
            return
        self._last_pitot_sequence = sequence
        raw = {'airspeed_ms':frame['pv'], 'pressure_pa':frame.get('pressure_pa', 0.0),
               'temperature_c':frame.get('temperature_c')}
        async with self.lock:
            self.last_valid_sample_monotonic = now
            self.sample = self._hardware_sample(raw, now)
            self.sample['fan_output_pct'] = frame.get('out')
            self.sample['state'] = frame['state']
            self.sample['host_timestamp'] = datetime.now(timezone.utc).isoformat()
            if frame.get('mode') == 'MANUAL':
                self.reynolds_setpoint = None
            self.sample.update(annotate_sample(self.sample, self.reynolds_setpoint,
                                               self.reynolds_environment()))
            self.sample['taps_seq'] = frame.get('taps_seq')
            self.sample['taps_age_ms'] = frame.get('taps_age_ms')
            self.sample['taps_valid'] = frame.get('taps_valid', 0)
            tap_seq = frame.get('taps_seq')
            new_taps = tap_seq is not None and tap_seq != self._last_taps_sequence
            self.sample['taps_new'] = new_taps
            self.sample['tap_channel_indices'] = []
            if tap_seq is not None:
                self.sample['pressure_pa'] = [None] * 16
            for index in range(16):
                if (new_taps and frame.get('taps_updated', 0) & (1 << index)
                        and frame.get('taps_valid', 0) & (1 << index)):
                    self.sample['tap_channel_indices'].append(index)
                    self.sample['pressure_pa'][index] = frame.get(f'p{index}')
            self._last_taps_sequence = tap_seq
            try:
                await asyncio.to_thread(self.measurements.append, self.sample.copy(), now)
            except Exception as exc:
                logging.exception('Measurement logger failed')
                await asyncio.to_thread(self.measurements.fail, str(exc))
                self.event('Logger error: ' + str(exc), 'error')

    async def loop(self):
        if self.esp32_mode:
            await self.esp32.run(self._esp32_status, self.event)
            return
        if self.simulation:
            last = time.monotonic()
            while self.running:
                now = time.monotonic()
                async with self.lock:
                    self.sample = self.tunnel.read(now - last)
                    self.sample.update(self._flow_metrics(self.sample['airspeed_ms'], now))
                    self.sample['airspeed_kmh'] = self.sample['airspeed_ms'] * 3.6
                    self.sample['differential_pressure_pa'] = self.sample['dynamic_pressure_pa']
                    self.sample['host_timestamp'] = datetime.now(timezone.utc).isoformat()
                    self.last_valid_sample_monotonic = now
                    self.sample.update(annotate_sample(self.sample, self.reynolds_setpoint,
                                                       self.reynolds_environment()))
                    await asyncio.to_thread(self.measurements.append, self.sample.copy(), now)
                last = now
                await asyncio.sleep(max(0.001, 1 / self.config.sample_rate_hz - (time.monotonic() - now)))
            return



@asynccontextmanager
async def lifespan(app):
    controller = Controller()
    app.state.controller = controller
    task = asyncio.create_task(controller.loop())
    yield
    await controller.calibration.cancel()
    await controller.aerodynamics.close()
    controller.running = False
    if controller.esp32:
        await controller.esp32.close()
    if controller.tunnel:
        controller.tunnel.stop()
    if controller.measurements.state == 'RECORDING':
        await asyncio.to_thread(controller.measurements.stop)
    try:
        await asyncio.wait_for(task, timeout=1.5)
    except (asyncio.TimeoutError, asyncio.CancelledError):
        task.cancel()


app = FastAPI(title='Avans Windtunnel HMI', lifespan=lifespan)



def control():
    return app.state.controller


@app.exception_handler(ValueError)
async def invalid_request(request, exc):
    return JSONResponse(status_code=400, content={'detail': str(exc)})


@app.exception_handler(OSError)
async def storage_error(request, exc):
    logging.exception('Storage operation failed')
    return JSONResponse(status_code=503, content={'detail': 'Storage operation failed: ' + str(exc)})


@app.get('/')
def index():
    return {'application': 'Avans Windtunnel', 'operator_hmi': 'native-pyside6', 'status': '/api/status'}


@app.get('/api/status')
async def status():
    return control().status()


@app.get('/api/config')
async def config():
    return control().config.model_dump()




@app.post('/api/settings/flow-metrics')
async def set_flow_metrics(body: FlowMetricsSetting):
    c = control()
    c.config.flow_metrics_enabled = body.enabled
    c.speed_window.clear()
    config_path = c.base / 'config/config.json'
    config_path.write_text(c.config.model_dump_json(indent=2), encoding='utf-8')
    c.event('5 s flow statistics enabled.' if body.enabled else '5 s flow statistics disabled.')
    return {'enabled': c.config.flow_metrics_enabled}


@app.post('/api/settings/graph-airspeed')
async def set_graph_airspeed(body: GraphAirspeedSetting):
    """Persist display-only chart scale. No ESP32 command or motor setting."""
    c = control()
    async with c.lock:
        old = c.config.graph_max_airspeed_ms
        c.config.graph_max_airspeed_ms = body.graph_max_airspeed_ms
        try:
            (c.base / 'config/config.json').write_text(
                c.config.model_dump_json(indent=2), encoding='utf-8'
            )
        except OSError:
            c.config.graph_max_airspeed_ms = old
            raise
    c.event(f'Graph Y-axis maximum set to {body.graph_max_airspeed_ms:g} m/s')
    return {'graph_max_airspeed_ms': c.config.graph_max_airspeed_ms}


@app.post('/api/settings/pressure-filter')
async def set_pressure_filter(body: PressureFilterSetting):
    c = control()
    c.calibration.require_idle()
    async with c.calibration.lock:
        if c.esp32_mode:
            await c.esp32.command(f'FILTER {body.cutoff_hz:.2f}')
        c.config.filter_cutoff_hz = body.cutoff_hz
        (c.base / 'config/config.json').write_text(c.config.model_dump_json(indent=2), encoding='utf-8')
        c.event(f'Pitot pressure filter setpoint: {body.cutoff_hz:.1f} Hz')
        return {'cutoff_hz':body.cutoff_hz, 'command_sent':c.esp32_mode, 'device_confirmed':False}


@app.post('/api/tunnel/start')
async def start_tunnel():
    c = control()
    if not c.simulation:
        raise ValueError('The fan can only START using the physical Start button.')
    c.tunnel.start()
    c.event('Tunnel start requested')
    return c.status()


@app.post('/api/tunnel/stop')
async def stop_tunnel():
    raise ValueError("STOP is exclusively the physical pushbutton")


def require_ready_esp32(c, required_mode=None):
    """A UI-level operating point may only be applied in physical READY.

    The calibration worker talks directly to ESP32 MANUAL commands while RUNNING
    and remains governed by the existing independent machine safety interlocks.
    """
    if not c.esp32_mode or c.esp32 is None or not c.esp32.fresh:
        raise ValueError('Fresh ESP32 READY telemetry required')
    snapshot = c.esp32.snapshot()
    if snapshot.get('stale') or snapshot.get('state') != 'READY':
        raise ValueError('Physical READY state required before applying a control command')
    if required_mode is not None and snapshot.get('mode') != required_mode:
        raise ValueError(f'Apply {required_mode} mode in READY before sending this command')
    return snapshot


@app.post('/api/tunnel/mode')
async def select_mode(body: ControlMode):
    c=control()
    c.calibration.require_idle()
    async with c.calibration.lock:
        require_ready_esp32(c)
        await c.esp32.command('MODE '+body.mode)
        if body.mode == 'MANUAL':
            c.reynolds_setpoint = None
        return c.status()

@app.post('/api/tunnel/frequency')
async def select_frequency(body: ManualFrequency):
    c=control()
    c.calibration.require_idle()
    async with c.calibration.lock:
        if not c.config.min_manual_frequency_hz <= body.hz <= c.config.max_manual_frequency_hz:
            raise ValueError(
                f"Manual frequency must be between {c.config.min_manual_frequency_hz:g} "
                f"and {c.config.max_manual_frequency_hz:g} Hz"
            )
        require_ready_esp32(c, 'MANUAL')
        await c.esp32.command(f'FREQ {body.hz:.3f}')
        return c.status()

@app.post('/api/tunnel/setpoint')
async def setpoint(body: Setpoint):
    c = control()
    c.calibration.require_idle()
    async with c.calibration.lock:
        if body.airspeed_ms > c.config.max_airspeed_ms:
            raise ValueError(f'Setpoint exceeds configured {c.config.max_airspeed_ms:g} m/s maximum')
        target = body.airspeed_ms
        context = None
        if body.requested_reynolds is not None:
            environment = c.reynolds_environment()
            if not environment['valid']:
                raise ValueError(environment['reason'])
            target = required_airspeed(body.requested_reynolds, body.chord_mm,
                environment['temperature_c'], environment['air_density_kg_m3'], c.config.max_airspeed_ms)
            if not math.isclose(body.airspeed_ms, target, rel_tol=0.001, abs_tol=0.0005):
                raise ValueError('Temperature or density changed; review the recalculated Reynolds airspeed and apply again')
            wire_target = float(f'{target:.3f}') if c.esp32_mode else target
            if wire_target <= 0:
                raise ValueError('Calculated Reynolds airspeed is below the existing 0.001 m/s command resolution')
            context = dict(requested_reynolds=body.requested_reynolds, chord_mm=body.chord_mm,
                chord_m=chord_metres(body.chord_mm), temperature_c=environment['temperature_c'],
                air_density_kg_m3=environment['air_density_kg_m3'],
                dynamic_viscosity_pa_s=environment['dynamic_viscosity_pa_s'],
                applied_airspeed_setpoint_ms=wire_target, calculated_airspeed_setpoint_ms=target)
        if c.esp32_mode:
            require_ready_esp32(c, 'AUTO')
            await c.esp32.command(f'SETPOINT {target:.3f}')
            c.reynolds_setpoint = context
            c.event(f'ESP32 airspeed setpoint requested: {target:g} m/s' +
                    (f' (Re {body.requested_reynolds:g}, chord {body.chord_mm:g} mm)' if context else ''))
            return c.status()
        if not c.simulation:
            raise ValueError('Airspeed setpoint control is unavailable in this mode')
        c.tunnel.setpoint = target
        c.reynolds_setpoint = context
        c.event(f'Setpoint changed to {target:g} m/s')
        return c.status()


@app.post('/api/measurement/start')
async def start_measurement(body: MeasurementRequest):
    c = control()
    async with c.lock:
        c.aerodynamics.require_idle()
        if not c.simulation and not c.sensor_fresh():
            raise ValueError('No fresh pitot data. Check the acquisition connection before recording.')
        result = await asyncio.to_thread(
            c.measurements.start, body.name.strip(), body.recording_rate_hz, c.reynolds_setpoint
        )
        c.event('Recording started: ' + body.name)
        return result


@app.post('/api/measurement/stop')
async def stop_measurement():
    c = control()
    async with c.lock:
        result = await asyncio.to_thread(c.measurements.stop)
        c.event('Measurement saved: ' + result['name'])
        return result


@app.get('/api/measurements')
async def measurements():
    return await asyncio.to_thread(control().measurements.list)


@app.get('/api/aerodynamics/experiments')
async def aerodynamic_experiments(airfoil: str = '', name: str = '', date: str = '',
                                 alpha_min: float = -90, alpha_max: float = 90):
    runs = await asyncio.to_thread(control().aerodynamics.list)
    return [run for run in runs if airfoil.lower() in run['configuration']['airfoil'].lower()
            and name.lower() in run['name'].lower() and run['timestamp'].startswith(date)
            and alpha_min <= run['configuration']['alpha_deg'] <= alpha_max]


@app.post('/api/aerodynamics/experiments')
async def create_aerodynamic_experiment(body: ExperimentConfig):
    return await control().aerodynamics.create(body)


@app.get('/api/aerodynamics/status')
async def aerodynamic_status():
    manager = control().aerodynamics
    return dict(physical_available=bool(manager.physical_adapter.available),
                active=[key for key, task in manager.tasks.items() if not task.done()])


@app.post('/api/aerodynamics/compare')
async def aerodynamic_compare(body: CompareRequest):
    return await asyncio.to_thread(control().aerodynamics.comparison, body.identifiers)


@app.get('/api/aerodynamics/experiments/{identifier}')
async def aerodynamic_experiment(identifier: str):
    return await asyncio.to_thread(control().aerodynamics.detail, identifier)


@app.put('/api/aerodynamics/experiments/{identifier}')
async def update_aerodynamic_experiment(identifier: str, body: ExperimentConfig):
    return await control().aerodynamics.update(identifier, body)


@app.post('/api/aerodynamics/experiments/{identifier}/start')
async def start_aerodynamic_acquisition(identifier: str):
    async with control().lock:
        return await control().aerodynamics.start(identifier)


@app.post('/api/aerodynamics/experiments/{identifier}/stop')
async def stop_aerodynamic_acquisition(identifier: str):
    return await control().aerodynamics.stop(identifier)


@app.post('/api/aerodynamics/experiments/{identifier}/analyze')
async def analyze_aerodynamic_experiment(identifier: str, body: AnalysisSettings):
    return await control().aerodynamics.run_analysis(identifier, body)


@app.get('/api/aerodynamics/experiments/{identifier}/cp')
async def aerodynamic_cp(identifier: str):
    detail = await asyncio.to_thread(control().aerodynamics.detail, identifier)
    return dict(status=detail['metadata']['analysis_status'],
                points=detail['analysis']['cp'] if detail['analysis'] else [])


@app.get('/api/aerodynamics/experiments/{identifier}/coefficients')
async def aerodynamic_coefficients(identifier: str):
    detail = await asyncio.to_thread(control().aerodynamics.detail, identifier)
    return dict(status=detail['metadata']['analysis_status'],
                coefficients=detail['analysis']['coefficients'] if detail['analysis'] else {})


@app.get('/api/aerodynamics/experiments/{identifier}/files/{filename}')
async def aerodynamic_file(identifier: str, filename: str):
    path = await asyncio.to_thread(control().aerodynamics.file, identifier, filename)
    return FileResponse(path, filename=f'{identifier}_{filename}')


@app.get('/api/measurements/{identifier}')
async def measurement(identifier: str):
    return await asyncio.to_thread(control().measurements.detail, identifier)


@app.get('/api/measurements/{identifier}/files/{filename}')
async def measurement_file(identifier: str, filename: str):
    if filename not in {'raw_data.csv', 'processed_data.csv', 'experiment.json'}:
        raise ValueError('Measurement file is not available')
    folder = control().measurements.folder_for(identifier)
    path = folder / filename
    if not path.is_file():
        raise ValueError('Measurement file is not available')
    media_type = 'text/csv' if filename.endswith('.csv') else 'application/json'
    return FileResponse(path, media_type=media_type, filename=f'{identifier}_{filename}')


@app.post('/api/measurements/{identifier}/cp')
async def recalculate(identifier: str, body: Recalculate):
    return await asyncio.to_thread(control().measurements.recalculate, identifier, body.air_density)


@app.get('/api/storage')
async def storage():
    return await asyncio.to_thread(control().storage.status)


@app.post('/api/storage/export-all')
async def export_all():
    c = control()
    async with c.export_lock:
        if not c.storage.connected():
            raise ValueError('No USB storage connected')
        runs = await asyncio.to_thread(c.measurements.list)
        for run in runs:
            if run['state'] == 'COMPLETED' and not run['exported']:
                await asyncio.to_thread(c.storage.export, run['id'])
        return c.storage.progress


@app.post('/api/storage/export/{identifier}')
async def export(identifier: str):
    c = control()
    async with c.export_lock:
        return await asyncio.to_thread(c.storage.export, identifier)


@app.post('/api/storage/eject')
async def eject():
    c = control()
    async with c.export_lock:
        return await asyncio.to_thread(c.storage.eject)


@app.post('/api/storage/reconnect')
async def reconnect():
    return control().storage.reconnect()


@app.post('/api/system/exit-hmi')
async def exit_hmi():
    control().event('HMI exit requested from operator interface', 'warning')
    subprocess.Popen(['/bin/sh', '-c', 'sleep 0.4; pkill -TERM chromium >/dev/null 2>&1 || true'], start_new_session=True)
    return {'ok': True, 'message': 'Closing HMI'}


@app.websocket('/ws/live')
async def live(websocket: WebSocket):
    await websocket.accept()
    try:
        while True:
            await websocket.send_json(control().status())
            await asyncio.sleep(0.1)
    except (WebSocketDisconnect, RuntimeError, OSError):
        pass


@app.post('/api/calibration/fan/prepare')
async def prepare_fan(body: dict | None = None):
    c = control()
    c.aerodynamics.require_idle()
    payload = {**c.config.calibration_defaults.model_dump(), **(body or {})}
    config = CalibrationConfig.model_validate(payload)
    if (config.start_hz < c.config.min_manual_frequency_hz or
            config.end_hz > c.config.max_manual_frequency_hz):
        raise ValueError(
            f'Calibration frequencies must lie within '
            f'{c.config.min_manual_frequency_hz:g}–{c.config.max_manual_frequency_hz:g} Hz'
        )
    return await c.calibration.prepare(config)

@app.get('/api/calibration/fan/status')
async def fan_status():
    return control().calibration.status()

@app.post('/api/calibration/fan/cancel')
async def cancel_fan():
    return await control().calibration.cancel()

@app.get('/api/calibration/fan/runs')
async def fan_runs():
    return await asyncio.to_thread(control().calibration.runs)

@app.get('/api/calibration/fan/runs/{identifier}')
async def fan_run(identifier: str):
    import json
    return json.loads(control().calibration.file(identifier).read_text(encoding='utf-8'))

@app.get('/api/calibration/fan/runs/{identifier}/files/{filename}')
async def fan_file(identifier: str, filename: str):
    return FileResponse(control().calibration.file(identifier, filename), filename=filename)


@app.get('/api/calibration/fan/runs/{identifier}/dynamics')
async def fan_dynamics(identifier: str, lambda_s: float = 1.0):
    """Read offline fits and simulate candidate ISA tuning. Never sends a command."""
    import json
    from backend.fan_dynamics import simc_parameters, simulate_candidate
    if not math.isfinite(lambda_s) or not 0.05 <= lambda_s <= 60:
        raise ValueError('Lambda must lie within 0.05–60 s')
    result = json.loads(control().calibration.file(identifier, 'dynamics.json').read_text(encoding='utf-8'))
    def tune():
        for item in result['transitions']:
            if item.get('tau_s') and item.get('K_ms_per_hz', 0) > 0 and item.get('L_s') is not None:
                args = (item['K_ms_per_hz'], item['tau_s'], item['L_s'])
                item['candidate_pi'] = simc_parameters(*args, lambda_s)
                item['simulation'] = simulate_candidate(*args, item['candidate_pi'])
            elif item.get('tau_s'):
                item.setdefault('warnings', []).append('Legacy fit has no delay estimate; refit raw data before tuning.')
        return result
    return await asyncio.to_thread(tune)

@app.post('/api/calibration/fan/runs/{identifier}/deploy')
async def deploy_fan(identifier: str):
    return await control().calibration.deploy(identifier)

@app.post('/api/calibration/fan/reset')
async def reset_fan():
    return await control().calibration.reset()
