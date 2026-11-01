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


    def test_mode_switch_only_shows_selected_input_and_ready_gates_apply(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window._on_status({"simulation": False, "esp32": {"stale": True}}, None)
            window.mode_select.setCurrentText("MANUAL")
            self.assertEqual(window.mode_value_stack.currentIndex(), 1)
            self.assertFalse(window.setpoint_apply.isEnabled())
            self.assertFalse(window.manual_apply.isEnabled())
            window.mode_select.setCurrentText("AUTO")
            self.assertEqual(window.mode_value_stack.currentIndex(), 0)
            self.assertFalse(window.mode_apply.isEnabled())
            # Fresh RUNNING cannot apply a new operating point.
            window._on_status({"simulation": False, "esp32": dict(
                connected=True, stale=False, state="RUNNING", mode="AUTO",
                fault="NONE", sp=0)}, None)
            self.assertFalse(window.mode_apply.isEnabled())
            self.assertFalse(window.setpoint_apply.isEnabled())
            window._on_status({"simulation": False, "esp32": dict(
                connected=True, stale=False, state="READY", mode="AUTO",
                fault="NONE", sp=0)}, None)
            self.assertTrue(window.setpoint_apply.isEnabled())
            self.assertFalse(window.manual_apply.isEnabled())
            window.mode_select.setCurrentText("MANUAL")
            self.assertEqual(window.mode_value_stack.currentIndex(), 1)
            self.assertFalse(window.manual_apply.isEnabled())  # Draft until applied.
            window._on_status({"simulation": False, "esp32": dict(
                connected=True, stale=False, state="READY", mode="MANUAL",
                fault="NONE", sp=0)}, None)
            self.assertTrue(window.manual_apply.isEnabled())
            self.assertFalse(window.setpoint_apply.isEnabled())
            window.close()


    def test_every_page_remains_visible_after_opening_calibration(self):
        # Regression: the large calibration form must not stretch the shared
        # QStackedWidget so its other pages render outside the viewport.
        from desktop import PAGES
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window.resize(1600, 900)
            window.show()
            self.app.processEvents()
            window._show_page("settings")
            window.settings_tabs.setCurrentIndex(1)
            self.app.processEvents()
            self.assertEqual(window.settings_tabs.tabText(1), "Calibration")
            for key, *_ in PAGES:
                window._show_page(key)
                self.app.processEvents()
                current = window.pages.currentWidget()
                self.assertTrue(current.isVisibleTo(window), key)
                self.assertGreater(window.pages.width(), 300, key)
                self.assertGreater(window.pages.height(), 200, key)
                self.assertLessEqual(window.pages.height(), window.height(), key)
                self.assertEqual(window.pages.currentIndex(),
                                 [p[0] for p in PAGES].index(key))
            window._show_page("overview")
            self.app.processEvents()
            self.assertTrue(window.chart_card.isVisibleTo(window))
            self.assertTrue(window.recording_card.isVisibleTo(window))
            self.assertTrue(window.operating_card.isVisibleTo(window))
            self.assertLessEqual(window.pages.minimumSizeHint().height(), 200)
            window.close()



if __name__ == "__main__":
    unittest.main()
