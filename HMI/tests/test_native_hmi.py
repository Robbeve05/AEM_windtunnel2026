"""Offscreen smoke tests for the native-only Qt HMI.

Run: QT_QPA_PLATFORM=offscreen python -m unittest tests/test_native_hmi.py
"""
import os
import unittest
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

try:
    from PySide6.QtWidgets import QApplication
    from PySide6.QtCore import QTimer, QPoint, Qt
    from PySide6.QtGui import QColor, QPalette
    from PySide6.QtTest import QTest
    from PySide6.QtWidgets import QLineEdit, QCheckBox
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

    def test_aerodynamic_navigation_actions_and_explicit_source(self):
        from desktop import PAGES
        from native_aerodynamics import AerodynamicPlot
        with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
            window = HMIWindow(); window.resize(1280, 800); window.show()
            self.assertEqual(PAGES[2][2], 'Aerodynamic Analysis')
            window._show_page('cp'); panel = window.aerodynamic_panel
            self.assertEqual([panel.tabs.tabText(i) for i in range(4)],
                             ['Overview', 'Cp Distribution', 'Aerodynamic Coefficients', 'Compare'])
            for index in range(4): panel.tabs.setCurrentIndex(index); self.app.processEvents()
            self.assertGreaterEqual(panel.tabs.widget(3).verticalScrollBar().maximum(), 1)
            experiment = window.aerodynamic_experiment
            self.assertEqual(experiment.duration.value(), 5)
            self.assertEqual(experiment.configuration()['source'], 'physical')
            self.assertFalse(experiment.start.isEnabled())
            experiment.name.setText('SIM_test'); experiment.airfoil.setText('Synthetic foil')
            experiment.source.setCurrentIndex(1)
            calls = []
            with patch.object(window, '_post', lambda path, body, callback: calls.append((path, body))):
                experiment.prepare_experiment()
                self.assertEqual(calls[0][0], 'aerodynamics/experiments')
                self.assertEqual(calls[0][1]['source'], 'simulation')
                self.assertFalse(any('tunnel' in path for path, body in calls))
            points = [dict(tap_id='A', surface='upper', x_c=.2, cp=-1, cp_std=.01),
                      dict(tap_id='B', surface='lower', x_c=.8, cp=1, cp_std=.01)]
            panel.cp_plot.set_results([dict(cp=points, experiment_name='fixture', alpha_deg=0, source='simulation')])
            panel.tabs.setCurrentIndex(1); self.app.processEvents(); panel.cp_plot.grab()
            hits = panel.cp_plot.hits
            self.assertEqual(len(hits), 2)
            self.assertLess(hits[0][0].y(), hits[1][0].y())  # Negative Cp is above positive Cp.
            panel.toggle_surface('upper', False); self.assertEqual(len(panel.cp_plot.series()), 1)
            window.close()

    def test_aerodynamic_native_export_commits_successful_reply(self):
        import tempfile
        from pathlib import Path
        from PySide6.QtCore import QObject, Signal, QByteArray
        from PySide6.QtNetwork import QNetworkReply
        class Reply(QObject):
            readyRead = Signal()
            finished = Signal()
            def __init__(self):
                super().__init__(); self.content = QByteArray(b'%PDF-successful-export')
            def readAll(self):
                content = self.content; self.content = QByteArray(); return content
            def error(self): return QNetworkReply.NetworkError.NoError
            def errorString(self): return 'No error'
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
                window = HMIWindow(); panel = window.aerodynamic_panel
                panel.identifier = 'safe-id'; panel.detail = {'files': ['analysis_report.pdf']}
                reply = Reply(); destination = Path(directory)/'report.pdf'
                with patch('native_aerodynamics.QFileDialog.getSaveFileName', return_value=(str(destination), '')):
                    with patch.object(window.network, 'get', return_value=reply) as request:
                        panel.download('analysis_report.pdf')
                        self.assertIn('/api/aerodynamics/experiments/safe-id/files/analysis_report.pdf', request.call_args.args[0].url().toString())
                        reply.readyRead.emit(); reply.finished.emit()
                self.assertEqual(destination.read_bytes(), b'%PDF-successful-export')
                self.assertTrue(panel.message.text().startswith('Exported '))
                window.close()

    def test_dark_windows_palette_keeps_workspace_and_diagnostics_readable(self):
        original = self.app.palette()
        dark = QPalette(original)
        for role in (QPalette.ColorRole.Window, QPalette.ColorRole.Base,
                     QPalette.ColorRole.Button):
            dark.setColor(role, QColor('#202020'))
        for role in (QPalette.ColorRole.Text, QPalette.ColorRole.WindowText,
                     QPalette.ColorRole.ButtonText):
            dark.setColor(role, QColor('#ffffff'))
        self.app.setPalette(dark)
        try:
            with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
                window = HMIWindow()
                window.show()
                window._on_status({'simulation': False, 'esp32': {'stale': True}}, None)
                window.diagnostics_toggle.setChecked(True)
                self.app.processEvents()
                self.assertEqual(window.overview_content.grab().toImage().pixelColor(0, 0).name(), '#f6f6f4')
                self.assertEqual(window.chart_card.grab().toImage().pixelColor(3, 25).name(), '#ffffff')
                self.assertEqual(window.diagnostics_text.palette().color(QPalette.ColorRole.Text).name(), '#181818')
                self.assertIn('ESP32 offline', window.diagnostics_text.toPlainText())
                self.assertEqual(len(window.health_labels), 6)
                self.assertGreaterEqual(window.diagnostics_text.height(), 140)
                self.assertIs(window.right_scroll.widget(), window.right)
                window._show_page('cp')
                for index in range(4):
                    panel = window.aerodynamic_panel
                    panel.tabs.setCurrentIndex(index)
                    self.app.processEvents()
                    content = panel.tabs.widget(index).widget()
                    self.assertTrue(content.testAttribute(Qt.WidgetAttribute.WA_StyledBackground))
                    self.assertEqual(content.grab().toImage().pixelColor(3, 25).name(), '#f6f6f4', panel.tabs.tabText(index))
                    viewport = panel.tabs.widget(index).viewport()
                    self.assertEqual(viewport.palette().color(QPalette.ColorRole.Window).name(), '#f6f6f4')
                for checkbox in window.aerodynamic_panel.findChildren(QCheckBox):
                    self.assertEqual(checkbox.palette().color(QPalette.ColorRole.WindowText).name(), '#181818')
                window._show_page('settings')
                for index in range(window.settings_tabs.count()):
                    window.settings_tabs.setCurrentIndex(index)
                    self.app.processEvents()
                    content = window.settings_tabs.widget(index)
                    self.assertTrue(content.property('workspaceSurface'))
                    color = content.grab().toImage().pixelColor(3, 25)
                    self.assertGreater(color.lightness(), 180, window.settings_tabs.tabText(index))
                window.close()
        finally:
            self.app.setPalette(original)

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

    def test_reynolds_input_uses_auto_and_existing_setpoint_api(self):
        from backend.reynolds import viscosity
        with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
            window = HMIWindow()
            window.mode_select.setCurrentIndex(2)
            panel = window.reynolds_widget
            self.assertEqual(len(panel.findChildren(QLineEdit)), 2)
            panel.reynolds.setText('100000')
            panel.chord.setText('100')
            status = dict(simulation=False, sensor_fresh=True, sample_age_s=0.05,
                sample=dict(airspeed_ms=10, temperature_c=20),
                reynolds=dict(environment=dict(valid=True, temperature_c=20,
                    air_density_kg_m3=1.225, dynamic_viscosity_pa_s=viscosity(20))),
                esp32=dict(connected=True, stale=False, state='READY', mode='MANUAL', fault='NONE'))
            window._on_status(status, None)
            self.assertEqual(window.mode_value_stack.currentIndex(), 2)
            self.assertFalse(panel.apply_button.isEnabled())
            calls = []
            with patch.object(window, '_post', lambda path, body, callback: calls.append((path, body))):
                window._apply_mode()
                self.assertEqual(calls, [('tunnel/mode', {'mode': 'AUTO'})])
                status['esp32']['mode'] = 'AUTO'
                window._on_status(status, None)
                self.assertEqual(window.mode_value_stack.currentIndex(), 2)
                self.assertTrue(panel.apply_button.isEnabled())
                window._apply_reynolds()
            self.assertEqual(calls[-1][0], 'tunnel/setpoint')
            self.assertAlmostEqual(calls[-1][1]['airspeed_ms'], 14.8033133237, places=8)
            self.assertEqual(calls[-1][1]['requested_reynolds'], 100000)
            self.assertEqual(calls[-1][1]['chord_mm'], 100)
            self.assertIn('20.00', panel.values['temperature'].text())
            self.assertNotEqual(panel.values['actual'].text(), '100,000')
            window.close()

    def test_reynolds_apply_blocks_invalid_sensor_and_over_limit_without_clamping(self):
        from backend.reynolds import viscosity
        with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
            window = HMIWindow()
            window.mode_select.setCurrentIndex(2)
            panel = window.reynolds_widget
            panel.reynolds.setText('1000000')
            panel.chord.setText('100')
            status = dict(simulation=False, sensor_fresh=True,
                sample=dict(airspeed_ms=10), esp32=dict(connected=True, stale=False,
                    state='READY', mode='AUTO', fault='NONE'),
                reynolds=dict(environment=dict(valid=True, temperature_c=20,
                    air_density_kg_m3=1.225, dynamic_viscosity_pa_s=viscosity(20))))
            window._on_status(status, None)
            self.assertFalse(panel.apply_button.isEnabled())
            self.assertIn('148.033', panel.values['airspeed'].text())
            self.assertIn('maximum', panel.message.text())
            panel.reynolds.setText('100000')
            self.assertTrue(panel.apply_button.isEnabled())
            status['reynolds']['environment'].update(valid=False, reason='Temperature telemetry unavailable')
            window._on_status(status, None)
            self.assertFalse(panel.apply_button.isEnabled())
            self.assertIn('Temperature telemetry unavailable', panel.message.text())
            window.close()

    def test_diagnostic_scroll_survives_unchanged_and_changed_status_updates(self):
        with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
            window = HMIWindow()
            window.show()
            window.diagnostics_toggle.setChecked(True)
            status = {'simulation': False, 'esp32': {'stale': False, 'connected': True, 'diag_modbus_errors': 1}}
            window._on_status(status, None)
            self.app.processEvents()
            bar = window.diagnostics_text.verticalScrollBar()
            self.assertGreater(bar.maximum(), 0)
            position = max(1, bar.maximum() // 2)
            bar.setValue(position)
            self.app.processEvents()
            position = bar.value()
            for _ in range(10):
                window._on_status(status, None)
            self.assertEqual(bar.value(), position)
            status['esp32']['diag_modbus_errors'] = 42
            window._on_status(status, None)
            self.app.processEvents()
            self.assertIn('42', window.diagnostics_text.toPlainText())
            self.assertEqual(bar.value(), position)
            window.close()

    def test_panel_dividers_and_diagnostics_grip_can_be_dragged(self):
        with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
            window = HMIWindow()
            window.resize(1600, 950)
            window.show()
            window.diagnostics_toggle.setChecked(True)
            self.app.processEvents()
            handle = window.main_splitter.handle(2)
            previous_width = window.right_scroll.width()
            start = handle.rect().center()
            QTest.mousePress(handle, Qt.MouseButton.LeftButton, pos=start)
            QTest.mouseMove(handle, start + QPoint(-100, 0))
            QTest.mouseRelease(handle, Qt.MouseButton.LeftButton, pos=handle.rect().center())
            self.app.processEvents()
            self.assertGreater(window.right_scroll.width(), previous_width + 50)
            grip = window.diagnostics_resize
            previous_height = window.diagnostics_text.height()
            start = grip.rect().center()
            QTest.mousePress(grip, Qt.MouseButton.LeftButton, pos=start)
            QTest.mouseMove(grip, start + QPoint(0, 80))
            QTest.mouseRelease(grip, Qt.MouseButton.LeftButton, pos=grip.rect().center())
            self.app.processEvents()
            self.assertGreater(window.diagnostics_text.height(), previous_height + 40)
            window.diagnostics_text.update_status({'state': 'Long diagnostic value ' * 60}, stale=False)
            window.diagnostics_text.tree.resizeColumnToContents(1)
            self.app.processEvents()
            self.assertGreater(window.diagnostics_text.horizontalScrollBar().maximum(), 0)
            window.close()

    def test_clean_diagnostic_groups_keep_expansion_and_suppress_stale_signals(self):
        with patch.object(HMIWindow, '_get', lambda *args, **kwargs: None):
            window = HMIWindow()
            panel = window.diagnostics_text
            self.assertEqual(set(panel.groups), {'Controller', 'Connection', 'Safety',
                'V20 drive', 'Outputs', 'Sensors', 'Registers', 'I2C bus'})
            panel.groups['Safety'].setExpanded(True)
            row = panel.rows['diag_estop_released']
            panel.tree.setCurrentItem(row)
            drive = {'diag_estop_released': 1, 'diag_v20_off2_active': 0,
                     'diag_v20_status_word': 513}
            panel.update_status(drive, stale=False)
            self.assertEqual(row.text(1), 'YES')
            self.assertEqual(panel.rows['diag_v20_off2_active'].text(1), 'NO')
            self.assertEqual(panel.rows['diag_v20_status_word'].text(1), '0x0201')
            self.assertTrue(panel.groups['Safety'].isExpanded())
            self.assertIs(panel.tree.currentItem(), row)
            panel.update_status(drive, stale=True)
            self.assertEqual(row.text(1), '—')
            self.assertEqual(len(window.health_labels), 6)
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
            self.assertEqual(panel.lambda_s.value(), 1.0)
            panel.dynamic_transitions = [dict(K_ms_per_hz=0.8, tau_s=2, L_s=0.2,
                candidate_pi=dict(kp=2.5, ti_s=2), warnings=['Candidate only'],
                trace=[dict(time_s=0, actual_hz=10, measured_ms=8, model_ms=8),
                       dict(time_s=1, actual_hz=12, measured_ms=9, model_ms=9.1)])]
            panel.transition_select.addItem('20 Hz')
            panel.show_dynamic_transition()
            self.assertIn('Kp=2.5000', panel.dynamics_detail.text())
            self.assertIn('Ti=2.0000', panel.dynamics_detail.text())
            self.assertIn('Candidate only', panel.dynamics_detail.text())
            self.assertFalse(panel.dynamic_plot.grab().isNull())
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
                    "diag_v20_control_word_requested": 1150,
                    "diag_v20_control_word_requested_valid": 1,
                    "diag_v20_control_word_acknowledged": 1150,
                    "diag_v20_control_word_acknowledged_valid": 1,
                    "diag_modbus_timeouts": 1, "diag_v20_run_commissioned": 0,
                    "diag_drive_communication_ok": 1, "diag_v20_ready": 0,
                    "diag_drive_fault_free": 1, "diag_v20_diagnostics_complete": 1,
                },
            }, None)
            self.assertIn("V20 RUN commissioning lock active", window.waiting_for_text.text())
            self.assertIn("Physical RESET sequence not accepted", window.waiting_for_text.text())
            self.assertEqual(window.diagnostics_text.rows['diag_v20_status_word'].text(1), '0x0201')
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
