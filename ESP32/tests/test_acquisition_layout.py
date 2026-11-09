"""Architectural invariants in addition to production C integration tests."""
import re
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class AcquisitionOwnershipTests(unittest.TestCase):
    def test_single_producer_and_no_control_queue_feedback(self):
        control=(ROOT/'src/ControlTask.c').read_text()
        sensor=(ROOT/'src/SensorTask.c').read_text()
        tasks=(ROOT/'src/ApplicationTasks.c').read_text()
        self.assertNotRegex(control,r'xQueuePeek\(\s*pitot_samples')
        self.assertEqual(len(re.findall(r'xQueueOverwrite\(pitot_samples',control)),1)
        self.assertNotIn('SensorLib_PitotStep',sensor)
        self.assertNotIn('SensorLib_Init',sensor)
        self.assertNotIn('pitot_samples',sensor)
        self.assertIn('xQueueOverwrite(pressure_taps',sensor)
        self.assertIn('pitot_samples = xQueueCreate(1, sizeof(PitotSample))',tasks)
        self.assertIn('pressure_taps = xQueueCreate(1, sizeof(PressureTapSample))',tasks)
        # Changes to timer phases require their own timing analysis.
        self.assertIn('sensor_phase ? sensor_task_handle : control_task_handle',control)
        loop=control[control.index('void ControlTask('):]
        self.assertLess(loop.index('Control_ProcessCommands'),loop.index('Control_AcquirePitot'))
        self.assertLess(loop.index('Control_AcquirePitot'),loop.index('Control_UpdateController'))

if __name__=='__main__':unittest.main()
