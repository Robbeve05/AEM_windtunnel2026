"""Offscreen smoke tests for the native-only Qt HMI.

Run: QT_QPA_PLATFORM=offscreen python -m unittest tests/test_native_hmi.py
"""
import os
import unittest
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtWidgets import QApplication
    from PySide6.QtCore import QTimer
    from desktop import HMIWindow
    from native_calibration import CalibrationPanel
    QT_AVAILABLE = True
except ImportError:
    QT_AVAILABLE = False


@unittest.skipUnless(QT_AVAILABLE, "PySide6 unavailable; install requirements-desktop.txt")
class NativeHMITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def test_native_settings_and_calibration_parity(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            self.assertEqual(window.settings_tabs.count(), 2)
            self.assertEqual(window.settings_tabs.tabText(0), "General")
            self.assertEqual(window.settings_tabs.tabText(1), "Calibration")
            self.assertIsInstance(window.calibration_panel, CalibrationPanel)
            panel = window.calibration_panel
            self.assertEqual(panel._configuration(), dict(
                start_hz=10.0, end_hz=50.0, step_hz=5.0,
                average_s=5.0, window_s=3.0))
            self.assertEqual(len(window.nav_buttons), 6)
            self.assertEqual(window.setpoint_spin.maximum(), 40.0)
            self.assertTrue(panel.prepare_button.text())
            self.assertTrue(panel.reset_button.text())
            window.close()

    def test_status_display_and_point_plot(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            panel = window.calibration_panel
            panel.render(dict(
                state="MEASURING", active=True, index=1, total=9, progress=50,
                target_hz=10, message="Recording", run_id="20261101T100000-abcdef12",
                stability=dict(std=.04, trend=.001),
                points=[dict(target_hz=10, mean_actual_hz=10, mean_pv=7.2,
                             std_pv=.04, samples=100)],
                esp32=dict(connected=True, stale=False, cal_source="DEFAULT",
                           cal_valid=0, actual_hz=10, applied_hz=10, pv=7.2)
            ))
            self.assertEqual(panel.results_table.rowCount(), 1)
            self.assertEqual(panel.progress.value(), 50)
            self.assertFalse(panel.prepare_button.isEnabled())
            self.assertTrue(panel.cancel_button.isEnabled())
            window.close()


    def test_overview_visible_after_navigation_without_esp32(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window.show()
            self.app.processEvents()
            window._show_page("settings")
            window._show_page("overview")
            self.app.processEvents()
            self.assertIs(window.pages.currentWidget(), window.overview_page)
            self.assertIs(window.overview_page.widget(), window.overview_content)
            self.assertTrue(window.chart_card.isVisibleTo(window))
            self.assertTrue(window.recording_card.isVisibleTo(window))
            self.assertTrue(window.operating_card.isVisibleTo(window))
            self.assertTrue(window.setpoint_spin.isEnabled())
            self.assertTrue(window.manual_hz.isEnabled())
            self.assertFalse(window.setpoint_apply.isEnabled())
            self.assertFalse(window.mode_apply.isEnabled())
            self.assertFalse(window.manual_apply.isEnabled())
            window.close()



if __name__ == "__main__":
    unittest.main()
