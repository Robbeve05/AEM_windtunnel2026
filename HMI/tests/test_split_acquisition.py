import asyncio
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from backend.esp32 import parse_status
from tests.test_esp32_ethernet import FRAME
import main

class SplitAcquisitionTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory();base=Path(self.tmp.name)
        (base/'config').mkdir()
        config=json.loads((main.BASE/'config/config.json').read_text())
        config.update(simulation=False,device_mode='esp32_tcp')
        (base/'config/config.json').write_text(json.dumps(config))
        self.c=main.Controller(base);self.frame={}
        self.c.esp32=SimpleNamespace(fresh=True,status_timeout=1.,snapshot=lambda:self.frame)

    async def asyncTearDown(self): self.tmp.cleanup()

    async def publish(self,seq,tap_seq,state='READY',mode='AUTO',sensor_ok=1,updated=1):
        line=FRAME.replace('state=FAULT','state='+state).replace('mode=MANUAL','mode='+mode)
        line=line.replace('sensor_ok=1','sensor_ok='+str(sensor_ok))
        line+=f' sample_seq={seq} taps_seq={tap_seq} taps_age_ms=10 taps_available=1 taps_valid=3 taps_updated={updated} taps_errors=0 p0=5.0 p1=6.0 tap_age0_ms=10 tap_age1_ms=0 pitot_acquisition_us=113'
        self.frame=parse_status(line)
        self.assertIsNotNone(self.frame)
        await self.c._esp32_status(self.frame)

    async def test_updates_wait_ready_manual_auto_and_independent_tap_sequences(self):
        for seq,(state,mode) in enumerate((('WAIT','AUTO'),('READY','AUTO'),('RUNNING','MANUAL'),('RUNNING','AUTO')),1):
            await self.publish(seq,seq,state,mode)
            self.assertEqual(self.c.sample['state'],state)
            self.assertEqual(self.c.sample['taps_seq'],seq)
            self.assertEqual(self.c.sample['pressure_pa'],[5.0]+[None]*15)
        await self.publish(5,4)
        self.assertFalse(self.c.sample['taps_new'])
        self.assertEqual(self.c.sample['pressure_pa'],[None]*16)
        await self.publish(6,5,updated=2)
        self.assertEqual(self.c.sample['pressure_pa'],[None,6.0]+[None]*14)
        self.assertEqual(self.c.sample['tap_channel_indices'],[1])
        stamp=self.c.sample['host_timestamp'];await self.publish(6,5)
        self.assertEqual(self.c.sample['host_timestamp'],stamp)

    async def test_taps_visible_during_invalid_pitot_and_stale_transport(self):
        await self.publish(1,1,state='WAIT',sensor_ok=0)
        self.assertEqual(self.c.sample,{})
        snapshot=self.c.status()['pressure_taps']
        self.assertEqual(snapshot['values_pa'][:2],[5.,6.])
        self.assertEqual(snapshot['sequence'],1)
        self.assertEqual(snapshot['channel_age_ms'][:2],[10,0])
        self.c.esp32.fresh=False
        snapshot=self.c.status()['pressure_taps']
        self.assertEqual(snapshot['valid_mask'],0)
        self.assertEqual(snapshot['values_pa'],[None]*16)

class SparseTapTests(unittest.TestCase):
    def test_missing_channels_are_not_shifted_or_used_for_cp(self):
        from backend.cp import calculate_cp
        result=calculate_cp(dict(airspeed_ms=10,static_pressure_pa=0,pressure_pa=[None,5.]),
                            [dict(name='tap0'),dict(name='tap1')],1.225)
        self.assertIsNone(result[0]['cp'])
        self.assertEqual(result[1]['name'],'tap1')
        self.assertGreater(result[1]['cp'],0)

if __name__=='__main__': unittest.main()
