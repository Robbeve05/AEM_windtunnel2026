import asyncio
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch
from backend import fan_calibration as fc
from backend.models import Config

class FakeESP:
    def __init__(self):
        self.fresh=True;self.commands=[]
        self.frame=dict(state='READY',mode='AUTO',motor_ok=1,sensor_ok=1,actual_hz=0.,applied_hz=0.,pv=0.,pressure_pa=0.,temperature_c=20.,sample_seq=0,filter_hz=1.,air_density=1.225,estop_pressed=0,reset_required=0)
    def snapshot(self):return dict(self.frame,connected=self.fresh,stale=not self.fresh)
    async def command(self,c):
        self.commands.append(c)
        if c=='MODE MANUAL':self.frame['mode']='MANUAL'
        if c.startswith('FREQ '):
            f=float(c.split()[1]);self.frame.update(actual_hz=f,applied_hz=f,pv=f*.7,pressure_pa=f*f*.3)
        if c.startswith('CAL_BEGIN '):self.crc=int(c.split()[3])
        if c=='CAL_COMMIT':self.frame.update(cal_valid=1,cal_active=1,cal_source='CALIBRATED',cal_crc=self.crc)
        if c=='CAL_RESET':self.frame.update(cal_valid=0,cal_active=0,cal_source='DEFAULT',cal_crc=0)
        return 'OK TEST'

class DataTests(unittest.TestCase):
    def test_sequence_crc_validation(self):
        self.assertEqual(fc.CalibrationConfig().frequencies(),list(range(10,51,5)))
        self.assertEqual(fc.crc32([(10000,10000),(20000,25000)]),364007285)
        for velocities in ([1],[1,1],[2,1]):
            with self.assertRaises(ValueError):fc.lookup_points([dict(mean_pv=v,target_hz=10+i*5) for i,v in enumerate(velocities)])
        for kwargs in ({'step_hz':0},{'end_hz':55},{'average_s':4},{'end_hz':11},{'start_hz':float('nan')}):
            with self.assertRaises(ValueError):fc.CalibrationConfig(**kwargs)

class SequenceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.esp=FakeESP();self.now=100.
        self.clock=patch.object(fc,'time',types.SimpleNamespace(monotonic=lambda:self.now));self.clock.start()
        self.cal=fc.FanCharacteristicCalibrator(self.esp,Path(self.tmp.name),Config())
    async def asyncTearDown(self):
        await self.cal.cancel();self.clock.stop();self.tmp.cleanup()
    async def tick(self,unique=True):
        self.now+=.05
        if unique:self.esp.frame['sample_seq']+=1
        self.cal.publish(self.esp.frame)
        for _ in range(6):await asyncio.sleep(0)
    async def until(self,state,limit=500):
        for _ in range(limit):
            if self.cal.state==state:return
            if self.cal.state in ('ERROR','ABORTED'):self.fail(self.cal.message)
            await self.tick()
        self.fail(f'Expected {state}, got {self.cal.state}')
    async def arm(self):
        await self.cal.prepare(fc.CalibrationConfig());await self.until('ARMED')
    async def test_full_run_deploy_reset(self):
        await self.arm()
        for _ in range(10):await self.tick()
        self.assertEqual(self.cal.state,'ARMED');self.assertEqual(self.esp.commands,['MODE MANUAL','FREQ 0'])
        self.esp.frame['state']='RUNNING';await self.until('AWAITING_STOP',3000)
        self.assertEqual(len(self.cal.points),9);self.assertTrue(all(p['samples']>=100 for p in self.cal.points))
        self.assertEqual([c for c in self.esp.commands if c.startswith('FREQ ')],['FREQ 0']+[f'FREQ {f:.3f}' for f in range(10,51,5)]+['FREQ 0'])
        self.esp.frame['state']='READY';await self.tick();await self.cal.task
        self.assertEqual(self.cal.state,'COMPLETED',self.cal.message)
        self.assertEqual(len(list((Path(self.tmp.name)/self.cal.run_id).iterdir())),4)
        await self.cal.deploy(self.cal.run_id);self.assertEqual(self.esp.frame['cal_valid'],1)
        await self.cal.reset();self.assertEqual(self.esp.frame['cal_valid'],0);self.assertEqual(len(self.cal.runs()),1)
        self.assertFalse(any(c in ('START','RESET') for c in self.esp.commands))
    async def test_start_and_cancel(self):
        self.esp.frame['state']='RUNNING'
        with self.assertRaises(ValueError):await self.cal.prepare(fc.CalibrationConfig())
        self.esp.frame['state']='READY';await self.arm()
        with self.assertRaises(ValueError):self.cal.require_idle()
        await self.cal.cancel();self.assertEqual(self.cal.state,'ABORTED');self.assertEqual(self.esp.commands[-1],'FREQ 0')
    async def test_fault(self):
        await self.arm();before=len(self.esp.commands);self.esp.frame.update(state='FAULT',estop_pressed=1)
        await self.tick();await self.cal.task;self.assertEqual(self.cal.state,'ERROR');self.assertEqual(len(self.esp.commands),before)
    async def test_duplicates(self):
        await self.arm();self.esp.frame['state']='RUNNING';await self.tick()
        for _ in range(25):await self.tick(False)
        await self.cal.task;self.assertEqual(self.cal.state,'ERROR');self.assertIn('unique',self.cal.message)
    async def test_stability_and_missing_sensor(self):
        self.assertFalse(self.cal._stable([(i*.05,{'pv':i*.05}) for i in range(61)]))
        self.assertTrue(self.cal._stable([(i*.05,{'pv':10.}) for i in range(61)]))
        await self.arm();self.esp.frame.update(state='RUNNING',sensor_ok=0);await self.tick();await self.cal.task
        self.assertEqual(self.cal.state,'ERROR')
    async def test_frequency_timeout(self):
        await self.cal.prepare(fc.CalibrationConfig(ramp_timeout_s=30));await self.until('ARMED')
        self.esp.frame['state']='RUNNING';await self.until('RAMPING')
        for _ in range(700):
            self.esp.frame.update(actual_hz=0.,applied_hz=0.);await self.tick()
            if self.cal.task.done():break
        await self.cal.task;self.assertIn('trajectory timeout',self.cal.message)
    async def test_queue_and_paths(self):
        with self.assertRaises(ValueError):self.cal.file('../../outside')
        await self.cal.prepare(fc.CalibrationConfig())
        for _ in range(1000):self.cal.publish(self.esp.frame)
        self.assertEqual(self.cal.queue.qsize(),128)
