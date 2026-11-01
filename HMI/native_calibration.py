"""Native Qt fan-characteristic calibration interface.

The local FastAPI backend owns calibration sequencing and ESP32 communication.
This widget never starts/resets the drive or performs real-time motor control.
Author: R.E. van Eekelen.
"""
import math
import re
from pathlib import Path
from urllib.parse import quote

from PySide6.QtCore import Qt, QIODevice
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtNetwork import QNetworkReply, QNetworkRequest
from PySide6.QtWidgets import (
    QComboBox, QDoubleSpinBox, QFileDialog, QFrame, QGridLayout,
    QHBoxLayout, QLabel, QMessageBox, QProgressBar, QPushButton,
    QScrollArea, QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget,
)
from PySide6.QtCore import QUrl, QSaveFile


RED = "#c8291e"
INK = "#181818"
MUTED = "#747474"
FILES = ("calibration.json", "raw_samples.csv", "characteristic.csv", "inverse_lookup.csv")
RUN_ID = re.compile(r"^[0-9]{8}T[0-9]{6}-[0-9a-f]{8}$")


def number(value, places=2):
    try:
        value = float(value)
        return f"{value:.{places}f}" if math.isfinite(value) else "—"
    except (TypeError, ValueError):
        return "—"


class FanCharacteristicPlot(QWidget):
    """Measured steady-state frequency versus filtered Pitot velocity."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.points = []
        self.setMinimumHeight(235)

    def set_points(self, points):
        filtered = []
        for point in points or []:
            try:
                hz, velocity = float(point["target_hz"]), float(point["mean_pv"])
            except (KeyError, TypeError, ValueError):
                continue
            if math.isfinite(hz) and math.isfinite(velocity) and hz >= 0 and velocity >= 0:
                filtered.append((hz, velocity))
        if filtered != self.points:
            self.points = filtered
            self.update()

    def paintEvent(self, event):
        super().paintEvent(event)
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = self.rect().adjusted(64, 28, -23, -42)
        if rect.width() < 10 or rect.height() < 10:
            return
        maximum = max(40.0, *(v for _, v in self.points))
        maximum = 5.0 * math.ceil(maximum / 5.0)
        painter.setPen(QColor(MUTED))
        painter.drawText(7, 19, "Airspeed (m/s)")
        for i in range(5):
            y = rect.top() + rect.height() * i / 4
            painter.setPen(QPen(QColor("#e5e5e0"), 1))
            painter.drawLine(int(rect.left()), int(y), int(rect.right()), int(y))
            painter.setPen(QColor(MUTED))
            painter.drawText(3, int(y - 9), 54, 18,
                             Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter,
                             f"{maximum * (1 - i / 4):.0f}")
        for i in range(6):
            x = rect.left() + rect.width() * i / 5
            painter.setPen(QPen(QColor("#e5e5e0"), 1))
            painter.drawLine(int(x), int(rect.top()), int(x), int(rect.bottom()))
            painter.setPen(QColor(MUTED))
            painter.drawText(int(x - 15), int(rect.bottom() + 10), 30, 20,
                             Qt.AlignmentFlag.AlignCenter, str(i * 10))
        painter.drawText(int(rect.right() - 105), self.height() - 4, 105, 18,
                         Qt.AlignmentFlag.AlignRight, "Frequency (Hz)")
        if not self.points:
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter,
                             "No completed calibration points")
            return
        coords = [
            (rect.left() + hz / 50 * rect.width(),
             rect.bottom() - velocity / maximum * rect.height())
            for hz, velocity in self.points
        ]
        painter.setPen(QPen(QColor(RED), 2.5))
        for a, b in zip(coords, coords[1:]):
            painter.drawLine(int(a[0]), int(a[1]), int(b[0]), int(b[1]))
        painter.setBrush(QColor(RED))
        for x, y in coords:
            painter.drawEllipse(int(x - 3), int(y - 3), 6, 6)


class CalibrationPanel(QWidget):
    """Settings > Calibration. All control actions are backend API calls."""

    def __init__(self, hmi):
        super().__init__(hmi)
        self.hmi = hmi
        self.snapshot = {}
        self.runs = []
        self._request_pending = False
        self._runs_pending = False
        self._last_results = None
        self._build()
        self.refresh_runs()

    def _card(self, layout, heading):
        card = QFrame()
        card.setObjectName("calibrationCard")
        content = QVBoxLayout(card)
        content.setSpacing(10)
        content.setContentsMargins(16, 14, 16, 14)
        title = QLabel(heading)
        title.setObjectName("calibrationHeading")
        content.addWidget(title)
        layout.addWidget(card)
        return content

    def _spin(self, layout, label, low, high, value, decimals=1):
        cell = QVBoxLayout()
        cell.addWidget(QLabel(label))
        spin = QDoubleSpinBox()
        spin.setRange(low, high)
        spin.setValue(value)
        spin.setDecimals(decimals)
        spin.setSingleStep(1.0)
        spin.setSuffix(" s" if "duration" in label.lower() or "window" in label.lower() else " Hz")
        cell.addWidget(spin)
        layout.addLayout(cell)
        return spin

    def _build(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        view = QScrollArea()
        view.setWidgetResizable(True)
        view.setFrameShape(QFrame.Shape.NoFrame)
        body = QWidget()
        self.content = QVBoxLayout(body)
        self.content.setContentsMargins(2, 2, 8, 4)
        self.content.setSpacing(12)
        intro = QLabel(
            "Uses the ESP32's existing MANUAL frequency trajectory and filtered Pitot airspeed. "
            "Physical START and STOP are required; the HMI never starts or resets the motor."
        )
        intro.setWordWrap(True)
        self.content.addWidget(intro)

        status = self._card(self.content, "Active ESP32 calibration")
        self.source_label = QLabel("ESP32 status not yet verified")
        self.source_label.setObjectName("calibrationSource")
        self.source_label.setWordWrap(True)
        status.addWidget(self.source_label)

        configuration = self._card(self.content, "Measurement configuration")
        row = QHBoxLayout()
        self.start_hz = self._spin(row, "Start frequency", 0.001, 50, 10)
        self.end_hz = self._spin(row, "End frequency", 0.001, 50, 50)
        self.step_hz = self._spin(row, "Frequency step", 0.001, 50, 5)
        configuration.addLayout(row)
        row = QHBoxLayout()
        self.average_s = self._spin(row, "Average duration", 5, 60, 5)
        self.window_s = self._spin(row, "Stability window", 3, 30, 3)
        configuration.addLayout(row)
        filter_note = QLabel("Pitot uses the current 1 Hz default pressure filter, or the "
                             "confirmed value from Settings > General. Filter and air density "
                             "remain fixed while calibrating.")
        filter_note.setWordWrap(True)
        configuration.addWidget(filter_note)
        buttons = QHBoxLayout()
        self.prepare_button = QPushButton("Prepare Calibration")
        self.prepare_button.setObjectName("primaryButton")
        self.prepare_button.clicked.connect(self.prepare)
        self.cancel_button = QPushButton("Cancel Calibration")
        self.cancel_button.clicked.connect(self.cancel)
        buttons.addWidget(self.prepare_button)
        buttons.addWidget(self.cancel_button)
        buttons.addStretch(1)
        configuration.addLayout(buttons)

        current = self._card(self.content, "Calibration progress")
        self.state_label = QLabel("IDLE")
        self.state_label.setWordWrap(True)
        current.addWidget(self.state_label)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        current.addWidget(self.progress)
        self.live_label = QLabel("Waiting for ESP32 telemetry")
        self.live_label.setWordWrap(True)
        current.addWidget(self.live_label)
        self.stability_label = QLabel("Stability: not available")
        current.addWidget(self.stability_label)

        results = self._card(self.content, "Measured fan characteristic")
        self.plot = FanCharacteristicPlot(self)
        results.addWidget(self.plot)
        self.results_table = QTableWidget(0, 5)
        self.results_table.setHorizontalHeaderLabels(
            ("Requested Hz", "Actual V20 Hz", "Mean m/s", "Std m/s", "Samples")
        )
        self.results_table.horizontalHeader().setStretchLastSection(True)
        self.results_table.setMinimumHeight(235)
        results.addWidget(self.results_table)
        row = QHBoxLayout()
        self.deploy_button = QPushButton("Save & Activate Calibration")
        self.deploy_button.setObjectName("primaryButton")
        self.deploy_button.clicked.connect(self.deploy)
        self.reset_button = QPushButton("Reset Fan Calibration")
        self.reset_button.clicked.connect(self.reset)
        row.addWidget(self.deploy_button)
        row.addWidget(self.reset_button)
        row.addStretch(1)
        results.addLayout(row)

        history = self._card(self.content, "Calibration history and export")
        row = QHBoxLayout()
        self.run_select = QComboBox()
        self.run_select.setMinimumWidth(280)
        self.run_select.currentIndexChanged.connect(self._selection_changed)
        row.addWidget(self.run_select, 1)
        self.refresh_button = QPushButton("Refresh runs")
        self.refresh_button.clicked.connect(self.refresh_runs)
        row.addWidget(self.refresh_button)
        history.addLayout(row)
        self.history_detail = QLabel("No calibration runs loaded")
        self.history_detail.setWordWrap(True)
        history.addWidget(self.history_detail)
        row = QHBoxLayout()
        for filename, title in (
            ("calibration.json", "Metadata JSON"),
            ("raw_samples.csv", "Raw CSV"),
            ("characteristic.csv", "Characteristic CSV"),
            ("inverse_lookup.csv", "Inverse lookup CSV"),
        ):
            button = QPushButton(title)
            button.clicked.connect(lambda checked=False, name=filename: self.download(name))
            button.setObjectName("calibrationDownload")
            row.addWidget(button)
        history.addLayout(row)

        self.message = QLabel("")
        self.message.setWordWrap(True)
        self.message.setObjectName("calibrationMessage")
        self.content.addWidget(self.message)
        self.content.addStretch(1)
        view.setWidget(body)
        outer.addWidget(view)
        self.render({})

    def _report(self, text):
        self.message.setText(str(text))
        if hasattr(self.hmi, "_show_error"):
            self.hmi._show_error(str(text))

    def _post(self, path, body=None, success=None):
        if self._request_pending:
            return
        self._request_pending = True
        self._update_controls()
        def done(data, error):
            self._request_pending = False
            if error:
                self._report(error)
            else:
                self.message.setText(success or "Request accepted")
                if isinstance(data, dict) and "state" in data and "points" in data:
                    self.render(data)
            self._update_controls()
            self.refresh_status()
            self.refresh_runs()
        self.hmi._post("calibration/fan/" + path, body or {}, done)

    def _configuration(self):
        return {
            "start_hz": self.start_hz.value(),
            "end_hz": self.end_hz.value(),
            "step_hz": self.step_hz.value(),
            "average_s": self.average_s.value(),
            "window_s": self.window_s.value(),
        }

    def prepare(self):
        values = self._configuration()
        count = (values["end_hz"] - values["start_hz"]) / values["step_hz"]
        if not (1 <= count <= 31 and math.isclose(count, round(count), abs_tol=1e-6)):
            self._report("Select 2–32 evenly spaced frequencies, including both endpoints.")
            return
        self._post("prepare", values, "Prepared: wait for ARMED, then press physical START")

    def cancel(self):
        self._post("cancel", {}, "Calibration cancelled; use physical STOP when required")

    def deploy(self):
        identifier = self.run_select.currentData() or self.snapshot.get("run_id")
        if not identifier or not RUN_ID.fullmatch(str(identifier)):
            self._report("Select a completed calibration before activation.")
            return
        run = next((r for r in self.runs if r.get("id") == identifier), {})
        if run and run.get("completion_status") != "COMPLETED":
            self._report("Only a completed calibration can be activated.")
            return
        self._post(f"runs/{quote(identifier, safe='')}/deploy", {}, "ESP32 calibration activated and verified")

    def reset(self):
        result = QMessageBox.question(
            self, "Reset fan calibration",
            "Remove the active fan characteristic and restore the DEFAULT calculated motor model?\n"
            "Historical measurement files will be preserved.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if result == QMessageBox.StandardButton.Yes:
            self._post("reset", {}, "ESP32 calibration reset; DEFAULT model selected")

    def refresh_status(self):
        if not self.isVisible():
            return
        self.hmi._get("calibration/fan/status",
                      lambda data, error: self.render(data) if not error else self.message.setText(str(error)))

    def refresh_runs(self):
        if self._runs_pending:
            return
        self._runs_pending = True
        def done(data, error):
            self._runs_pending = False
            if error:
                self.message.setText(f"Could not load calibration history: {error}")
                return
            if not isinstance(data, list):
                self.message.setText("Invalid calibration history response")
                return
            self.runs = data
            previous = self.run_select.currentData()
            self.run_select.blockSignals(True)
            self.run_select.clear()
            for run in data:
                identifier = run.get("id", "")
                if RUN_ID.fullmatch(identifier):
                    self.run_select.addItem(
                        f"{run.get('timestamp', 'Unknown date')} · "
                        f"{run.get('completion_status', 'UNKNOWN')} · "
                        f"{run.get('deployment_status', 'NOT DEPLOYED')}",
                        identifier,
                    )
            if previous:
                index = self.run_select.findData(previous)
                if index >= 0:
                    self.run_select.setCurrentIndex(index)
            self.run_select.blockSignals(False)
            self._selection_changed()
            self._update_controls()
            self.render(self.snapshot)
        self.hmi._get("calibration/fan/runs", done)

    def _selection_changed(self, *args):
        identifier = self.run_select.currentData()
        run = next((r for r in self.runs if r.get("id") == identifier), None)
        self.history_detail.setText(
            f"Run: {identifier}\n"
            f"Status: {run.get('completion_status', 'UNKNOWN')} · "
            f"Deployment: {run.get('deployment_status', 'UNKNOWN')} · "
            f"CRC32: {run.get('crc32', '—')}\n"
            f"Filter: {number(run.get('filter_hz'), 1)} Hz · "
            f"Density: {number(run.get('air_density'), 3)} kg/m³"
            if run else "No calibration runs available"
        )
        self._update_controls()

    def download(self, filename):
        identifier = self.run_select.currentData()
        if filename not in FILES or not identifier or not RUN_ID.fullmatch(identifier):
            self._report("Select a calibration run before downloading files.")
            return
        extension = "JSON (*.json)" if filename.endswith(".json") else "CSV (*.csv)"
        destination, _ = QFileDialog.getSaveFileName(
            self, "Export fan calibration",
            str(Path.home() / f"{identifier}_{filename}"), extension,
        )
        if not destination:
            return
        save = QSaveFile(destination)
        if not save.open(QIODevice.OpenModeFlag.WriteOnly):
            self._report("Cannot create download destination: " + save.errorString())
            return
        url = QUrl(
            f"http://127.0.0.1:8000/api/calibration/fan/runs/"
            f"{quote(identifier, safe='')}/files/{quote(filename, safe='')}"
        )
        reply = self.hmi.network.get(QNetworkRequest(url))
        problem = {"error": None}

        def write_available():
            chunk = bytes(reply.readAll())
            if chunk and save.write(chunk) != len(chunk):
                problem["error"] = save.errorString()

        def finish():
            write_available()
            status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute)
            error = problem["error"]
            if reply.error() != QNetworkReply.NetworkError.NoError:
                error = error or reply.errorString()
            elif status and int(status) >= 400:
                error = error or f"HTTP {int(status)}"
            reply.deleteLater()
            if error:
                save.cancelWriting()
                self._report(f"Calibration download failed: {error}")
            elif not save.commit():
                self._report("Saving calibration failed: " + save.errorString())
            else:
                self.message.setText(f"Saved calibration file: {destination}")

        reply.readyRead.connect(write_available)
        reply.finished.connect(finish)

    def _update_controls(self):
        active = bool(self.snapshot.get("active"))
        state = self.snapshot.get("state", "IDLE")
        disabled = active or self._request_pending
        for spin in (self.start_hz, self.end_hz, self.step_hz, self.average_s, self.window_s):
            spin.setEnabled(not disabled)
        self.prepare_button.setEnabled(not disabled)
        self.cancel_button.setEnabled(active and not self._request_pending)
        self.reset_button.setEnabled(not disabled)
        selected_id = self.run_select.currentData()
        selected = next((r for r in self.runs if r.get("id") == selected_id), {})
        self.deploy_button.setEnabled(
            not disabled and (
                selected.get("completion_status") == "COMPLETED" or
                (not selected_id and state == "COMPLETED" and bool(self.snapshot.get("run_id")))
            )
        )

    def render(self, status):
        if not isinstance(status, dict):
            return
        self.snapshot = status
        esp = status.get("esp32") or {}
        source = esp.get("cal_source", "UNKNOWN")
        stale = esp.get("stale", True) or not esp.get("connected", False)
        valid = bool(esp.get("cal_valid") == 1)
        crc = esp.get("cal_crc")
        verified = next(
            (r for r in self.runs if r.get("deployment_status") == "VERIFIED"
             and r.get("crc32") == crc),
            None,
        ) if valid and not stale else None
        self.source_label.setText(
            "ESP32 disconnected or telemetry stale; calibration source unknown."
            if stale else
            f"Source: {source} · Valid: {'YES' if valid else 'NO'} · "
            f"Version: {esp.get('cal_version', '—')} · "
            f"Points: {esp.get('cal_points', '—')} · "
            f"CRC32: {crc if crc is not None else '—'} · "
            f"Saved: {verified.get('timestamp', '—') if verified else 'unverified'}"
        )
        state = status.get("state", "IDLE")
        index = status.get("index", 0)
        total = status.get("total", 0)
        self.state_label.setText(
            f"{state} · point {index}/{total} · "
            f"{status.get('message', '')}"
        )
        self.progress.setValue(max(0, min(100, int(status.get("progress") or 0))))
        self.live_label.setText(
            f"Requested: {number(status.get('target_hz'), 2)} Hz · "
            f"Applied: {number(esp.get('applied_hz'), 2)} Hz · "
            f"Actual V20: {number(esp.get('actual_hz'), 2)} Hz · "
            f"Pitot: {number(esp.get('pv'), 3)} m/s"
        )
        stability = status.get("stability") or {}
        self.stability_label.setText(
            f"Stability: σ = {number(stability.get('std'), 3)} m/s · "
            f"trend = {number(stability.get('trend'), 3)} m/s²"
        )
        points = status.get("points") or []
        self.plot.set_points(points)
        signature = repr(points)
        if signature != self._last_results:
            self._last_results = signature
            self.results_table.setRowCount(len(points))
            for row, point in enumerate(points):
                values = (
                    number(point.get("target_hz"), 2),
                    number(point.get("mean_actual_hz"), 2),
                    number(point.get("mean_pv"), 3),
                    number(point.get("std_pv"), 3),
                    str(point.get("samples", "—")),
                )
                for col, value in enumerate(values):
                    self.results_table.setItem(row, col, QTableWidgetItem(value))
        self._update_controls()
