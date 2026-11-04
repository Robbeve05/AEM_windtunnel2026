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
    from PySide6.QtGui import QColor, QPalette
    from PySide6.QtTest import QTest
    from PySide6.QtWidgets import QLineEdit
    from desktop import HMIWindow, OperatorNumericInput
    from native_calibration import CalibrationPanel
    QT_AVAILABLE = True
except ImportError:
    QT_AVAILABLE = False


@unittest.skipUnless(QT_AVAILABLE, "PySide6 unavailable; install requirements-desktop.txt")
class NativeHMITests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def test_graph_maximum_is_independent_of_auto_limit(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            self.assertEqual(window.graph_max_spin.value(), 50.0)
            self.assertEqual(window.chart.y_max, 50.0)
            self.assertEqual(window.setpoint_spin.maximum(), 40.0)
            window._on_config({"graph_max_airspeed_ms": 65}, None)
            self.assertEqual(window.graph_max_spin.value(), 65.0)
            self.assertEqual(window.chart.y_max, 65.0)
            self.assertEqual(window.setpoint_spin.maximum(), 40.0)
            window.graph_max_spin.setValue(75.0)
            calls = []
            def mock_post(path, body, callback):
                calls.append((path, body))
                callback({"graph_max_airspeed_ms": body["graph_max_airspeed_ms"]}, None)
            with patch.object(window, "_post", mock_post):
                window._apply_graph_airspeed()
            self.assertEqual(calls, [(
                "settings/graph-airspeed", {"graph_max_airspeed_ms": 75.0}
            )])
            self.assertEqual(window.chart.y_max, 75.0)
            self.assertEqual(window.setpoint_spin.maximum(), 40.0)
            window.close()

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

    def test_wait_blockers_and_expandable_diagnostics(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window._on_status({
                "simulation": False,
                "esp32": {
                    "state": "WAIT", "fault": "NONE", "last_fault": "NONE",
                    "stale": False, "connected": True, "mode": "AUTO", "sp": 0.0,
                    "reset_required": 0, "estop_pressed": 0, "diag_blockers": 163840,
                    "diag_v20_status_word": 513, "diag_modbus_errors": 2,
                    "diag_modbus_timeouts": 1, "diag_v20_run_commissioned": 0,
                    "diag_drive_communication_ok": 1, "diag_v20_ready": 0,
                    "diag_drive_fault_free": 1, "diag_v20_diagnostics_complete": 1,
                },
            }, None)
            self.assertIn("V20 RUN commissioning lock active", window.waiting_for_text.text())
            self.assertIn("Physical RESET sequence not accepted", window.waiting_for_text.text())
            self.assertIn("V20 status word: 0x0201", window.diagnostics_text.toPlainText())
            self.assertTrue(window.waiting_for_text.isVisibleTo(window))
            self.assertTrue(window.diagnostics_text.isHidden())
            window.diagnostics_toggle.setChecked(True)
            self.assertFalse(window.diagnostics_text.isHidden())
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
            self.assertFalse(window.manual_apply.isEnabled())  # 0 Hz is below manual 10 Hz minimum.
            window.manual_hz.setText("10")
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



    def test_card_pages_use_direct_native_painting(self):
        # Overview, Measurements, Storage and Settings all use Card.
        # A graphics-effect composition regression would leave these pages blank
        # while Raw Data/Cp placeholders (plain QFrame) still rendered.
        from desktop import Card
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window.resize(1500, 900)
            window.show()
            self.app.processEvents()
            window._show_page("settings")
            window.settings_tabs.setCurrentIndex(1)
            self.app.processEvents()
            for name, card in (
                ("overview", window.chart_card),
                ("measurements", window.measurements_empty.parentWidget()),
                ("storage", window.local_space.parentWidget().parentWidget()),
                ("settings", window.filter_status.parentWidget()),
            ):
                window._show_page(name)
                if name == "settings":
                    window.settings_tabs.setCurrentIndex(0)  # General card is hidden under Calibration.
                self.app.processEvents()
                self.assertIsNone(card.graphicsEffect(), name)
                self.assertTrue(card.isVisibleTo(window), name)
                self.assertGreater(card.width(), 10, name)
                self.assertGreater(card.height(), 10, name)
            self.assertTrue(all(
                child.graphicsEffect() is None
                for child in window.findChildren(Card)
            ))
            window.close()



    def test_air_speed_and_manual_hz_text_is_visible_and_typable_without_esp32(self):
        """Exercise actual Windows Qt keyboard entry into a plain QLineEdit."""
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window.resize(1500, 900)
            window.show()
            self.app.processEvents()
            window._on_status({"simulation": False, "esp32": {"stale": True}}, None)
            for mode, widget, new_text, expected in (
                ("AUTO", window.setpoint_spin, "17.50", 17.5),
                ("MANUAL", window.manual_hz, "32,50", 32.5),
            ):
                window.mode_select.setCurrentText(mode)
                self.app.processEvents()
                self.assertIsInstance(widget, OperatorNumericInput)
                self.assertTrue(widget.isEnabled())
                self.assertTrue(widget.isVisibleTo(window))
                self.assertIn("color: #17212b", widget.styleSheet())
                self.assertEqual(widget.palette().color(
                    QPalette.ColorRole.Text).name(), QColor("#17212b").name())
                widget.setFocus()
                widget.selectAll()
                QTest.keyClicks(widget, new_text)
                self.app.processEvents()
                self.assertEqual(widget.text(), new_text)
                self.assertAlmostEqual(widget.value(), expected)
            self.assertFalse(window.mode_apply.isEnabled())
            self.assertFalse(window.setpoint_apply.isEnabled())
            self.assertFalse(window.manual_apply.isEnabled())
            window.close()

    def test_numeric_field_rejects_out_of_range_and_does_not_send_commands(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window._on_status({"simulation": False, "esp32": dict(
                connected=True, stale=False, state="READY", mode="AUTO",
                fault="NONE", sp=0)}, None)
            self.assertTrue(window.setpoint_apply.isEnabled())
            window.setpoint_spin.setText("41")
            self.assertFalse(window.setpoint_apply.isEnabled())
            with self.assertRaises(ValueError):
                window.setpoint_spin.value()
            window.setpoint_spin.setText("20")
            self.assertTrue(window.setpoint_apply.isEnabled())
            window.mode_select.setCurrentText("MANUAL")
            window._on_status({"simulation": False, "esp32": dict(
                connected=True, stale=False, state="READY", mode="MANUAL",
                fault="NONE", sp=0)}, None)
            window.manual_hz.setText("51")
            self.assertFalse(window.manual_apply.isEnabled())
            with self.assertRaises(ValueError):
                window.manual_hz.value()
            window.manual_hz.setText("25,5")
            self.assertTrue(window.manual_apply.isEnabled())
            self.assertAlmostEqual(window.manual_hz.value(), 25.5)
            window.close()

    def test_single_config_limits_and_visible_validation_feedback(self):
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window._on_config(dict(
                filter_cutoff_hz=1.0, flow_metrics_enabled=True, max_airspeed_ms=40,
                min_manual_frequency_hz=10, max_manual_frequency_hz=50,
                default_recording_rate_hz=25,
                calibration_defaults=dict(start_hz=10,end_hz=50,step_hz=5,average_s=5,window_s=3),
            ), None)
            window.mode_select.setCurrentText("AUTO")
            window.setpoint_spin.setText("50")
            self.assertIn("0–40 m/s", window.input_validation.text())
            window.setpoint_spin.setText("30")
            self.assertEqual(window.input_validation.text(), "")
            window.mode_select.setCurrentText("MANUAL")
            window.manual_hz.setText("5")
            self.assertIn("10–50 Hz", window.input_validation.text())
            window.manual_hz.setText("50")
            self.assertEqual(window.input_validation.text(), "")
            window.close()


    def test_manual_minimum_comes_from_local_config_when_backend_offline(self):
        """Offline Windows preview must not silently default to 0–50 Hz."""
        with patch.object(HMIWindow, "_get", lambda *args, **kwargs: None):
            window = HMIWindow()
            window.resize(1500, 900)
            window.show()
            self.app.processEvents()
            self.assertEqual(window.config_source, "local")
            self.assertEqual(window.config["min_manual_frequency_hz"], 10)
            self.assertEqual(window.manual_hz.minimum_value, 10)
            window.mode_select.setCurrentText("MANUAL")
            for invalid in ("0", "5", "60"):
                window.manual_hz.setText(invalid)
                self.assertFalse(window.manual_hz.has_valid_value())
                self.assertIn("10–50 Hz", window.input_validation.text())
                self.assertFalse(window.manual_apply.isEnabled())
            window.manual_hz.setText("50")
            self.assertTrue(window.manual_hz.has_valid_value())
            self.assertEqual(window.input_validation.text(), "")
            self.assertFalse(window.manual_apply.isEnabled())  # Offline/UNKNOWN
            window.close()


if __name__ == "__main__":
    unittest.main()
