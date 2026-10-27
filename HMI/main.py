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
from fastapi.staticfiles import StaticFiles

from backend.esp32 import ESP32Client
from backend.measurements import Measurements
from backend.models import Config, FlowMetricsSetting, MeasurementRequest, Recalculate, Setpoint, PressureFilterSetting
from backend.simulator import Simulator
from backend.storage import Storage

BASE = Path(__file__).resolve().parent


class Controller:
    def __init__(self, base=BASE):
        self.base = Path(base)
        self.config = Config.model_validate_json((base / 'config/config.json').read_text(encoding='utf-8'))
        self.simulation = self.config.simulation
        self.esp32_mode = not self.simulation
        self.esp32 = ESP32Client(self.config.esp32_host, self.config.esp32_port) if self.esp32_mode else None
        self.esp32_frame = {}
        self.tunnel = Simulator(self.config) if self.simulation else None
        self.measurements = Measurements(base / self.config.data_directory, self.config)
        self.storage = Storage(base, self.config, self.measurements)
        self.events = deque(maxlen=80)
        self.sample = {}
        self.running = True
        self.lock = asyncio.Lock()
        self.export_lock = asyncio.Lock()
        self.speed_window = deque()  # (monotonic_time, velocity)
        self.last_valid_sample_monotonic = None
        self.started_monotonic = time.monotonic()
        if self.simulation:
            self.event('Simulation mode active.')
        elif self.esp32_mode:
            self.event('ESP32 Ethernet mode active. Physical START and RESET required.')

    def event(self, message, level='info'):
        self.events.appendleft(dict(timestamp=datetime.now(timezone.utc).isoformat(), message=message, level=level))

    def _flow_metrics(self, velocity: float, now: float) -> dict:
        if not self.config.flow_metrics_enabled:
            self.speed_window.clear()
            return dict(flow_stability_pct=None, flow_mean_ms=None, flow_std_ms=None, flow_min_ms=None, flow_max_ms=None)
        self.speed_window.append((now, velocity))
        while self.speed_window and self.speed_window[0][0] < now - 5.0:
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
            temperature_c=float(raw['temperature_c']),
            fan_output_pct=None,
            angle_of_attack_deg=None,
            pressure_pa=[],
            errors=[],
            **metrics,
        )

    def sensor_fresh(self) -> bool:
        return (self.esp32.fresh and bool(self.esp32_frame.get('sensor_ok'))) if self.esp32_mode else True

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
            system = {'host':'OK', 'connection':'ESP32 ETHERNET',
                      'pitot':'OK' if self.sensor_fresh() else 'NO DATA',
                      'pressure_sensors':'ONLINE' if frame.get('taps_valid') else 'NOT COMMISSIONED',
                      'vfd':'OK' if frame.get('motor_ok') else 'UNAVAILABLE',
                      'logger':self.measurements.state, 'storage':'LOCAL'}
            warnings = [] if self.esp32.fresh else ['ESP32 status UNKNOWN - fan may still be rotating.']
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
            sample=self.sample,
            setpoint=self.setpoint,
            tunnel_state=self.tunnel_state,
            measurement=measurement,
            simulation=self.simulation,
            hardware_mode=not self.simulation,
            pressure_filter=pressure_filter,
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
        if frame.get('sensor_ok') != 1:
            return
        raw = {'airspeed_ms':frame['pv'], 'pressure_pa':frame.get('pressure_pa', 0.0),
               'temperature_c':frame.get('temperature_c', 0.0)}
        async with self.lock:
            self.last_valid_sample_monotonic = now
            self.sample = self._hardware_sample(raw, now)
            self.sample['fan_output_pct'] = frame.get('out')
            self.sample['state'] = frame['state']
            self.sample['host_timestamp'] = datetime.now(timezone.utc).isoformat()
            for index in range(16):
                if frame.get('taps_valid', 0) & (1 << index):
                    self.sample['pressure_pa'].append(frame.get(f'p{index}'))
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
app.mount('/static', StaticFiles(directory=BASE / 'static'), name='static')


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
    return FileResponse(BASE / 'static/index.html')


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


@app.post('/api/settings/pressure-filter')
async def set_pressure_filter(body: PressureFilterSetting):
    c = control()
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
    c = control()
    if c.esp32_mode:
        await c.esp32.command('STOP')
        c.event('Operational OFF1 STOP requested from HMI.', 'warning')
        return c.status()
    if not c.simulation:
        raise ValueError('Fan/VFD control is not available in this mode')
    c.tunnel.stop()
    c.event('Software stop requested.', 'warning')
    return c.status()


@app.post('/api/tunnel/setpoint')
async def setpoint(body: Setpoint):
    c = control()
    if body.airspeed_ms > c.config.max_airspeed_ms:
        raise ValueError('Setpoint exceeds configured maximum wind speed')
    if c.esp32_mode:
        await c.esp32.command(f'SETPOINT {body.airspeed_ms:.3f}')
        c.event(f'ESP32 airspeed setpoint requested: {body.airspeed_ms:g} m/s')
        return c.status()
    if not c.simulation:
        raise ValueError('Airspeed setpoint control is unavailable in this mode')
    if body.airspeed_ms > c.config.max_airspeed_ms:
        raise ValueError('Setpoint exceeds configured maximum wind speed')
    c.tunnel.setpoint = body.airspeed_ms
    c.event(f'Setpoint changed to {body.airspeed_ms:g} m/s')
    return c.status()


@app.post('/api/measurement/start')
async def start_measurement(body: MeasurementRequest):
    c = control()
    async with c.lock:
        if not c.simulation and not c.sensor_fresh():
            raise ValueError('No fresh pitot data. Check the acquisition connection before recording.')
        result = await asyncio.to_thread(
            c.measurements.start, body.name.strip(), body.recording_rate_hz
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
    raise ValueError('Cp calculation is in development until the pressure-tap system is connected')


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
