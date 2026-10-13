import json
import math
import re
import sys
import time
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import Qt, QTimer, QUrl
from PySide6.QtGui import QColor, QFont, QPainter, QPainterPath, QPen
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest
from PySide6.QtSvgWidgets import QSvgWidget
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QFrame,
    QGraphicsDropShadowEffect,
    QGridLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)


RED = "#c8291e"
INK = "#181818"
MUTED = "#747474"
LINE = "#dededb"
CREAM = "#f6f6f4"
PANEL = "#1e2023"

PAGES = (
    ("overview", "◫", "Overview", "Tunnel overview", "Live flow data from the USB-connected pitot measurement node."),
    ("raw", "≡", "Raw Data", "Raw sensor data", "Inspect the Arduino Uno / ASPD-4525 telemetry frame."),
    ("cp", "⌁", "Cp Analysis", "Cp analysis", "Multi-point pressure distribution is the next measurement subsystem."),
    ("measurements", "▤", "Measurements", "Measurements", "Record and review real pitot-tube measurements."),
    ("storage", "▱", "Storage", "Storage & export", "Keep data local and export complete runs when needed."),
    ("settings", "⚙", "Settings", "System settings", "Current acquisition configuration and hardware status."),
)


class Card(QFrame):
    def __init__(self, parent=None, accent=False):
        super().__init__(parent)
        self.setObjectName("card")
        if accent:
            self.setProperty("accent", True)
        shadow = QGraphicsDropShadowEffect(self)
        shadow.setBlurRadius(18)
        shadow.setOffset(0, 3)
        shadow.setColor(QColor(0, 0, 0, 20))
        self.setGraphicsEffect(shadow)


class FlowChart(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(180)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.points = []
        self.y_max = 45.0

    def set_points(self, points, y_max):
        self.points = points[-600:]
        self.y_max = max(1.0, float(y_max))
        self.update()

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = self.rect().adjusted(60, 12, -16, -32)
        if rect.width() <= 0 or rect.height() <= 0:
            return
        painter.setFont(QFont("Arial", 9))
        painter.setPen(QPen(QColor(LINE), 1))
        for step in range(5):
            y = rect.top() + rect.height() * step / 4
            painter.drawLine(int(rect.left()), int(y), int(rect.right()), int(y))
            value = self.y_max * (1 - step / 4)
            painter.setPen(QColor("#666662"))
            painter.drawText(0, int(y - 9), 49, 18, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, f"{value:.0f}")
            painter.setPen(QPen(QColor(LINE), 1))
        for step in range(5):
            x = rect.left() + rect.width() * step / 4
            painter.setPen(QColor("#777772"))
            painter.drawText(int(x - 20), self.height() - 24, 40, 16, Qt.AlignmentFlag.AlignCenter, f"-{60 - step * 15}s")
        if not self.points:
            painter.setPen(QColor("#7f7f7a"))
            painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, "Waiting for valid flow data")
            return

        def path_for(key):
            points = []
            newest = self.points[-1]["t"]
            for item in self.points:
                value = item.get(key)
                if value is None:
                    points.append(None)
                    continue
                x = rect.left() + rect.width() * max(0.0, min(1.0, 1 - (newest - item["t"]) / 60))
                y = rect.bottom() - rect.height() * max(0.0, min(1.0, float(value) / self.y_max))
                points.append((x, y))
            return points

        painter.save()
        painter.setClipRect(rect)
        for key, color, width, dashed in (
            ("mean", "#2f3338", 2, True),
            ("speed", RED, 2.6, False),
        ):
            points = path_for(key)
            pen = QPen(QColor(color), width)
            if dashed:
                pen.setStyle(Qt.PenStyle.DashLine)
            pen.setCapStyle(Qt.PenCapStyle.RoundCap)
            pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
            painter.setPen(pen)
            previous = None
            for current in points:
                if current is None:
                    previous = None
                elif previous is not None:
                    painter.drawLine(int(previous[0]), int(previous[1]), int(current[0]), int(current[1]))
                    previous = current
                else:
                    previous = current
        painter.restore()


class AirfoilDiagram(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(82)
        self.setMaximumHeight(112)
        self.setMinimumWidth(240)

    def paintEvent(self, _event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        width, height = self.width(), self.height()
        for start, first, second, end in (
            ((0, .18), (.28, .22), (.48, .10), (1, .18)),
            ((0, .36), (.30, .37), (.48, .27), (1, .36)),
            ((0, .70), (.32, .72), (.50, .84), (1, .70)),
            ((0, .89), (.36, .92), (.66, .88), (1, .89)),
        ):
            path = QPainterPath()
            path.moveTo(start[0] * width, start[1] * height)
            path.cubicTo(first[0] * width, first[1] * height,
                         second[0] * width, second[1] * height,
                         end[0] * width, end[1] * height)
            painter.setPen(QPen(QColor("#d5d5d1"), 1))
            painter.drawPath(path)

        foil = QPainterPath()
        foil.moveTo(.12 * width, .59 * height)
        foil.cubicTo(.26 * width, .20 * height, .73 * width, .40 * height, .91 * width, .56 * height)
        foil.cubicTo(.68 * width, .67 * height, .35 * width, .77 * height, .12 * width, .59 * height)
        painter.setPen(QPen(QColor(RED), 1.5))
        painter.setBrush(QColor(200, 41, 30, 24))
        painter.drawPath(foil)
        painter.setPen(QPen(QColor("#92928d"), 1, Qt.PenStyle.DashLine))
        painter.drawLine(int(.06 * width), int(.60 * height), int(.95 * width), int(.60 * height))


class HMIWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Avans Windtunnel · HMI")
        self.resize(1600, 950)
        self.setMinimumSize(1120, 700)
        self.network = QNetworkAccessManager(self)
        self.config = {}
        self.state = {}
        self.page_key = "overview"
        self.poll_busy = False
        self.history = []
        self.last_events = None
        self.last_refresh = 0.0
        self.last_sample_timestamp = None
        self._build_ui()
        self._apply_style()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._poll)
        self.timer.start(100)
        self.clock_timer = QTimer(self)
        self.clock_timer.timeout.connect(lambda: self.clock.setText(datetime.now().strftime("%H:%M:%S")))
        self.clock_timer.start(1000)
        self._get("config", self._on_config)
        self._get("status", self._on_status)

    def _build_ui(self):
        root = QWidget()
        self.setCentralWidget(root)
        layout = QHBoxLayout(root)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self.navigation = QWidget()
        self.navigation.setObjectName("navigation")
        self.navigation.setFixedWidth(220)
        nav_layout = QVBoxLayout(self.navigation)
        nav_layout.setContentsMargins(14, 20, 14, 15)
        nav_layout.setSpacing(8)
        logo = QSvgWidget(str(Path(__file__).resolve().parent / "static" / "avans-wordmark.svg"))
        logo.setFixedSize(132, 52)
        brand = QLabel("WINDTUNNEL\nAEROSPACE ENGINEERING")
        brand.setObjectName("brand")
        nav_layout.addWidget(logo)
        nav_layout.addWidget(brand)
        nav_layout.addSpacing(18)
        workspace_label = QLabel("☰   WORKSPACE")
        workspace_label.setObjectName("navSection")
        nav_layout.addWidget(workspace_label)
        self.nav_buttons = {}
        for key, icon, title, _heading, _description in PAGES:
            button = QPushButton(f"{icon}    {title}")
            button.setObjectName("navButton")
            button.setCheckable(True)
            button.clicked.connect(lambda checked=False, name=key: self._show_page(name))
            nav_layout.addWidget(button)
            self.nav_buttons[key] = button
        nav_layout.addStretch(1)
        self.exit_button = QPushButton("⏻    Exit HMI")
        self.exit_button.setObjectName("exitButton")
        self.exit_button.clicked.connect(self.close)
        nav_layout.addWidget(self.exit_button)
        nav_layout.addWidget(QLabel("Avans windtunnel test facility\nSUPERVISORY HMI · 2026"))
        layout.addWidget(self.navigation)

        center = QWidget()
        center.setObjectName("center")
        center_layout = QVBoxLayout(center)
        center_layout.setContentsMargins(0, 0, 0, 0)
        center_layout.setSpacing(0)
        header = QWidget()
        header.setObjectName("header")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(22, 0, 22, 0)
        self.connection = QLabel("CONNECTING")
        self.connection.setObjectName("tagAmber")
        self.clock = QLabel("--:--:--")
        header_layout.addWidget(QLabel("AVANS HOGESCHOOL   /   AEROSPACE ENGINEERING   /   WINDTUNNEL 2026"))
        header_layout.addStretch(1)
        header_layout.addWidget(self.connection)
        header_layout.addWidget(self.clock)
        center_layout.addWidget(header)

        main = QWidget()
        main_layout = QVBoxLayout(main)
        main_layout.setContentsMargins(20, 14, 20, 8)
        main_layout.setSpacing(12)
        heading = QHBoxLayout()
        heading_left = QVBoxLayout()
        self.section_number = QLabel("FLIGHT DECK / 01")
        self.section_number.setObjectName("eyebrow")
        self.page_title = QLabel("Tunnel overview")
        self.page_title.setObjectName("pageTitle")
        self.page_description = QLabel(PAGES[0][4])
        self.page_description.setObjectName("muted")
        heading_left.addWidget(self.section_number)
        heading_left.addWidget(self.page_title)
        heading_left.addWidget(self.page_description)
        heading.addLayout(heading_left)
        heading.addStretch(1)
        self.mode_tag = QLabel("WAITING FOR ARDUINO")
        self.mode_tag.setObjectName("tagAmber")
        heading.addWidget(self.mode_tag, 0, Qt.AlignmentFlag.AlignVCenter)
        main_layout.addLayout(heading)
        self.pages = QStackedWidget()
        self._build_overview()
        self._build_raw()
        self._build_cp()
        self._build_measurements()
        self._build_storage()
        self._build_settings()
        main_layout.addWidget(self.pages, 1)
        footer = QLabel("AVANS HOGESCHOOL   /   WINDTUNNEL 2026                         AEROSPACE ENGINEERING · LOCAL DATA ACQUISITION")
        footer.setObjectName("footer")
        main_layout.addWidget(footer)
        center_layout.addWidget(main, 1)
        layout.addWidget(center, 1)

        self.right = QWidget()
        self.right.setObjectName("rightSidebar")
        self.right.setFixedWidth(304)
        right_layout = QVBoxLayout(self.right)
        right_layout.setContentsMargins(18, 20, 18, 18)
        right_layout.setSpacing(10)
        eyebrow = QLabel("SYSTEM & DEVELOPMENT")
        eyebrow.setObjectName("eyebrow")
        right_layout.addWidget(eyebrow)
        tunnel_heading = self._heading("Wind tunnel")
        tunnel_heading.setObjectName("sidebarTitle")
        right_layout.addWidget(tunnel_heading)
        tunnel_status = QHBoxLayout()
        tunnel_badge = QLabel("IN DEVELOPMENT")
        tunnel_badge.setObjectName("tagAmber")
        tunnel_status.addWidget(tunnel_badge)
        tunnel_status.addWidget(QLabel("Fan / VFD control"))
        tunnel_status.addStretch(1)
        right_layout.addLayout(tunnel_status)
        control = QWidget()
        control.setObjectName("development")
        control_layout = QVBoxLayout(control)
        control_layout.setContentsMargins(10, 9, 10, 9)
        control_layout.setSpacing(3)
        control_label = QLabel("AIRSPEED SETPOINT")
        control_label.setObjectName("smallLabel")
        control_state = QLabel("IN DEVELOPMENT")
        control_state.setObjectName("developmentText")
        control_note = QLabel("Manual fan operation for now")
        control_note.setObjectName("hint")
        control_layout.addWidget(control_label)
        control_layout.addWidget(control_state)
        control_layout.addWidget(control_note)
        right_layout.addWidget(control)
        safety_note = QLabel("Data acquisition is active.\nFan control remains manual until commissioned.")
        safety_note.setObjectName("safetyNote")
        safety_note.setAlignment(Qt.AlignmentFlag.AlignCenter)
        right_layout.addWidget(safety_note)
        readings = QHBoxLayout()
        self.side_speed = self._side_reading(readings, "LIVE AIRSPEED")
        self.side_stability = self._side_reading(readings, "FLOW STABILITY")
        right_layout.addLayout(readings)
        self.health_card = Card()
        self.health_card.setGraphicsEffect(None)
        health_layout = QVBoxLayout(self.health_card)
        health_layout.addWidget(self._heading("System health"))
        self.health_labels = {}
        for key, label in (
            ("host", "Host / Raspberry Pi"), ("arduino", "Arduino Uno"), ("serial", "USB serial"),
            ("pitot", "Pitot sensor"), ("pressure_sensors", "Cp pressure taps"),
            ("vfd", "Fan / VFD"), ("logger", "Measurement logger"), ("storage", "Storage"),
        ):
            row = QHBoxLayout()
            row.addWidget(QLabel(label))
            value = QLabel("CHECKING")
            value.setObjectName("healthValue")
            value.setAlignment(Qt.AlignmentFlag.AlignRight)
            row.addWidget(value)
            health_layout.addLayout(row)
            self.health_labels[key] = value
        self.usb_health = QLabel("USB storage  ·  CHECKING")
        health_layout.addWidget(self.usb_health)
        right_layout.addWidget(self.health_card)
        warning_card = Card()
        warning_card.setGraphicsEffect(None)
        warning_card.setProperty("warning", True)
        warning_layout = QVBoxLayout(warning_card)
        warning_layout.addWidget(self._heading("Warnings & development"))
        self.warnings = QLabel("Waiting for Arduino pitot data")
        self.warnings.setWordWrap(True)
        warning_layout.addWidget(self.warnings)
        right_layout.addWidget(warning_card)
        event_card = Card()
        event_card.setGraphicsEffect(None)
        event_layout = QVBoxLayout(event_card)
        event_layout.addWidget(self._heading("Event log · latest"))
        self.event_log = QLabel("Waiting for events")
        self.event_log.setWordWrap(True)
        self.event_log.setAlignment(Qt.AlignmentFlag.AlignTop)
        event_layout.addWidget(self.event_log)
        right_layout.addWidget(event_card, 1)
        layout.addWidget(self.right)
        self._show_page("overview")

    def _heading(self, text):
        label = QLabel(text)
        label.setObjectName("cardHeading")
        return label

    def _side_reading(self, layout, title):
        box = QVBoxLayout()
        label = QLabel(title)
        label.setObjectName("smallLabel")
        value = QLabel("—")
        value.setObjectName("sideValue")
        box.addWidget(label)
        box.addWidget(value)
        layout.addLayout(box)
        return value

    def _card(self, layout, title=None, subtitle=None):
        card = Card()
        box = QVBoxLayout(card)
        box.setContentsMargins(16, 14, 16, 14)
        box.setSpacing(8)
        if title:
            box.addWidget(self._heading(title))
        if subtitle:
            label = QLabel(subtitle)
            label.setObjectName("muted")
            box.addWidget(label)
        layout.addWidget(card)
        return card, box

    def _metric(self, parent_layout, title, unit, key, accent=False):
        card = Card(accent=accent)
        card.setMinimumHeight(98)
        box = QVBoxLayout(card)
        box.setContentsMargins(14, 12, 14, 10)
        label = QLabel(title.upper())
        label.setObjectName("smallLabel")
        value = QLabel(f"— {unit}")
        value.setObjectName("metricValueRed" if accent else "metricValue")
        note = QLabel("")
        note.setObjectName("hint")
        box.addWidget(label)
        box.addWidget(value)
        box.addWidget(note)
        parent_layout.addWidget(card)
        setattr(self, key, value)
        setattr(self, f"{key}_note", note)

    def _build_overview(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)
        self.metrics_widget = QWidget()
        metrics = QHBoxLayout(self.metrics_widget)
        metrics.setContentsMargins(0, 0, 0, 0)
        metrics.setSpacing(12)
        self._metric(metrics, "Airspeed", "m/s", "airspeed", True)
        self._metric(metrics, "Dynamic pressure", "Pa", "dynamic")
        self._metric(metrics, "Flow stability", "%", "stability")
        self._metric(metrics, "Temperature", "°C", "temperature")
        layout.addWidget(self.metrics_widget)
        self.chart_card = Card()
        self.chart_card.setObjectName("chartCard")
        chart_layout = QVBoxLayout(self.chart_card)
        chart_layout.setContentsMargins(14, 12, 14, 10)
        chart_layout.setSpacing(5)
        chart_heading = QHBoxLayout()
        chart_title = QVBoxLayout()
        chart_title.setSpacing(1)
        chart_title.addWidget(self._heading("Flow velocity"))
        chart_subtitle = QLabel("Live pitot-tube airspeed with 5 s rolling mean")
        chart_subtitle.setObjectName("muted")
        chart_title.addWidget(chart_subtitle)
        chart_heading.addLayout(chart_title)
        chart_heading.addStretch(1)
        legend = QHBoxLayout()
        legend.setSpacing(5)
        actual_line = QFrame()
        actual_line.setObjectName("actualLegendLine")
        actual_line.setFixedSize(18, 3)
        legend.addWidget(actual_line)
        actual_label = QLabel("Actual")
        actual_label.setObjectName("chartLegendText")
        legend.addWidget(actual_label)
        mean_line = QFrame()
        mean_line.setObjectName("meanLegendLine")
        mean_line.setFixedSize(18, 2)
        legend.addSpacing(5)
        legend.addWidget(mean_line)
        mean_label = QLabel("5 s mean")
        mean_label.setObjectName("chartLegendText")
        legend.addWidget(mean_label)
        latest = QLabel("LAST 60 S")
        latest.setObjectName("tagMuted")
        legend.addSpacing(5)
        legend.addWidget(latest)
        chart_heading.addLayout(legend)
        chart_layout.addLayout(chart_heading)
        self.chart = FlowChart()
        chart_layout.addWidget(self.chart, 1)
        self.stream_note = QLabel("Waiting for telemetry")
        self.stream_note.setObjectName("hint")
        chart_layout.addWidget(self.stream_note)

        self.overview_bottom = QWidget()
        bottom = QHBoxLayout(self.overview_bottom)
        bottom.setContentsMargins(0, 0, 0, 0)
        bottom.setSpacing(12)
        self.recording_card = Card()
        recording = QVBoxLayout(self.recording_card)
        recording.setContentsMargins(16, 14, 16, 14)
        recording.setSpacing(8)
        recording_heading = QHBoxLayout()
        recording_eyebrow = QLabel("EXPERIMENT MANAGER")
        recording_eyebrow.setObjectName("eyebrow")
        recording_heading.addWidget(recording_eyebrow)
        self.measurement_badge = QLabel("IDLE")
        self.measurement_badge.setObjectName("tagMuted")
        recording_heading.addStretch(1)
        recording_heading.addWidget(self.measurement_badge)
        recording.addLayout(recording_heading)
        recording.addWidget(self._heading("Measurement recording"))
        bottom.addWidget(self.recording_card, 6)
        recording.addWidget(QLabel("Measurement name"))
        self.measurement_name = QLineEdit()
        self.measurement_name.setPlaceholderText("e.g. NACA0012_AOA5_Run01")
        self.measurement_name.setMaxLength(80)
        recording.addWidget(self.measurement_name)
        recording.addWidget(QLabel("Recording is independent of tunnel operation."))
        stats = QHBoxLayout()
        self.elapsed = QLabel("00:00")
        self.samples = QLabel("0")
        self.sample_rate = QLabel("— Hz")
        for title, value in (("ELAPSED", self.elapsed), ("SAMPLES", self.samples), ("LOG RATE", self.sample_rate)):
            column = QVBoxLayout()
            column.addWidget(QLabel(title))
            column.addWidget(value)
            stats.addLayout(column)
        recording.addLayout(stats)
        buttons = QHBoxLayout()
        self.record_start = QPushButton("● Start measurement")
        self.record_start.setObjectName("primaryButton")
        self.record_start.clicked.connect(self._start_recording)
        self.record_stop = QPushButton("■ Stop & save")
        self.record_stop.clicked.connect(self._stop_recording)
        buttons.addWidget(self.record_start)
        buttons.addWidget(self.record_stop)
        recording.addLayout(buttons)
        self.file_status = QLabel("Connect Arduino Uno via USB to record.")
        self.file_status.setObjectName("hint")
        recording.addWidget(self.file_status)

        self.operating_card = Card()
        operating = QVBoxLayout(self.operating_card)
        operating.setContentsMargins(16, 14, 16, 14)
        operating.setSpacing(7)
        eyebrow = QLabel("OPERATING POINT")
        eyebrow.setObjectName("eyebrow")
        operating.addWidget(eyebrow)
        operating.addWidget(self._heading("Current pitot measurement"))
        operating.addWidget(AirfoilDiagram())
        self.operating_grid = QGridLayout()
        self.operating_grid.setHorizontalSpacing(14)
        self.operating_grid.setVerticalSpacing(6)
        self.operating_values = {}
        fields = (
            ("5 S MEAN", "— m/s"),
            ("VARIATION σ", "— m/s"),
            ("5 S MIN / MAX", "—"),
            ("ANGLE OF ATTACK", "IN DEVELOPMENT"),
            ("FAN / VFD CONTROL", "IN DEVELOPMENT"),
            ("REYNOLDS NUMBER", "IN DEVELOPMENT"),
        )
        for index, (title, text) in enumerate(fields):
            cell = QVBoxLayout()
            label = QLabel(title)
            label.setObjectName("smallLabel")
            value = QLabel(text)
            value.setObjectName("developmentText" if text == "IN DEVELOPMENT" else "operatingValue")
            if text == "IN DEVELOPMENT":
                value.setSizePolicy(QSizePolicy.Policy.Maximum, QSizePolicy.Policy.Fixed)
            cell.addWidget(label)
            cell.addWidget(value)
            self.operating_grid.addLayout(cell, index // 2, index % 2)
            self.operating_values[title] = value
        operating.addLayout(self.operating_grid)
        hint = QLabel("Live values are derived from the single USB-connected pitot sensor.")
        hint.setObjectName("hint")
        operating.addWidget(hint)
        self.flow_summary = self.operating_values["5 S MEAN"]
        self.flow_summary_std = self.operating_values["VARIATION σ"]
        self.flow_summary_range = self.operating_values["5 S MIN / MAX"]
        bottom.addWidget(self.operating_card, 5)
        layout.addWidget(self.overview_bottom, 4)
        layout.setStretch(1, 3)
        layout.setStretch(2, 4)
        self.pages.addWidget(page)

    def _build_raw(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        self.raw_empty = QLabel("USB\n\nNO SENSOR DATA\nArduino Uno not connected\nConnect the Arduino Uno via USB to start live pitot measurements.")
        self.raw_empty.setObjectName("emptyState")
        self.raw_table = self._table(("Channel", "Raw value", "Calibrated", "Unit", "Host timestamp", "Status"))
        self.raw_json = QPlainTextEdit()
        self.raw_json.setReadOnly(True)
        self.raw_json.setMaximumHeight(220)
        self.raw_table.hide()
        self.raw_json.hide()
        layout.addWidget(self.raw_empty, 1)
        self.raw_card, raw_box = self._card(layout, "Sensor telemetry", "Live Arduino Uno / ASPD-4525 pitot telemetry")
        raw_box.addWidget(self.raw_table)
        self.raw_json_card, json_box = self._card(layout, "Complete incoming frame")
        json_box.addWidget(self.raw_json)
        self.pages.addWidget(page)

    def _build_cp(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        empty = QLabel("Cp\n\nIN DEVELOPMENT\nPressure tap system not connected\nCp analysis will become available when the multi-point pressure manifold and channel switching hardware are installed.\n\nPRESSURE SYSTEM OFFLINE")
        empty.setObjectName("emptyState")
        layout.addWidget(empty, 1)
        self.pages.addWidget(page)

    def _table(self, headers):
        table = QTableWidget(0, len(headers))
        table.setHorizontalHeaderLabels(headers)
        table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        table.setAlternatingRowColors(True)
        table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        return table

    def _build_measurements(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        card, box = self._card(layout, "Measurement library", "Stored locally · full CSV and JSON preserved per run")
        top = QHBoxLayout()
        top.addStretch(1)
        refresh = QPushButton("↻ Refresh")
        refresh.clicked.connect(self._refresh_data)
        top.addWidget(refresh)
        box.addLayout(top)
        self.measurements_table = self._table(("Experiment", "Timestamp", "Duration", "Avg. m/s", "Samples", "Export"))
        self.measurements_table.cellClicked.connect(self._select_run)
        box.addWidget(self.measurements_table)
        self.measurements_empty = QLabel("NO MEASUREMENTS YET\nNo recorded datasets are available")
        self.measurements_empty.setObjectName("emptyState")
        box.addWidget(self.measurements_empty)
        detail_card, detail = self._card(layout, "Run details", "Select a measurement to inspect its metadata and samples.")
        self.measurement_detail = QPlainTextEdit()
        self.measurement_detail.setReadOnly(True)
        self.measurement_detail.setMinimumHeight(180)
        detail.addWidget(self.measurement_detail)
        self.pages.addWidget(page)

    def _build_storage(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        metrics = QHBoxLayout()
        self._metric(metrics, "Local free space", "", "local_space")
        self._metric(metrics, "USB storage", "", "usb_space")
        self._metric(metrics, "Awaiting export", "", "unexported")
        layout.addLayout(metrics)
        card, box = self._card(layout, "Export measurements", "Copy complete experiment folders to USB. Local copies are always retained.")
        box.addWidget(QLabel("Select measurement"))
        self.export_select = QComboBox()
        box.addWidget(self.export_select)
        buttons = QHBoxLayout()
        for label, handler in (
            ("Export selected", self._export_selected), ("Export all", self._export_all),
            ("Safe eject", self._eject), ("Reconnect simulated USB", self._reconnect_usb),
        ):
            button = QPushButton(label)
            button.clicked.connect(handler)
            buttons.addWidget(button)
            setattr(self, f"button_{handler.__name__}", button)
        box.addLayout(buttons)
        self.export_progress = QProgressBar()
        box.addWidget(self.export_progress)
        self.export_message = QLabel("Ready to export")
        box.addWidget(self.export_message)
        layout.addWidget(card)
        layout.addStretch(1)
        self.pages.addWidget(page)

    def _build_settings(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        card, box = self._card(layout, "System configuration", "Live acquisition settings for the Raspberry Pi HMI.")
        toggle_row = QHBoxLayout()
        toggle_row.addWidget(QLabel("5 s flow statistics\nFlow stability, rolling mean, standard deviation and min/max."))
        self.flow_toggle = QCheckBox()
        self.flow_toggle.toggled.connect(self._toggle_flow_metrics)
        toggle_row.addWidget(self.flow_toggle)
        box.addLayout(toggle_row)
        self.settings_grid = QGridLayout()
        box.addLayout(self.settings_grid)
        box.addWidget(self._heading("Pressure-tap system"))
        box.addWidget(QLabel("IN DEVELOPMENT · Multi-point Cp hardware is not connected yet."))
        box.addWidget(QLabel("Current hardware: Arduino Uno + ASPD-4525 differential pressure sensor over USB serial. Fan/VFD control and angle-of-attack sensing remain disabled until commissioned."))
        layout.addWidget(card)
        layout.addStretch(1)
        self.pages.addWidget(page)

    def _apply_style(self):
        self.setStyleSheet(f"""
            QWidget {{ color:{INK}; font-family:Arial; font-size:14px; }}
            QMainWindow, #center {{ background:{CREAM}; }}
            #navigation {{ background:{RED}; color:white; }}
            #navigation QLabel {{ color:white; }}
            #brandLogo {{ font-size:17px; font-weight:800; padding:4px 7px; }}
            #brand {{ font-size:12px; font-weight:800; letter-spacing:1px; padding:4px 7px 18px; border-bottom:1px solid rgba(255,255,255,.35); }}
            #navSection {{ color:rgba(255,255,255,.78); font-size:9px; letter-spacing:1px; padding:8px 10px; }}
            QPushButton {{ background:white; color:{INK}; border:1px solid #c8c8c4; border-radius:4px; padding:8px 12px; font-weight:600; }}
            QPushButton:hover {{ background:#f1f1ee; border-color:#adada8; }}
            QPushButton:disabled {{ color:#999; }}
            #navigation QPushButton {{ background:transparent; color:rgba(255,255,255,.86); border:0; border-radius:4px; text-align:left; padding:11px 10px; }}
            #navigation QPushButton:hover {{ background:rgba(255,255,255,.11); }}
            #navigation QPushButton:checked {{ background:white; color:{RED}; font-weight:700; }}
            #exitButton {{ background:rgba(255,255,255,.06); border:1px solid rgba(255,255,255,.25); text-align:left; }}
            #exitButton:hover {{ background:white; color:#202225; }}
            #primaryButton {{ background:{RED}; border-color:{RED}; color:white; font-weight:700; }}
            #primaryButton:hover {{ background:#9f2018; border-color:#9f2018; }}
            #header {{ background:white; border-bottom:1px solid {LINE}; min-height:50px; max-height:50px; }}
            #header QLabel {{ color:#777; font-size:9px; letter-spacing:1px; }}
            #pageTitle {{ font-size:25px; font-weight:700; }}
            #eyebrow {{ color:{RED}; font-size:9px; font-weight:800; letter-spacing:1.5px; }}
            #muted {{ color:{MUTED}; font-size:11px; }}
            #hint {{ color:#84847f; font-size:10px; }}
            #footer {{ color:#8a8a85; font-size:8px; letter-spacing:1px; padding-top:5px; }}
            #card {{ background:white; border:1px solid {LINE}; border-radius:6px; }}
            #card[accent="true"] {{ border-color:#d4c0bd; border-left:4px solid {RED}; }}
            #chartCard {{ border-top:3px solid {RED}; }}
            #cardHeading {{ font-size:15px; font-weight:700; }}
            #smallLabel {{ color:#727272; font-size:8px; font-weight:800; letter-spacing:1px; }}
            #operatingValue {{ font-size:14px; font-weight:700; }}
            #metricValue, #metricValueRed {{ font-size:30px; font-weight:700; }}
            #metricValueRed {{ color:{RED}; }}
            #tagAmber, #tagMuted {{ padding:4px 7px; border:1px solid #ead0a2; border-radius:3px; background:#fff1d8; color:#975900; font-size:8px; font-weight:800; }}
            #tagMuted {{ background:#efefec; border-color:#d7d7d2; color:#6f6f6c; }}
            #rightSidebar {{ background:{PANEL}; color:#f5f5f5; border-left:1px solid #303236; }}
            #rightSidebar QLabel {{ color:#eee; }}
            #rightSidebar #card {{ background:transparent; border:0; border-top:1px solid #3a3d42; border-radius:0; }}
            #rightSidebar #card[warning="true"] {{ background:#3a3022; border:1px solid #5b482c; border-radius:4px; }}
            #rightSidebar #card[warning="true"] QLabel {{ color:#ffd695; font-size:10px; }}
            #rightSidebar #card[warning="true"] #cardHeading {{ color:white; font-size:12px; }}
            #rightSidebar #cardHeading {{ color:white; }}
            #rightSidebar #smallLabel {{ color:#bbb; }}
            #rightSidebar #eyebrow {{ color:#ff776e; }}
            #sidebarTitle {{ color:white; font-size:18px; }}
            #healthValue {{ color:#69caa4; font-size:9px; font-weight:700; }}
            #sideValue {{ font-size:19px; font-weight:700; }}
            #development {{ background:#292c30; border:1px solid #444; border-radius:4px; }}
            #rightSidebar #developmentText {{ color:#ffd58a; background:transparent; border:0; padding:0; font-size:11px; font-weight:700; }}
            #rightSidebar #hint {{ color:#9da1a6; font-size:9px; }}
            #safetyNote {{ color:#92969a; font-size:9px; }}
            #developmentText {{ color:#9a6100; background:#fff4dd; border:1px solid #ead0a2; border-radius:4px; padding:4px 6px; font-size:9px; font-weight:700; }}
            #actualLegendLine {{ background:{RED}; }}
            #meanLegendLine {{ background:#2f3338; }}
            #chartCard #cardHeading {{ font-size:16px; }}
            #chartCard #muted {{ font-size:11px; }}
            #chartLegendText {{ color:#656565; font-size:10px; }}
            #emptyState {{ background:white; border:1px solid {LINE}; border-radius:6px; padding:45px; color:#777; }}
            QLineEdit, QComboBox, QPlainTextEdit, QTableWidget {{ background:white; border:1px solid #c9c9c5; border-radius:4px; padding:7px; }}
            QLineEdit:focus, QComboBox:focus {{ border-color:{RED}; }}
            QHeaderView::section {{ background:#f1f1ee; color:#555; border:0; padding:7px; font-weight:700; }}
            QTableWidget {{ gridline-color:#e9e8e4; }}
            QProgressBar {{ background:#eee; border:0; height:10px; text-align:center; }}
            QProgressBar::chunk {{ background:{RED}; }}
        """)

    def _show_page(self, key):
        keys = [item[0] for item in PAGES]
        self.page_key = key
        self.pages.setCurrentIndex(keys.index(key))
        page = PAGES[keys.index(key)]
        self.page_title.setText(page[3])
        self.page_description.setText(page[4])
        self.section_number.setText(f"FLIGHT DECK / {keys.index(key) + 1:02}")
        for name, button in self.nav_buttons.items():
            button.setChecked(name == key)
        if key in ("measurements", "storage"):
            self._refresh_data()

    def _request(self, path, body=None, callback=None):
        request = QNetworkRequest(QUrl("http://127.0.0.1:8000/api/" + path))
        if body is None:
            reply = self.network.get(request)
        else:
            request.setHeader(QNetworkRequest.KnownHeaders.ContentTypeHeader, "application/json")
            reply = self.network.post(request, json.dumps(body).encode("utf-8"))

        def finished():
            raw = bytes(reply.readAll())
            try:
                data = json.loads(raw.decode("utf-8")) if raw else {}
            except (UnicodeDecodeError, json.JSONDecodeError):
                data = {}
            status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute)
            error = None
            if reply.error() != QNetworkReply.NetworkError.NoError:
                detail = data.get("detail") if isinstance(data, dict) else None
                error = str(detail or reply.errorString())
            elif status and int(status) >= 400:
                error = str(data.get("detail", "Request failed"))
            reply.deleteLater()
            if callback:
                callback(data, error)

        reply.finished.connect(finished)

    def _get(self, path, callback):
        self._request(path, callback=callback)

    def _post(self, path, body=None, callback=None):
        self._request(path, body={} if body is None else body, callback=callback)

    def _on_config(self, data, error):
        if error:
            self._show_error(f"Could not load HMI configuration: {error}")
            return
        self.config = data
        self.flow_toggle.blockSignals(True)
        self.flow_toggle.setChecked(data.get("flow_metrics_enabled", True))
        self.flow_toggle.blockSignals(False)
        names = (
            ("simulation", "Simulation mode"), ("sample_rate_hz", "Expected pitot rate · Hz"),
            ("air_density", "Air density · kg/m³"), ("max_airspeed_ms", "Graph maximum airspeed · m/s"),
            ("serial_port", "Arduino serial port"), ("baud_rate", "Baud rate"),
            ("data_directory", "Local data directory"), ("usb_mount_path", "USB mount path"),
            ("simulate_usb", "Simulated USB export"),
        )
        for index, (key, label) in enumerate(names):
            self.settings_grid.addWidget(QLabel(label), index, 0)
            self.settings_grid.addWidget(QLabel(str(data.get(key, ""))), index, 1)
        self.chart.set_points(self.history, data.get("max_airspeed_ms", 45))

    def _poll(self):
        if self.poll_busy:
            return
        self.poll_busy = True

        def done(data, error):
            self.poll_busy = False
            if error:
                self.connection.setText("RECONNECTING")
                self.connection.setObjectName("tagAmber")
                self.connection.style().unpolish(self.connection)
                self.connection.style().polish(self.connection)
                return
            self.connection.setText("● HMI ONLINE")
            self._on_status(data, None)

        self._get("status", done)

    def _on_status(self, data, error):
        if error:
            return
        self.state = data
        sample = data.get("sample") or {}
        measurement = data.get("measurement") or {}
        live = bool(data.get("sensor_fresh"))
        self.mode_tag.setText("● LIVE HARDWARE" if live else "WAITING FOR ARDUINO")
        self.airspeed.setText(f"{self._fixed(sample.get('airspeed_ms'))} m/s")
        self.dynamic.setText(f"{self._fixed(sample.get('dynamic_pressure_pa'), 1)} Pa")
        self.temperature.setText(f"{self._fixed(sample.get('temperature_c'), 1)} °C")
        enabled = self.config.get("flow_metrics_enabled", True)
        stability = sample.get("flow_stability_pct") if enabled else None
        self.stability.setText(f"{self._fixed(stability, 1)} %" if enabled else "OFF")
        self.airspeed_note.setText("Pitot-derived velocity")
        self.dynamic_note.setText("q∞ · differential pressure")
        self.temperature_note.setText("Test section ambient")
        self.stability_note.setText("5 s rolling window · flow > 1 m/s" if enabled else "Disabled in Settings")
        speed = sample.get("airspeed_ms")
        sample_timestamp = sample.get("host_timestamp")
        if speed is not None and sample_timestamp != self.last_sample_timestamp:
            self.last_sample_timestamp = sample_timestamp
            now = time.monotonic()
            self.history.append({"t": now, "speed": speed, "mean": sample.get("flow_mean_ms")})
            self.history = [item for item in self.history if now - item["t"] <= 60]
            self.chart.set_points(self.history, self.config.get("max_airspeed_ms", 45))
            self.side_speed.setText(f"{self._fixed(speed)} m/s")
            self.side_stability.setText(f"{self._fixed(stability, 1)} %" if enabled else "OFF")
            self.flow_summary.setText(f"{self._fixed(sample.get('flow_mean_ms'))} m/s")
            self.flow_summary_std.setText(f"{self._fixed(sample.get('flow_std_ms'), 3)} m/s")
            self.flow_summary_range.setText(
                f"{self._fixed(sample.get('flow_min_ms'))} / {self._fixed(sample.get('flow_max_ms'))} m/s"
            )
            self.stream_note.setText(f"LIVE SENSOR · {self._fixed(data.get('sample_age_s'), 1)} s sample age" if live else "Waiting for telemetry")
        self.measurement_badge.setText(measurement.get("state", "IDLE"))
        self.elapsed.setText(self._duration(measurement.get("elapsed_s", 0)))
        self.samples.setText(f"{measurement.get('samples', 0):,}")
        self.sample_rate.setText(f"≈ {self.config.get('sample_rate_hz', 5)} Hz")
        recording = measurement.get("state") == "RECORDING"
        if recording:
            self.measurement_name.setText(measurement.get("name", ""))
        self.measurement_name.setEnabled(not recording)
        valid_name = bool(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}", self.measurement_name.text().strip()))
        self.record_start.setEnabled(live and not recording and valid_name)
        self.record_stop.setEnabled(recording)
        self.file_status.setText(
            measurement.get("error") or
            (f"{measurement.get('file_status')} · {measurement.get('name')}" if measurement.get("name") else
             ("Ready · enter a measurement name" if live else "Connect Arduino Uno via USB to record."))
        )
        for key, label in self.health_labels.items():
            label.setText(str((data.get("system") or {}).get(key, "—")))
        self.warnings.setText(" · ".join(data.get("errors", []) + data.get("warnings", [])) or "No warnings")
        events = data.get("events", [])[:8]
        signature = json.dumps(events, sort_keys=True)
        if signature != self.last_events:
            self.last_events = signature
            self.event_log.setText("\n".join(
                f"{self._time(event.get('timestamp'))}  {event.get('message', '')}" for event in events
            ) or "No events")
        if self.page_key == "raw":
            self._render_raw(sample, live)
        if time.monotonic() - self.last_refresh > 3 and self.page_key in ("measurements", "storage"):
            self.last_refresh = time.monotonic()
            self._refresh_data()

    def _render_raw(self, sample, live):
        self.raw_empty.setVisible(not live)
        self.raw_card.setVisible(live)
        self.raw_json_card.setVisible(live)
        self.raw_table.setVisible(live)
        self.raw_json.setVisible(live)
        if not live:
            return
        rows = (
            ("Pitot differential pressure", sample.get("differential_pressure_pa"), "Pa", "MEASURED"),
            ("Dynamic pressure", sample.get("dynamic_pressure_pa"), "Pa", "DERIVED"),
            ("Airspeed", sample.get("airspeed_ms"), "m/s", "DERIVED"),
            ("Airspeed", sample.get("airspeed_kmh"), "km/h", "DERIVED"),
            ("Sensor temperature", sample.get("temperature_c"), "°C", "MEASURED"),
            ("Flow stability", sample.get("flow_stability_pct"), "%", "5 S WINDOW"),
            ("Static pressure", None, "Pa", "IN DEVELOPMENT"),
            ("Angle of attack", None, "°", "IN DEVELOPMENT"),
            ("Fan / VFD output", None, "%", "IN DEVELOPMENT"),
        )
        self.raw_table.setRowCount(len(rows))
        for row_index, (name, value, unit, state) in enumerate(rows):
            values = (name, self._fixed(value, 4), self._fixed(value, 4), unit, self._time(sample.get("host_timestamp")), state)
            for column, text in enumerate(values):
                self.raw_table.setItem(row_index, column, QTableWidgetItem(str(text)))
        self.raw_json.setPlainText(json.dumps(sample, indent=2, ensure_ascii=False))

    def _refresh_data(self):
        self._get("measurements", self._on_runs)
        self._get("storage", self._on_storage)

    def _on_runs(self, runs, error):
        if error:
            self._show_error(error)
            return
        self.measurements_table.setRowCount(len(runs))
        self.measurements_empty.setVisible(not runs)
        self.measurements_table.setVisible(bool(runs))
        for row, run in enumerate(runs):
            values = (
                f"{run.get('name', '')} · {run.get('state', '')}",
                self._time(run.get("timestamp")),
                self._duration(run.get("duration_s", 0)),
                self._fixed(run.get("average_airspeed_ms")),
                str(run.get("samples", 0)),
                "EXPORTED" if run.get("exported") else "LOCAL",
            )
            for column, value in enumerate(values):
                item = QTableWidgetItem(value)
                item.setData(Qt.ItemDataRole.UserRole, run.get("id"))
                self.measurements_table.setItem(row, column, item)
        self.export_select.clear()
        self.export_select.addItem("Choose a completed run", "")
        for run in runs:
            if run.get("state") == "COMPLETED":
                self.export_select.addItem(
                    f"{run.get('name')} · {self._time(run.get('timestamp'))}", run.get("id")
                )

    def _on_storage(self, data, error):
        if error:
            self._show_error(error)
            return
        self.local_space.setText(f"{data.get('local_free_bytes', 0) / 1e9:.1f} GB")
        self.usb_space.setText(f"{data.get('usb_free_bytes', 0) / 1e9:.1f} GB" if data.get("connected") else "Disconnected")
        self.usb_space_note.setText("SIMULATED USB · local test folder" if data.get("simulated") else "Physical USB mount")
        self.unexported.setText(str(data.get("unexported", 0)))
        progress = data.get("progress", {})
        self.export_progress.setValue(int(progress.get("percent", 0)))
        self.export_message.setText(progress.get("message", "Ready to export"))
        self.usb_health.setText("USB storage  ·  " + ("SIM · CONNECTED" if data.get("connected") and data.get("simulated") else "CONNECTED" if data.get("connected") else "DISCONNECTED"))
        self.button__export_selected.setEnabled(bool(data.get("connected")))
        self.button__export_all.setEnabled(bool(data.get("connected")))
        self.button__reconnect_usb.setVisible(bool(data.get("simulated")))

    def _select_run(self, row, _column):
        item = self.measurements_table.item(row, 0)
        identifier = item.data(Qt.ItemDataRole.UserRole) if item else None
        if not identifier:
            return

        def show_detail(data, error):
            if error:
                self._show_error(error)
                return
            self.measurement_detail.setPlainText(json.dumps(data, indent=2, ensure_ascii=False))

        self._get("measurements/" + identifier, show_detail)

    def _start_recording(self):
        self._post("measurement/start", {"name": self.measurement_name.text().strip()}, self._action_result("Recording started · live pitot data"))

    def _stop_recording(self):
        self._post("measurement/stop", {}, self._action_result("Measurement saved", refresh=True))

    def _export_selected(self):
        identifier = self.export_select.currentData()
        if not identifier:
            self._show_error("Select a completed measurement")
            return
        self._post("storage/export/" + str(identifier), {}, self._action_result("Measurement exported", refresh=True))

    def _export_all(self):
        self._post("storage/export-all", {}, self._action_result("All completed measurements exported", refresh=True))

    def _eject(self):
        self._post("storage/eject", {}, self._action_result("Simulated USB safely ejected", refresh=True))

    def _reconnect_usb(self):
        self._post("storage/reconnect", {}, self._action_result("USB storage connected", refresh=True))

    def _toggle_flow_metrics(self, enabled):
        self._post("settings/flow-metrics", {"enabled": enabled}, self._flow_metrics_result(enabled))

    def _flow_metrics_result(self, requested):
        def done(data, error):
            if error:
                self.flow_toggle.blockSignals(True)
                self.flow_toggle.setChecked(not requested)
                self.flow_toggle.blockSignals(False)
                self._show_error(error)
                return
            self.config["flow_metrics_enabled"] = data.get("enabled", requested)
        return done

    def _action_result(self, message, refresh=False):
        def done(_data, error):
            if error:
                self._show_error(error)
                return
            self.file_status.setText(message)
            if refresh:
                self._refresh_data()
        return done

    def _show_error(self, message):
        QMessageBox.warning(self, "Windtunnel HMI", str(message))

    @staticmethod
    def _fixed(value, digits=2):
        if value is None or value == "" or not isinstance(value, (float, int)):
            try:
                value = float(value)
            except (TypeError, ValueError):
                return "—"
        if not math.isfinite(value):
            return "—"
        return f"{value:.{digits}f}"

    @staticmethod
    def _duration(seconds):
        seconds = max(0, int(float(seconds or 0)))
        return f"{seconds // 60:02}:{seconds % 60:02}"

    @staticmethod
    def _time(value):
        if not value:
            return "—"
        try:
            return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone().strftime("%Y-%m-%d %H:%M:%S")
        except (ValueError, AttributeError):
            return str(value)


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("Avans Windtunnel HMI")
    window = HMIWindow()
    window.showMaximized()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
