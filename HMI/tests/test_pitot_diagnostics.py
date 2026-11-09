import os
os.environ['QT_QPA_PLATFORM']='offscreen'
import asyncio
import unittest
from unittest.mock import patch
from PySide6.QtWidgets import QApplication
from desktop import HMIWindow
from backend.esp32 import ESP32Client, parse_status
from tests.test_esp32_ethernet import FRAME

class PitotDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app=QApplication.instance() or QApplication([])

    def test_wire_types_and_invalid_numeric(self):
        frame=FRAME+' pitot_raw_count=8192 pitot_status_bits=0 pitot_raw_pressure_pa=-12.5 pitot_sample_seq=42 pitot_calibrated=1'
        parsed=parse_status(frame)
        self.assertEqual(parsed['pitot_raw_count'],8192)
        self.assertEqual(parsed['pitot_raw_pressure_pa'],-12.5)
        self.assertEqual(parsed['pitot_sample_seq'],42)
        self.assertIsNone(parse_status(frame.replace('-12.5','nan')))

    def test_graph_sequence_calibration_invalid_and_stale(self):
        with patch.object(HMIWindow,'_get',lambda *a,**k:None):
            w=HMIWindow()
        d=dict(pitot_sample_seq=1,pitot_age_ms=0,pitot_read_ok=1,pitot_valid=0,
               pitot_calibrated=0,pitot_raw_pressure_pa=-20.,pitot_corrected_pressure_pa=2.,
               pitot_filtered_pressure_pa=1.,pitot_airspeed_ms=0.)
        data=dict(pitot_diagnostics=d,pitot_transport_fresh=True)
        w._render_pitot(data)
        self.assertEqual(w.pitot_state.text(),'CALIBRATING')
        self.assertEqual(len(w.pitot_history),1)
        self.assertIsNone(w.pitot_history[0]['speed'])
        w._render_pitot(data)
        self.assertEqual(len(w.pitot_history),1)
        d.update(pitot_sample_seq=2,pitot_calibrated=1,pitot_valid=1,pitot_airspeed_ms=3.)
        w._render_pitot(data)
        self.assertEqual(w.pitot_state.text(),'LIVE')
        self.assertEqual(w.pitot_speed_chart.points[-1]['speed'],3.)
        self.assertLess(w.pitot_chart.y_min,0)
        d['pitot_read_ok']=0
        w._render_pitot(data)
        self.assertIn('INVALID',w.pitot_state.text())
        d['pitot_read_ok']=1
        w.pitot_last_progress -= 1
        w._render_pitot(data)
        self.assertEqual(w.pitot_state.text(),'STALE')
        data['pitot_transport_fresh']=False
        w._render_pitot(data)
        self.assertEqual(w.pitot_state.text(),'STALE')
        w.pitot_chart.grab()
        w.close()

class PitotTCPTests(unittest.IsolatedAsyncioTestCase):
    async def test_tcp_calibration_sequences_reach_controller(self):
        from main import Controller
        from types import SimpleNamespace
        c=Controller.__new__(Controller)
        c.calibration=SimpleNamespace(publish=lambda _:None)
        received=[]
        ready=asyncio.Event()
        async def callback(frame):
            await c._esp32_status(frame)
            received.append(c.esp32_frame.copy())
            if len(received)>=3: ready.set()
        async def handler(reader,writer):
            try:
                for seq in (1,1,2):
                    line=FRAME.replace('sensor_ok=1','sensor_ok=0')+f' pitot_sample_seq={seq} pitot_zero_samples=5 pitot_calibrated=0 pitot_raw_pressure_pa=12.5'
                    wire=(line+'\n').encode()
                    writer.write(wire[:25]); await writer.drain()
                    writer.write(wire[25:]); await writer.drain()
                    await asyncio.sleep(0.02)
                await reader.read()
            finally:
                writer.close()
                await writer.wait_closed()
        server=await asyncio.start_server(handler,'127.0.0.1',0)
        client=ESP32Client('127.0.0.1',server.sockets[0].getsockname()[1])
        task=asyncio.create_task(client.run(callback,lambda *a:None))
        try:
            await asyncio.wait_for(ready.wait(),2)
            self.assertEqual([f['pitot_sample_seq'] for f in received[:3]],[1,1,2])
            self.assertEqual(c.esp32_frame['pitot_raw_pressure_pa'],12.5)
        finally:
            task.cancel(); await asyncio.gather(task,return_exceptions=True)
            server.close(); await server.wait_closed()

if __name__=='__main__': unittest.main()
