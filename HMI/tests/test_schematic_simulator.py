import unittest
from types import SimpleNamespace
from backend.simulator import Simulator


class SchematicSimulatorTests(unittest.TestCase):
    def make(self):
        return Simulator(SimpleNamespace(air_density=1.2, max_airspeed_ms=40, pressure_taps=[]))

    def ready(self):
        s = self.make()
        self.assertEqual(s.state, 'INIT')
        s.read(.04)
        self.assertEqual(s.state, 'WAIT')
        s.reset()
        self.assertEqual(s.state, 'READY')
        return s

    def test_nc_boot_validation(self):
        for di, fault in [(1, 'EMERGENCY_STOP'), (4, 'INTERLOCK'), (5, 'SAFETY_FEEDBACK')]:
            with self.subTest(di=di):
                s = self.make()
                s.set_input(di, False)
                s.read(.04)
                self.assertEqual((s.state, s.fault), ('FAULT', fault))
                self.assertFalse(s.snapshot()['vfd_enable'])
                s.set_input(di, True)
                s.read(.1)
                self.assertEqual(s.state, 'FAULT')
                s.reset()
                s.read(.04)
                self.assertEqual(s.state, 'READY')

    def test_no_start_without_reset_or_fresh_edge(self):
        s = self.make()
        s.set_input(2, True)
        s.set_input(3, True)
        s.read(.1)
        self.assertEqual(s.state, 'WAIT')
        s.read(.1)
        self.assertEqual(s.state, 'WAIT')
        with self.assertRaises(ValueError):
            s.start()
        s.reset()
        self.assertEqual(s.state, 'READY')
        s.read(.1)
        self.assertEqual(s.state, 'READY')  # START still held
        s.set_input(3, False); s.read(.04)
        s.set_input(3, True); s.read(.01)
        self.assertEqual(s.state, 'READY')
        s.read(.02)
        self.assertEqual(s.state, 'RUNNING')

    def test_stop_curve_release_and_no_restart(self):
        s = self.ready()
        s.start()
        s.read(20)
        initial = s.speed
        s.set_input(4, False)
        self.assertEqual(s.state, 'STOP')
        self.assertTrue(s.snapshot()['vfd_enable'])
        s.read(.01)
        self.assertGreater(s.speed, initial * .99)
        with self.assertRaises(ValueError):
            s.start()
        s.set_input(4, True)  # release does not cancel the ramp
        s.setpoint = 40
        previous = s.speed
        while s.state == 'STOP':
            s.read(.01)
            self.assertLessEqual(s.speed, previous)
            previous = s.speed
        self.assertEqual(s.state, 'READY')
        self.assertFalse(s.snapshot()['vfd_enable'])
        self.assertEqual(s.speed, 0)
        s.read(1)
        self.assertEqual(s.state, 'READY')

    def test_fault_latches_from_every_state(self):
        for state in ('INIT', 'WAIT', 'READY', 'RUNNING', 'STOP', 'FAULT'):
            for di in (1, 5):
                with self.subTest(state=state, di=di):
                    s = self.ready()
                    s.state = state
                    s.set_input(di, False)
                    self.assertEqual(s.state, 'FAULT')
                    self.assertFalse(s.snapshot()['vfd_enable'])
                    frame = s.read(.01)
                    self.assertEqual(frame["fan_output_pct"], 0)
                    s.set_input(di, True)
                    s.read(.1)
                    self.assertEqual(s.state, 'FAULT')
                    s.reset(); s.read(.04)
                    self.assertEqual(s.state, 'READY')
                    s.read(1)
                    self.assertEqual(s.state, 'READY')

    def test_bounce_and_held_reset(self):
        s = self.make(); s.read(.04)
        s.set_input(2, True); s.read(.01)
        s.set_input(2, False); s.read(.01)
        self.assertEqual(s.state, 'WAIT')
        s.set_input(2, True); s.read(.03); s.read(.01)
        self.assertEqual(s.state, 'READY')
        s.set_input(3, True); s.read(.04)
        self.assertEqual(s.state, 'READY')
        s.set_input(2, False); s.read(.04)
        self.assertEqual(s.state, 'READY')
        self.assertEqual(s.snapshot()['raw_levels'], 29)

if __name__ == '__main__':
    unittest.main()
