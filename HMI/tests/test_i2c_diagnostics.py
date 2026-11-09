import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import unittest
from PySide6.QtWidgets import QApplication
from backend.esp32 import I2C_FIELDS, parse_status, MAX_FRAME_BYTES
from native_diagnostics import DiagnosticsPanel
from tests.test_esp32_ethernet import FRAME

class I2CDiagnosticsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.app=QApplication.instance() or QApplication([])

    def test_wire_and_panel_show_numeric_counters_error_and_stale(self):
        values=(100,2,3,4,5,6,1,107,112,2000,350,2350,0,0,2,-1,1234,10)
        token=','.join(map(str,values))
        line=FRAME+' i2c_hz=400000 i2c_diag_v=1 i2c_20='+token
        d=parse_status(line)
        self.assertEqual(d['i2c_20'],dict(zip(I2C_FIELDS,values)))
        panel=DiagnosticsPanel();panel.update_status(d,stale=False)
        self.assertEqual(panel.rows['i2c_hz'].text(1),'400000')
        self.assertEqual(panel.rows['i2c_20_nack'].text(1),'3')
        self.assertEqual(panel.rows['i2c_20_last_failure_error'].text(1),'-1')
        panel.i2c_devices['i2c_20'].setExpanded(True)
        panel.update_status(d,stale=True)
        self.assertEqual(panel.rows['i2c_20_ok'].text(1),'—')
        self.assertTrue(panel.i2c_devices['i2c_20'].isExpanded())
        self.assertIsNone(parse_status(line.replace('i2c_diag_v=1','i2c_diag_v=2')))
        self.assertIsNone(parse_status(line.replace(token,'1,2')))
        self.assertIsNone(parse_status(line.replace(token,token.replace('2000','nan'))))
        # Firmware can supply a differently ordered/custom tap address.
        d['i2c_36']=d['i2c_20'];panel.update_status(d,stale=False)
        self.assertEqual(panel.rows['i2c_36_ok'].text(1),'100')
        panel.close()

    def test_expanded_frame_budget_including_all_six_addresses(self):
        token=','.join(['4294967295']*18)
        line=FRAME+''.join(f' reserved{i}=4294967295' for i in range(300))+' i2c_hz=400000 i2c_diag_v=1'
        line+=''.join(f' i2c_{a}={token}' for a in ('20','28','51','70','71','29'))
        self.assertGreater(len(line),6144)
        self.assertLess(len(line),MAX_FRAME_BYTES)
        self.assertIsNotNone(parse_status(line))

if __name__=='__main__': unittest.main()
