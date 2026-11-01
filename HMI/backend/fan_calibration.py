"""MANUAL-mode fan measurements; this module never controls motor trajectories."""
import asyncio
import csv
import json
import math
import re
import statistics
import struct
import time
import uuid
import zlib
from collections import deque
from datetime import datetime, timezone
from pathlib import Path
from pydantic import BaseModel, Field, model_validator

class CalibrationConfig(BaseModel):
    start_hz: float = Field(default=10, ge=.001, le=50, allow_inf_nan=False)
    end_hz: float = Field(default=50, gt=0, le=50, allow_inf_nan=False)
    step_hz: float = Field(default=5, ge=.001, le=50, allow_inf_nan=False)
    average_s: float = Field(default=5, ge=5, le=60, allow_inf_nan=False)
    window_s: float = Field(default=3, ge=3, le=30, allow_inf_nan=False)
    min_samples: int = Field(default=80, ge=80, le=1200)
    applied_tolerance: float = Field(default=.15, gt=0, le=.5, allow_inf_nan=False)
    actual_tolerance: float = Field(default=.20, gt=0, le=.5, allow_inf_nan=False)
    std_ms: float = Field(default=.15, gt=0, le=1, allow_inf_nan=False)
    relative_std: float = Field(default=.01, gt=0, le=.05, allow_inf_nan=False)
    trend_ms2: float = Field(default=.05, gt=0, le=.2, allow_inf_nan=False)
    stabilization_timeout_s: float = Field(default=30, ge=10, le=120, allow_inf_nan=False)
    ramp_timeout_s: float = Field(default=180, ge=30, le=600, allow_inf_nan=False)

    @model_validator(mode='after')
    def validate_sequence(self):
        n=(self.end_hz-self.start_hz)/self.step_hz
        if n<1 or n>31 or not math.isclose(n,round(n),abs_tol=1e-6):
            raise ValueError('Choose 2–32 evenly spaced frequencies including both endpoints')
        return self

    def frequencies(self):
        return [round(self.start_hz+i*self.step_hz,3) for i in range(round((self.end_hz-self.start_hz)/self.step_hz)+1)]

def lookup_points(points):
    result=[(round(p['mean_pv']*1000),round(p['target_hz']*1000)) for p in points]
    if not 2<=len(result)<=32: raise ValueError('Invalid point count')
    for i,(v,f) in enumerate(result):
        if not 0<v<=100000 or not 0<f<=50000: raise ValueError('Invalid lookup units')
        if i and (v<=result[i-1][0] or f<=result[i-1][1]): raise ValueError('Characteristic must be strictly increasing')
    return result

def crc32(points):
    return zlib.crc32(struct.pack('<HH',1,len(points))+b''.join(struct.pack('<II',*p) for p in points)) & 0xffffffff

class FanCharacteristicCalibrator:
    def __init__(self, client, directory, settings):
        self.client=client; self.directory=Path(directory); self.settings=settings
        self.queue=asyncio.Queue(maxsize=128); self.lock=asyncio.Lock(); self.task=None
        self.state='IDLE'; self.message=''; self.points=[]; self.target=0; self.index=0
        self.config=CalibrationConfig(); self.run_id=None; self.progress=0; self.stability=None
        self.last_sequence=None; self.last_unique=0; self.raw=[]; self.metadata={}

    @property
    def active(self):
        return self.task is not None and not self.task.done() or self.lock.locked()

    def require_idle(self):
        if self.active: raise ValueError('Calibration owns frequency, mode and filter settings')

    def publish(self, frame):
        if self.task is None or self.task.done(): return
        if self.queue.full(): self.queue.get_nowait()
        self.queue.put_nowait((time.monotonic(),dict(frame)))

    def status(self):
        frame=self.client.snapshot() if self.client else {}
        return dict(state=self.state,active=self.active,message=self.message,index=self.index,
                    total=len(self.config.frequencies()),target_hz=self.target,progress=self.progress,
                    stability=self.stability,points=self.points,run_id=self.run_id,
                    esp32=frame)

    def safe_ready(self):
        s=self.client.snapshot() if self.client else {}
        if not self.client or not self.client.fresh or s.get('state')!='READY' or not s.get('motor_ok') or abs(s.get('actual_hz',999))>.2:
            raise ValueError('Fresh ESP32 READY and stopped motor feedback required')
        return s

    async def prepare(self, config):
        self.require_idle()
        async with self.lock:
            s=self.safe_ready()
            if s.get('sensor_ok')!=1: raise ValueError('Valid Pitot data required')
            if not math.isclose(s.get('filter_hz',0),self.settings.filter_cutoff_hz,abs_tol=.06):
                raise ValueError('Confirm the configured Pitot filter before calibration')
            if not math.isclose(s.get('air_density',0),self.settings.air_density,abs_tol=.0001):
                raise ValueError('ESP32 air density must match the configured calibration density')
            self.config=config; self.points=[]; self.raw=[]; self.last_sequence=None; self.index=0
            self.progress=0; self.message='Preparing MANUAL; wait for ARMED before physical START'
            self.run_id=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'-'+uuid.uuid4().hex[:8]
            self.metadata=dict(id=self.run_id,timestamp=datetime.now(timezone.utc).isoformat(),
                configuration=config.model_dump(),filter_hz=s['filter_hz'],air_density=self.settings.air_density,
                software_version='fan-calibration-v1',deployment_status='NOT_DEPLOYED',version=1)
            self.state='PREPARING'
            while not self.queue.empty(): self.queue.get_nowait()
            self.task=asyncio.create_task(self._run())
        return self.status()

    def _operating(self,s):
        if s.get('mode')!='MANUAL' or s.get('state')!='RUNNING': raise ValueError('MANUAL RUNNING was lost')
        if s.get('sensor_ok')!=1 or s.get('motor_ok')!=1 or s.get('estop_pressed')!=0 or s.get('reset_required')!=0 or s.get('drive_fault',0):
            raise ValueError('Sensor, motor or safety validity lost')
        if not math.isclose(s.get('filter_hz',0),self.metadata['filter_hz'],abs_tol=.06): raise ValueError('Pitot filter changed')
        if not math.isclose(s.get('air_density',0),self.metadata['air_density'],abs_tol=.0001): raise ValueError('Air density changed')

    async def _next(self, running=True):
        while True:
            now,s=await asyncio.wait_for(self.queue.get(),1)
            if time.monotonic()-now>.5 or not self.client.fresh: raise ValueError('Stale telemetry')
            if running: self._operating(s)
            elif s.get('state') in ('FAULT','STOP','WAIT','INIT'): raise ValueError('Physical STOP or safety state interrupted calibration')
            seq=s.get('sample_seq')
            unique=isinstance(seq,int) and (self.last_sequence is None or 0<((seq-self.last_sequence)&0xffffffff)<0x80000000)
            if unique:
                self.last_sequence=seq;self.last_unique=now
            elif now-self.last_unique>1: raise ValueError('No unique Pitot samples')
            valid=s.get('sensor_ok')==1 and all(isinstance(s.get(k),(float,int)) and math.isfinite(s[k]) for k in ('pv','pressure_pa','temperature_c','applied_hz','actual_hz'))
            if unique:
                self.raw.append(dict(timestamp=datetime.now(timezone.utc).isoformat(),phase=self.state,target_hz=self.target,
                    **{k:s.get(k) for k in ('applied_hz','actual_hz','pv','pressure_pa','temperature_c','sample_seq','state')},valid=valid))
            if not valid: raise ValueError('Missing valid Pitot data')
            if unique: return now,s

    def _tracks(self,s):
        return abs(s['applied_hz']-self.target)<=self.config.applied_tolerance and abs(s['actual_hz']-self.target)<=self.config.actual_tolerance

    def _stable(self, samples):
        if len(samples)<2 or samples[-1][0]-samples[0][0]<self.config.window_s-.1: return False
        ts=[t-samples[0][0] for t,s in samples];vs=[s['pv'] for t,s in samples]
        mean=statistics.fmean(vs);std=statistics.pstdev(vs);mt=statistics.fmean(ts)
        denominator=sum((t-mt)**2 for t in ts)
        trend=sum((t-mt)*(v-mean) for t,v in zip(ts,vs))/denominator if denominator else math.inf
        self.stability=dict(mean=mean,std=std,trend=trend)
        return std<=max(self.config.std_ms,self.config.relative_std*mean) and abs(trend)<=self.config.trend_ms2

    async def _run(self):
        try:
            await self.client.command('MODE MANUAL')
            self.last_unique=time.monotonic()
            # A new READY/MANUAL frame after the mode command is mandatory.
            deadline=time.monotonic()+5
            while True:
                t,s=await self._next(False)
                if s['state']!='READY': raise ValueError('START occurred before preparation completed')
                if s.get('mode')=='MANUAL': break
                if t>deadline: raise ValueError('MANUAL confirmation timed out')
            await self.client.command('FREQ 0')
            if self.client.snapshot().get('state')!='READY': raise ValueError('START occurred before ARMED')
            self.state='ARMED'; self.message='Press physical START'
            deadline=time.monotonic()+600
            while True:
                t,s=await self._next(False)
                if t>deadline: raise ValueError('Physical START timeout')
                if s['state']=='RUNNING': self._operating(s); break
            for self.index,self.target in enumerate(self.config.frequencies(),1):
                self.state='RAMPING';self.message='Waiting for actual V20 frequency';self.progress=0
                await self.client.command(f'FREQ {self.target:.3f}')
                deadline=time.monotonic()+self.config.ramp_timeout_s
                while True:
                    t,s=await self._next()
                    if self._tracks(s): break
                    if t>deadline: raise ValueError('Frequency trajectory timeout')
                self.state='STABILIZING';deadline=t+self.config.stabilization_timeout_s;point_deadline=t+600;window=deque();measure=[]
                while True:
                    t,s=await self._next()
                    if t>deadline or t>point_deadline: raise ValueError('Airspeed stabilization/averaging timeout')
                    if not self._tracks(s): window.clear();measure.clear();self.state='STABILIZING';continue
                    window.append((t,s))
                    while window and t-window[0][0]>self.config.window_s+.05: window.popleft()
                    stable=self._stable(window)
                    if not stable:
                        if measure: deadline=t+self.config.stabilization_timeout_s
                        measure.clear();self.state='STABILIZING';self.progress=0;continue
                    if not measure:
                        self.state='MEASURING';deadline=t+self.config.average_s+5
                    measure.append((t,s));elapsed=t-measure[0][0]
                    self.progress=min(100,100*elapsed/self.config.average_s)
                    if elapsed>=self.config.average_s and len(measure)>=self.config.min_samples: break
                values=[s['pv'] for t,s in measure]
                point=dict(target_hz=self.target,mean_pv=statistics.fmean(values),std_pv=statistics.pstdev(values),
                           min_pv=min(values),max_pv=max(values),samples=len(values))
                for key in ('applied_hz','actual_hz','pressure_pa','temperature_c'):
                    point['mean_'+key]=statistics.fmean(s[key] for t,s in measure)
                self.points.append(point);self.state='NEXT_POINT'
            self.state='RAMP_DOWN';self.target=0
            await self.client.command('FREQ 0');deadline=time.monotonic()+self.config.ramp_timeout_s
            while True:
                t,s=await self._next()
                if self._tracks(s): break
                if t>deadline: raise ValueError('Ramp-down timeout')
            self.state='AWAITING_STOP';self.message='Calibration complete — press physical STOP';deadline=time.monotonic()+600
            while True:
                t,s=await asyncio.wait_for(self.queue.get(),1)
                if not self.client.fresh or time.monotonic()-t>.5: raise ValueError('Stale telemetry awaiting STOP')
                if t>deadline: raise ValueError('Physical STOP timeout')
                if s.get('state')=='FAULT' or s.get('estop_pressed'): raise ValueError('Fault awaiting STOP')
                if s.get('state')=='READY' and abs(s.get('actual_hz',999))<=.2: break
            lut=lookup_points(self.points);self.metadata.update(crc32=crc32(lut),validation='VALID')
            self.state='COMPLETED';self.message='Review results, then Save & Activate'
        except asyncio.CancelledError:
            self.state='ABORTED';self.message='Calibration cancelled; use physical STOP'
            await self._zero()
        except Exception as ex:
            self.state='ERROR';self.message=str(ex)
            await self._zero()
        finally:
            self.metadata.update(completion_status=self.state,validation=self.metadata.get('validation','INVALID'),
                                 valid_points=len(self.points),error=self.message)
            try: await asyncio.to_thread(self._save)
            except Exception as ex: self.state='ERROR';self.message='Saving calibration failed: '+str(ex)

    async def _zero(self):
        s=self.client.snapshot()
        if self.client.fresh and s.get('mode')=='MANUAL' and s.get('state') in ('READY','RUNNING') and not s.get('estop_pressed'):
            try: await self.client.command('FREQ 0')
            except (ValueError,OSError,asyncio.TimeoutError): pass

    async def cancel(self):
        if self.task and not self.task.done(): self.task.cancel();await self.task
        return self.status()

    def _save(self):
        folder=self.directory/self.run_id;folder.mkdir(parents=True,exist_ok=False)
        (folder/'calibration.json').write_text(json.dumps(dict(self.metadata,completion_status='WRITING'),indent=2),encoding='utf-8')
        for name,rows,fields in [('raw_samples.csv',self.raw,['timestamp','phase','target_hz','applied_hz','actual_hz','pv','pressure_pa','temperature_c','sample_seq','state','valid']),
                                 ('characteristic.csv',self.points,list(self.points[0]) if self.points else ['target_hz','mean_pv']),
                                 ('inverse_lookup.csv',[dict(velocity_mmps=v,frequency_mhz=f) for v,f in lookup_points(self.points)] if self.state=='COMPLETED' else [],['velocity_mmps','frequency_mhz'])]:
            with (folder/name).open('w',newline='',encoding='utf-8') as f:
                w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
        (folder/'calibration.json').write_text(json.dumps(self.metadata,indent=2),encoding='utf-8')

    def file(self,identifier,name='calibration.json'):
        if not re.fullmatch(r'[0-9]{8}T[0-9]{6}-[0-9a-f]{8}',identifier) or name not in ('calibration.json','raw_samples.csv','characteristic.csv','inverse_lookup.csv'):
            raise ValueError('Invalid calibration file')
        p=self.directory/identifier/name
        if not p.is_file(): raise ValueError('Calibration file not found')
        return p

    def runs(self):
        return [json.loads(p.read_text(encoding='utf-8')) for p in sorted(self.directory.glob('*/calibration.json'),reverse=True)]

    async def _verify(self,crc,valid):
        await self.client.command('CAL_INFO')
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            s=self.safe_ready()
            if s.get('cal_valid')==valid and s.get('cal_active')==valid and s.get('cal_source')==('CALIBRATED' if valid else 'DEFAULT') and s.get('cal_crc')==crc: return
            await asyncio.sleep(.05)
        raise ValueError('ESP32 activation telemetry was not verified')

    async def deploy(self,identifier):
        self.require_idle()
        async with self.lock:
            self.safe_ready();p=self.file(identifier);meta=json.loads(p.read_text(encoding='utf-8'))
            if meta.get('completion_status')!='COMPLETED' or meta.get('version')!=1: raise ValueError('Completed version 1 run required')
            with self.file(identifier,'characteristic.csv').open(encoding='utf-8') as f:
                lut=lookup_points([{k:float(v) for k,v in row.items()} for row in csv.DictReader(f)])
            crc=crc32(lut)
            if crc!=meta.get('crc32'): raise ValueError('Stored CRC mismatch')
            try:
                await self.client.command(f'CAL_BEGIN 1 {len(lut)} {crc}')
                for i,(v,hz) in enumerate(lut):
                    self.safe_ready();await self.client.command(f'CAL_POINT {i} {v} {hz}')
                self.safe_ready();await self.client.command('CAL_COMMIT');await self._verify(crc,1)
            except Exception:
                if self.client.fresh:
                    try: await self.client.command('CAL_ABORT')
                    except Exception: pass
                raise
            meta.update(deployment_status='VERIFIED',deployed_at=datetime.now(timezone.utc).isoformat())
            p.write_text(json.dumps(meta,indent=2),encoding='utf-8');return meta

    async def reset(self):
        self.require_idle()
        async with self.lock:
            self.safe_ready();await self.client.command('CAL_RESET');await self._verify(0,0)
        return self.status()
