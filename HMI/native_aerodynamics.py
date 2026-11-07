"""Native aerodynamic acquisition and analysis views; all work uses asynchronous HTTP."""
import json
import math
from urllib.parse import urlencode, quote

from PySide6.QtCore import Qt, QTimer, QPointF, QRectF, QUrl, QSaveFile, QIODevice
from PySide6.QtGui import QColor, QPainter, QPen, QPainterPath, QPalette
from PySide6.QtNetwork import QNetworkRequest, QNetworkReply
from PySide6.QtWidgets import (
    QWidget, QFrame, QLabel, QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox,
    QPlainTextEdit, QPushButton, QCheckBox, QProgressBar, QVBoxLayout, QHBoxLayout,
    QGridLayout, QFormLayout, QTabWidget, QTableWidget, QTableWidgetItem,
    QHeaderView, QListWidget, QListWidgetItem, QFileDialog, QToolTip, QScrollArea,
)

API = 'aerodynamics'
COLORS = ('#c8291e', '#2868a0', '#38906a', '#9a5c9d', '#b58128', '#35516a')


def number(value, digits=4):
    return 'Unavailable' if value is None else f'{value:.{digits}g}'


def label(text=''):
    widget = QLabel(text)
    widget.setWordWrap(True)
    widget.setTextFormat(Qt.TextFormat.PlainText)
    return widget


def card(title):
    widget = QFrame()
    widget.setObjectName('card')
    layout = QVBoxLayout(widget)
    heading = label(title)
    heading.setStyleSheet('font-size:18px;font-weight:600;')
    layout.addWidget(heading)
    return widget, layout


def spin(minimum, maximum, value, suffix='', decimals=2):
    widget = QDoubleSpinBox()
    widget.setRange(minimum, maximum)
    widget.setDecimals(decimals)
    widget.setValue(value)
    widget.setSuffix(suffix)
    return widget


def table(headers):
    widget = QTableWidget(0, len(headers))
    widget.setHorizontalHeaderLabels(headers)
    widget.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
    widget.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    widget.horizontalHeader().setStretchLastSection(True)
    widget.setMinimumHeight(180)
    return widget


def fill_table(widget, rows):
    widget.setRowCount(len(rows))
    for row, values in enumerate(rows):
        for column, value in enumerate(values):
            item = QTableWidgetItem(str(value))
            item.setToolTip(str(value))
            widget.setItem(row, column, item)


def scroll_tab(content):
    scroll = QScrollArea()
    scroll.setObjectName('aerodynamicScroll')
    scroll.setWidgetResizable(True)
    scroll.setFrameShape(QFrame.Shape.NoFrame)
    # Native Windows dark mode also paints scroll viewports and plain tab widgets.
    # Explicitly paint both surfaces, rather than relying on an ancestor palette.
    for surface in (content, scroll.viewport()):
        surface.setProperty('aerodynamicSurface', True)
        surface.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        surface.setAutoFillBackground(True)
        palette = surface.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor('#f6f6f4'))
        palette.setColor(QPalette.ColorRole.Base, QColor('#ffffff'))
        palette.setColor(QPalette.ColorRole.WindowText, QColor('#181818'))
        palette.setColor(QPalette.ColorRole.Text, QColor('#181818'))
        surface.setPalette(palette)
    scroll.setWidget(content)
    return scroll


class AerodynamicPlot(QWidget):
    """Responsive tap plot, inverted Cp and exact point inspection (no invented taps)."""
    def __init__(self, polar=False):
        super().__init__()
        self.polar = polar
        self.results = []
        self.upper = self.lower = self.uncertainty = True
        self.hits = []
        self.setMinimumHeight(300)
        self.setMouseTracking(True)

    def set_results(self, results):
        self.results = results
        self.setMinimumHeight(max(300, 245+20*len(self.series())))
        self.update()

    def series(self):
        if self.polar:
            groups = {}
            for point in self.results:
                groups.setdefault((point['airfoil'], point['source']), []).append(point)
            return [(f'{foil} [{source}]', index, False,
                     [dict(x_c=p['alpha_deg'], cp=p['cl'], cp_std=0,
                           tap_id=p['experiment_id']) for p in sorted(points, key=lambda p: p['alpha_deg'])])
                    for index, ((foil, source), points) in enumerate(groups.items())
                    if len({p['alpha_deg'] for p in points}) >= 2]
        series = []
        for index, result in enumerate(self.results):
            for surface, enabled in (('upper', self.upper), ('lower', self.lower)):
                points = sorted([p for p in result['cp'] if p['surface'] == surface and p['cp'] is not None],
                                key=lambda p: p['x_c'])
                if enabled and points:
                    name = f"{result['experiment_name']} | {result['alpha_deg']:g}° | {surface} [{result['source']}]"
                    series.append((name, index, surface == 'lower', points))
        return series

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.fillRect(self.rect(), QColor('white'))
        series = self.series()
        self.hits = []
        if not series:
            painter.setPen(QColor('#747474'))
            painter.drawText(self.rect().adjusted(25, 10, -25, -10), Qt.AlignmentFlag.AlignCenter | Qt.TextFlag.TextWordWrap,
                'Cl–alpha requires full Cl results at two or more distinct angles for one airfoil.' if self.polar
                else 'Select an analyzed experiment with valid pressure taps.')
            return
        legend_height = 20*len(series)
        graph = QRectF(65, 22, max(50, self.width()-90), max(80, self.height()-legend_height-85))
        values = [p['cp']+sign*(p.get('cp_std') or 0) for _, _, _, points in series
                  for p in points for sign in (-1, 1)]
        low, high = min(values), max(values)
        padding = max(.15, (high-low)*.12)
        low -= padding; high += padding
        xs = [p['x_c'] for _, _, _, points in series for p in points]
        xmin, xmax = (min(xs)-1, max(xs)+1) if self.polar else (0, 1)
        def position(x, y):
            fraction = (y-low)/(high-low)
            return QPointF(graph.left()+(x-xmin)/(xmax-xmin)*graph.width(),
                           graph.top()+(1-fraction if self.polar else fraction)*graph.height())
        for tick in range(6):
            y = low+(high-low)*tick/5
            point = position(xmin, y)
            painter.setPen(QColor('#e3e3e3'))
            painter.drawLine(QPointF(graph.left(), point.y()), QPointF(graph.right(), point.y()))
            painter.setPen(QColor('#505050'))
            painter.drawText(QRectF(0, point.y()-10, 57, 20), Qt.AlignmentFlag.AlignRight, f'{y:.2g}')
            x = xmin+(xmax-xmin)*tick/5
            painter.drawText(QRectF(position(x, low).x()-25, graph.bottom()+8, 50, 22), Qt.AlignmentFlag.AlignCenter, f'{x:.2g}')
        painter.drawText(QRectF(graph.left(), graph.bottom()+32, graph.width(), 22), Qt.AlignmentFlag.AlignCenter,
                         'Angle of attack α [°]' if self.polar else 'Normalized chord position x/c [−]')
        painter.save(); painter.translate(16, graph.center().y()); painter.rotate(-90)
        painter.drawText(QRectF(-100, -12, 200, 24), Qt.AlignmentFlag.AlignCenter, 'Lift coefficient Cl [−]' if self.polar else 'Pressure coefficient Cp [−]')
        painter.restore()
        for index, (name, color_index, dashed, points) in enumerate(series):
            color = QColor(COLORS[color_index % len(COLORS)])
            pen = QPen(color, 1.7, Qt.PenStyle.DashLine if dashed else Qt.PenStyle.SolidLine)
            painter.setPen(pen)
            path = QPainterPath()
            for point_index, point in enumerate(points):
                pos = position(point['x_c'], point['cp'])
                path.moveTo(pos) if point_index == 0 else path.lineTo(pos)
                painter.setBrush(color); painter.drawEllipse(pos, 3.5, 3.5)
                if self.uncertainty and point.get('cp_std'):
                    painter.drawLine(position(point['x_c'], point['cp']-point['cp_std']),
                                     position(point['x_c'], point['cp']+point['cp_std']))
                self.hits.append((pos, f"{name}\n{point['tap_id']} | x: {point['x_c']:.4g} | value: {point['cp']:.4g}\nσ: {number(point.get('cp_std'))}"))
            painter.setBrush(Qt.BrushStyle.NoBrush); painter.drawPath(path)
            y = self.height()-legend_height+index*20
            painter.drawLine(QPointF(66, y+8), QPointF(92, y+8))
            painter.drawText(QRectF(100, y-2, self.width()-110, 20), Qt.AlignmentFlag.AlignLeft, name)

    def mouseMoveEvent(self, event):
        for position, text in self.hits:
            if (position-event.position()).manhattanLength() < 15:
                QToolTip.showText(event.globalPosition().toPoint(), text, self)
                return
        QToolTip.hideText()


class AerodynamicExperimentCard(QFrame):
    def __init__(self, hmi):
        super().__init__()
        self.hmi = hmi
        self.identifier = None
        self.polling = False
        self.detail = None
        self.setObjectName('card')
        layout = QVBoxLayout(self)
        title = label('Aerodynamic Experiment')
        title.setStyleSheet('font-size:20px;font-weight:600;')
        layout.addWidget(title)
        layout.addWidget(label('Pressure acquisition and analysis. Physical START, STOP and RESET remain on the machine.'))
        grid = QGridLayout()
        self.name = QLineEdit(); self.name.setPlaceholderText('e.g. Wing_alpha_5')
        self.airfoil = QLineEdit(); self.airfoil.setPlaceholderText('Airfoil identifier')
        self.chord = spin(.1, 10000, 100, ' mm')
        self.alpha = spin(-90, 90, 0, ' °')
        self.duration = spin(.05, 3600, 5, ' s')
        self.source = QComboBox(); self.source.addItems(['Physical — adapter not configured', 'SIMULATION — synthetic pressure data'])
        self.description = QLineEdit(); self.description.setPlaceholderText('Optional experiment description')
        for index, (caption, field) in enumerate((('Experiment name', self.name), ('Airfoil identifier', self.airfoil),
                ('Chord', self.chord), ('Angle of attack', self.alpha), ('Averaging duration', self.duration), ('Data source', self.source))):
            grid.addWidget(label(caption), index//3*2, index%3)
            grid.addWidget(field, index//3*2+1, index%3)
        layout.addLayout(grid); layout.addWidget(self.description)
        self.advanced_toggle = QCheckBox('Advanced: geometry, channels, sampling and simulation settings')
        layout.addWidget(self.advanced_toggle)
        self.advanced = QWidget(); advanced = QFormLayout(self.advanced)
        self.rate = spin(1, 100, 20, ' Hz')
        self.sim_speed = spin(.1, 40, 15, ' m/s')
        self.noise = spin(0, 100, .1, ' Pa', 3)
        self.seed = QSpinBox(); self.seed.setRange(0, 2147483647); self.seed.setValue(42)
        self.taps = QPlainTextEdit(); self.taps.setMaximumHeight(140)
        self.taps.setPlaceholderText('Optional tap JSON array: tap_id, surface, x_c, z_c, channel, enabled, sensor_config, calibration.\nBlank simulation uses an explicit synthetic diamond fixture, not validated airfoil geometry.')
        for caption, field in (('Sampling rate', self.rate), ('Synthetic airspeed', self.sim_speed),
                               ('Synthetic noise σ', self.noise), ('Deterministic seed', self.seed), ('Pressure tap configuration', self.taps)):
            advanced.addRow(caption, field)
        self.advanced.hide(); self.advanced_toggle.toggled.connect(self.advanced.setVisible)
        layout.addWidget(self.advanced)
        self.state = label('IDLE · Physical acquisition unavailable until a real pressure adapter is configured.')
        layout.addWidget(self.state)
        self.progress = QProgressBar(); self.progress.setRange(0, 100); layout.addWidget(self.progress)
        actions = QHBoxLayout()
        self.prepare = QPushButton('Prepare Experiment'); self.start = QPushButton('Start Measurement')
        self.stop = QPushButton('Stop Measurement'); self.analyze = QPushButton('Run Analysis'); self.view = QPushButton('View Results')
        for button in (self.prepare, self.start, self.stop, self.analyze, self.view): actions.addWidget(button)
        layout.addLayout(actions)
        self.prepare.clicked.connect(self.prepare_experiment)
        self.start.clicked.connect(lambda: self.action('start'))
        self.stop.clicked.connect(lambda: self.action('stop'))
        self.analyze.clicked.connect(lambda: self.action('analyze'))
        self.view.clicked.connect(self.view_results)
        self.timer = QTimer(self); self.timer.setInterval(500); self.timer.timeout.connect(self.poll)
        self.timer.start(); self.set_state(None)

    def configuration(self):
        taps = json.loads(self.taps.toPlainText()) if self.taps.toPlainText().strip() else []
        return dict(name=self.name.text().strip(), airfoil=self.airfoil.text().strip(), chord_mm=self.chord.value(),
                    alpha_deg=self.alpha.value(), averaging_s=self.duration.value(), description=self.description.text(),
                    source='simulation' if self.source.currentIndex() == 1 else 'physical', taps=taps,
                    sample_rate_hz=self.rate.value(), simulation_airspeed_ms=self.sim_speed.value(),
                    simulation_noise_pa=self.noise.value(), simulation_seed=self.seed.value())

    def prepare_experiment(self):
        try:
            body = self.configuration()
        except (ValueError, TypeError) as exc:
            self.state.setText('Invalid tap configuration: '+str(exc)); return
        self.prepare.setEnabled(False)
        self.hmi._post(API+'/experiments', body, self.received)

    def action(self, action):
        if self.identifier:
            self.hmi._post(API+'/experiments/'+self.identifier+'/'+action, {}, self.received)

    def received(self, data, error):
        self.prepare.setEnabled(True)
        if error:
            self.state.setText('Request failed: '+error); return
        self.detail = data; self.identifier = data['metadata']['id']; self.set_state(data)
        if hasattr(self.hmi, 'aerodynamic_panel'): self.hmi.aerodynamic_panel.refresh()

    def set_state(self, detail):
        metadata = detail['metadata'] if detail else {}
        state = metadata.get('state', 'IDLE')
        if detail:
            self.state.setText(f"{state} · {metadata['source'].upper()} · Analysis: {metadata['analysis_status']}\n"+
                               str(metadata.get('error') or metadata.get('message') or metadata['id']))
        self.progress.setValue(int(metadata.get('progress', 0)))
        busy = state in ('ACQUIRING', 'PROCESSING')
        self.prepare.setEnabled(not busy)
        physical_ok = bool(detail and detail.get('physical_available'))
        self.start.setEnabled(state == 'PREPARED' and (metadata.get('source') == 'simulation' or physical_ok))
        self.start.setToolTip('Starts pressure acquisition only; never starts the fan.')
        self.stop.setEnabled(state == 'ACQUIRING')
        self.analyze.setEnabled(state == 'COMPLETED')
        self.view.setEnabled(bool(detail))

    def poll(self):
        if self.identifier and not self.polling and (self.isVisible() or self.detail['metadata']['state'] in ('ACQUIRING', 'PROCESSING')):
            self.polling = True
            identifier = self.identifier
            def done(data, error):
                self.polling = False
                if identifier != self.identifier:
                    return
                if error:
                    self.state.setText('Backend unavailable: '+error)
                    self.start.setEnabled(False); self.analyze.setEnabled(False)
                else:
                    previous = self.detail['metadata']['state']
                    self.detail = data; self.set_state(data)
                    if previous != data['metadata']['state'] and hasattr(self.hmi, 'aerodynamic_panel'):
                        self.hmi.aerodynamic_panel.refresh()
            self.hmi._get(API+'/experiments/'+identifier, done)

    def view_results(self):
        self.hmi._show_page('cp')
        self.hmi.aerodynamic_panel.select_identifier(self.identifier)


class AerodynamicAnalysisPanel(QWidget):
    def __init__(self, hmi):
        super().__init__()
        self.setObjectName('aerodynamicPanel')
        self.hmi = hmi; self.identifier = None; self.experiments = []; self.detail = None
        self.pending_selection = None; self.loading = False; self.signature = None
        layout = QVBoxLayout(self); layout.setContentsMargins(0, 0, 0, 0)
        header = QHBoxLayout(); self.selector = QComboBox(); self.selector.setMinimumWidth(230)
        refresh = QPushButton('Select / refresh experiments'); refresh.clicked.connect(self.refresh)
        header.addWidget(self.selector, 1); header.addWidget(refresh); layout.addLayout(header)
        self.message = label('Select an experiment or prepare one from Overview.'); layout.addWidget(self.message)
        self.tabs = QTabWidget(); layout.addWidget(self.tabs, 1)
        overview, box = card('Selected aerodynamic experiment')
        self.summary = table(['Measurement / analysis', 'Value']); box.addWidget(self.summary, 1)
        actions = QHBoxLayout(); self.analyze_button = QPushButton('Run / rerun analysis')
        self.analyze_button.clicked.connect(self.analyze); actions.addWidget(self.analyze_button)
        for title, index in (('Open Cp Distribution', 1), ('Open Aerodynamic Coefficients', 2)):
            button = QPushButton(title); button.clicked.connect(lambda checked=False, i=index: self.tabs.setCurrentIndex(i)); actions.addWidget(button)
        box.addLayout(actions)
        settings = QHBoxLayout()
        self.qmin = spin(1, 10000, 1, ' Pa'); self.minimum_samples = QSpinBox(); self.minimum_samples.setRange(2, 1000000); self.minimum_samples.setValue(5)
        self.reference_age = spin(.001, 10, .1, ' s', 3); self.moment_reference = spin(0, 1, .25, ' c')
        for caption, field in (('Minimum q', self.qmin), ('Samples / tap', self.minimum_samples), ('Reference age', self.reference_age), ('Moment x/c', self.moment_reference)):
            settings.addWidget(label(caption)); settings.addWidget(field)
        box.addLayout(settings)
        exports = QHBoxLayout(); self.export_choice = QComboBox()
        self.export_choice.addItems(['analysis_report.pdf', 'analysis.json', 'cp_curve.csv', 'coefficients.csv', 'raw_data.csv', 'processed_data.csv', 'cp_distribution.png', 'cp_distribution.svg', 'experiment.json'])
        self.export_button = QPushButton('Export results'); self.export_button.clicked.connect(lambda: self.download(self.export_choice.currentText()))
        exports.addWidget(self.export_choice, 1); exports.addWidget(self.export_button); box.addLayout(exports)
        self.tabs.addTab(scroll_tab(overview), 'Overview')
        cp = QWidget(); cpbox = QVBoxLayout(cp)
        self.cp_metadata = label(); cpbox.addWidget(self.cp_metadata)
        toggles = QHBoxLayout(); self.cp_plot = AerodynamicPlot()
        for caption, attribute in (('Upper surface', 'upper'), ('Lower surface', 'lower'), ('Standard deviation', 'uncertainty')):
            toggle = QCheckBox(caption); toggle.setChecked(True)
            toggle.toggled.connect(lambda enabled, attr=attribute: self.toggle_surface(attr, enabled)); toggles.addWidget(toggle)
        for caption, filename in (('Export CSV', 'cp_curve.csv'), ('PNG', 'cp_distribution.png'), ('SVG', 'cp_distribution.svg')):
            button = QPushButton(caption); button.clicked.connect(lambda checked=False, f=filename: self.download(f)); toggles.addWidget(button)
        cpbox.addLayout(toggles); cpbox.addWidget(self.cp_plot, 2)
        cpbox.addWidget(label('Dots are measured taps; lines connect measured taps piecewise-linearly. Hover over a dot to inspect it. No endpoint extrapolation.'))
        self.cp_table = table(['Tap', 'Surface', 'x/c', 'Cp', 'σ Cp', 'Mean p [Pa]', 'σ p [Pa]', 'Samples', 'Validity'])
        cpbox.addWidget(self.cp_table, 1); self.tabs.addTab(scroll_tab(cp), 'Cp Distribution')
        coeff = QWidget(); coeffbox = QVBoxLayout(coeff); metrics = QHBoxLayout(); self.coefficient_labels = {}
        for key in ('cl', 'cn', 'ca', 'cm', 'cd'):
            widget, cell = card(key.capitalize()); value = label('Unavailable'); cell.addWidget(value)
            self.coefficient_labels[key] = value; metrics.addWidget(widget)
        coeffbox.addLayout(metrics)
        self.coefficient_notes = label(); coeffbox.addWidget(self.coefficient_notes)
        self.polar = AerodynamicPlot(polar=True); coeffbox.addWidget(self.polar, 1)
        coeffbox.addWidget(label('Cl–alpha uses full pressure-integration results only. Select multiple experiments in Compare. Total Cd is unavailable from pressure alone.'))
        self.tabs.addTab(scroll_tab(coeff), 'Aerodynamic Coefficients')
        comparison = QWidget(); comparebox = QVBoxLayout(comparison)
        filters = QHBoxLayout(); self.filters = {}
        for key, caption in (('airfoil', 'Airfoil'), ('name', 'Experiment name'), ('date', 'Date YYYY-MM-DD')):
            field = QLineEdit(); field.setPlaceholderText(caption); self.filters[key] = field; filters.addWidget(field)
        self.alpha_min = spin(-90, 90, -90, ' °'); self.alpha_max = spin(-90, 90, 90, ' °')
        filters.addWidget(label('α from')); filters.addWidget(self.alpha_min); filters.addWidget(label('to')); filters.addWidget(self.alpha_max)
        filter_button = QPushButton('Filter'); filter_button.clicked.connect(self.refresh); filters.addWidget(filter_button)
        comparebox.addLayout(filters)
        choose = QHBoxLayout(); self.compare_list = QListWidget(); self.compare_list.setMaximumHeight(115)
        choose.addWidget(self.compare_list, 1); compare_button = QPushButton('Compare selected'); compare_button.clicked.connect(self.compare_selected); choose.addWidget(compare_button)
        comparebox.addLayout(choose)
        self.compare_warning = label(); comparebox.addWidget(self.compare_warning)
        self.compare_conditions = table(['Experiment / source', 'Airfoil', 'α [°]', 'V [m/s]', 'q [Pa]', 'ρ [kg/m³]', 'Chord [mm]', 'Cl', 'Cn', 'Ca', 'Cm', 'Quality'])
        self.compare_conditions.setMaximumHeight(180); comparebox.addWidget(self.compare_conditions)
        self.compare_cp = AerodynamicPlot(); comparebox.addWidget(self.compare_cp, 1)
        self.compare_polar = AerodynamicPlot(polar=True); comparebox.addWidget(self.compare_polar, 1)
        self.tabs.addTab(scroll_tab(comparison), 'Compare')
        self.selector.currentIndexChanged.connect(self.selection_changed)
        self.timer = QTimer(self); self.timer.setInterval(800); self.timer.timeout.connect(self.poll); self.timer.start()
        self.analyze_button.setEnabled(False); self.export_button.setEnabled(False)
        QTimer.singleShot(0, self.refresh)

    def toggle_surface(self, attribute, enabled):
        setattr(self.cp_plot, attribute, enabled); self.cp_plot.update()

    def refresh(self):
        query = {key: field.text().strip() for key, field in self.filters.items()}
        query.update(alpha_min=self.alpha_min.value(), alpha_max=self.alpha_max.value())
        def received(data, error):
            if error:
                self.message.setText('Cannot load experiments: '+error); return
            self.experiments = data
            selected = self.pending_selection or self.identifier
            checked = {self.compare_list.item(i).data(Qt.ItemDataRole.UserRole) for i in range(self.compare_list.count())
                       if self.compare_list.item(i).checkState() == Qt.CheckState.Checked}
            self.selector.blockSignals(True); self.selector.clear(); self.compare_list.clear()
            for run in data:
                config = run['configuration']
                name = f"{run['name']} | {config['alpha_deg']:g}° | {run['source'].upper()} | {run['state']}"
                self.selector.addItem(name, run['id'])
                if run['state'] == 'COMPLETED' and run['analysis_status'] in ('FULL', 'PARTIAL', 'INVALID'):
                    item = QListWidgetItem(name); item.setData(Qt.ItemDataRole.UserRole, run['id'])
                    item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
                    item.setCheckState(Qt.CheckState.Checked if run['id'] in checked else Qt.CheckState.Unchecked)
                    self.compare_list.addItem(item)
            index = self.selector.findData(selected)
            self.selector.setCurrentIndex(index if index >= 0 else (0 if data else -1))
            self.selector.blockSignals(False); self.pending_selection = None
            self.signature = None; self.selection_changed()
        self.hmi._get(API+'/experiments?'+urlencode(query), received)

    def select_identifier(self, identifier):
        self.pending_selection = identifier
        for field in self.filters.values(): field.clear()
        self.alpha_min.setValue(-90); self.alpha_max.setValue(90); self.refresh()

    def selection_changed(self):
        self.identifier = self.selector.currentData(); self.signature = None
        self.detail = None
        self.analyze_button.setEnabled(False); self.export_button.setEnabled(False)
        fill_table(self.summary, []); fill_table(self.cp_table, [])
        self.cp_metadata.clear(); self.coefficient_notes.clear()
        for field in self.coefficient_labels.values():
            field.setText('Unavailable'); field.setToolTip('Select a completed analysis')
        self.message.setText('Loading selected experiment…' if self.identifier else 'Prepare an experiment from Overview.')
        self.cp_plot.set_results([]); self.polar.set_results([]); self.poll(force=True)

    def poll(self, force=False):
        if not self.identifier or self.loading or (not force and not self.isVisible()): return
        identifier = self.identifier; self.loading = True
        def received(data, error):
            self.loading = False
            if identifier != self.identifier: return
            if error:
                self.message.setText('Backend unavailable: '+error); self.analyze_button.setEnabled(False); return
            self.detail = data
            metadata = data['metadata']
            signature = (metadata['id'], metadata['state'], metadata['analysis_status'], metadata.get('analysis_revision'), metadata.get('progress'), metadata.get('error'))
            if signature != self.signature:
                self.signature = signature; self.render(data)
        self.hmi._get(API+'/experiments/'+identifier, received)

    def render(self, detail):
        meta = detail['metadata']; config = meta['configuration']; result = detail['analysis']
        summary = result['summary'] if result else {}; quality = result['quality'] if result else {}
        source = 'SIMULATION — SYNTHETIC DATA; not validated airfoil performance' if meta['simulated'] else 'PHYSICAL ACQUISITION'
        self.message.setText(f"{source}\n{meta['state']} · Analysis: {meta['analysis_status']} · Progress {meta.get('progress', 0)}%"+
                             ('\n'+str(meta['error']) if meta.get('error') else ''))
        rows = [('Experiment', meta['name']), ('Identifier', meta['id']), ('Airfoil', config['airfoil']),
                ('Angle of attack [°]', number(config['alpha_deg'])), ('Chord [mm]', number(config['chord_mm'])),
                ('Timestamp', meta['timestamp']), ('Duration [s]', number(meta['duration_s'])),
                ('Mean airspeed [m/s]', number(summary.get('mean_airspeed_ms'))),
                ('Air density [kg/m³]', number(summary.get('air_density_kg_m3', meta['air_density_kg_m3']))),
                ('Dynamic pressure [Pa]', number(summary.get('mean_dynamic_pressure_pa'))),
                ('Requested Reynolds number', number(summary.get('requested_reynolds'))),
                ('Actual Reynolds number (measured speed)', number(summary.get('actual_reynolds'))),
                ('Temperature used [°C]', number(summary.get('temperature_c'))),
                ('Dynamic viscosity [Pa·s]', number(summary.get('dynamic_viscosity_pa_s'))),
                ('Applied airspeed setpoint [m/s]', number(summary.get('applied_airspeed_setpoint_ms'))),
                ('Valid samples', summary.get('valid_samples', 'Not analyzed')), ('Pressure taps', summary.get('tap_count', len(config['taps']))),
                ('Sensor validity', quality.get('sensor_validity', 'Not validated')), ('Measurement quality', quality.get('status', 'Not analyzed')),
                ('Analysis status', meta['analysis_status']), ('Available coefficients', ', '.join(k.capitalize() for k, v in result['coefficients'].items() if v['value'] is not None) if result else 'None'),
                ('Description', config['description']), ('Warnings', '\n'.join(quality.get('warnings', [])))]
        fill_table(self.summary, rows)
        self.analyze_button.setEnabled(meta['state'] == 'COMPLETED')
        self.export_button.setEnabled(meta['state'] == 'COMPLETED')
        for index in range(self.export_choice.count()):
            self.export_choice.model().item(index).setEnabled(self.export_choice.itemText(index) in detail['files'])
        self.cp_metadata.setText(f"{meta['name']} | {config['airfoil']} | α {config['alpha_deg']:g}° | chord {config['chord_mm']:g} mm | V {number(summary.get('mean_airspeed_ms'))} m/s | {source}")
        self.cp_plot.set_results([result] if result else [])
        fill_table(self.cp_table, [[p['tap_id'], p['surface'], number(p['x_c']), number(p['cp']), number(p['cp_std']), number(p['mean_pressure_pa'], 7), number(p['pressure_std_pa']), p['valid_sample_count'], 'Valid' if p['valid'] else 'Insufficient samples'] for p in result['cp']] if result else [])
        for key, field in self.coefficient_labels.items():
            coefficient = result['coefficients'][key] if result else dict(value=None, status='UNAVAILABLE', reason='Run analysis on valid pressure data')
            field.setText(number(coefficient['value'])+'\n'+coefficient['status'])
            field.setToolTip(coefficient['reason'])
        self.coefficient_notes.setText('\n'.join(f"{key.capitalize()}: {value['reason']}" for key, value in result['coefficients'].items()) if result else 'No analyzed pressure data.')
        matching = [run['id'] for run in self.experiments if run['configuration']['airfoil'] == config['airfoil'] and run.get('analysis_status') == 'FULL' and run['source'] == meta['source']][:12]
        if len(matching) >= 2:
            selected = self.identifier
            self.hmi._post(API+'/compare', {'identifiers': matching}, lambda data, error: self.polar.set_results(data['cl_alpha']) if not error and selected == self.identifier else None)

    def analyze(self):
        if not self.identifier: return
        self.analyze_button.setEnabled(False)
        body = dict(minimum_q_pa=self.qmin.value(), minimum_samples_per_tap=self.minimum_samples.value(),
                    max_reference_age_s=self.reference_age.value(), moment_reference_x_c=self.moment_reference.value())
        def done(data, error):
            if error: self.message.setText('Analysis failed: '+error)
            self.signature = None; self.poll(force=True)
        self.hmi._post(API+'/experiments/'+self.identifier+'/analyze', body, done)

    def compare_selected(self):
        ids = [self.compare_list.item(i).data(Qt.ItemDataRole.UserRole) for i in range(self.compare_list.count())
               if self.compare_list.item(i).checkState() == Qt.CheckState.Checked]
        if not 1 <= len(ids) <= 12:
            self.compare_warning.setText('Select between 1 and 12 completed, analyzed experiments.'); return
        def done(data, error):
            if error: self.compare_warning.setText(error); return
            self.compare_warning.setText('\n'.join(data['warnings']) or 'Measurement conditions comparable.')
            self.compare_cp.set_results(data['experiments']); self.compare_polar.set_results(data['cl_alpha'])
            self.polar.set_results(data['cl_alpha'])
            fill_table(self.compare_conditions, [[r['experiment_name']+' / '+r['source'], r['airfoil'], number(r['alpha_deg']),
                number(r['summary']['mean_airspeed_ms']), number(r['summary']['mean_dynamic_pressure_pa']), number(r['summary']['air_density_kg_m3']),
                number(r['chord_mm']), *(number(r['coefficients'][k]['value']) for k in ('cl', 'cn', 'ca', 'cm')), r['status']] for r in data['experiments']])
        self.hmi._post(API+'/compare', {'identifiers': ids}, done)

    def download(self, filename):
        if not self.identifier or not self.detail or filename not in self.detail['files']:
            self.message.setText('This result file is unavailable. Complete acquisition and analysis first.'); return
        path, _ = QFileDialog.getSaveFileName(self, 'Export aerodynamic result', self.identifier+'_'+filename)
        if not path: return
        output = QSaveFile(path)
        if not output.open(QIODevice.OpenModeFlag.WriteOnly):
            self.message.setText('Cannot open destination: '+output.errorString()); return
        url = 'http://127.0.0.1:8000/api/'+API+'/experiments/'+quote(self.identifier, safe='')+'/files/'+filename
        reply = self.hmi.network.get(QNetworkRequest(QUrl(url)))
        write_failed = [False]
        def read():
            content = reply.readAll()
            if output.write(content) != content.size(): write_failed[0] = True
        reply.readyRead.connect(read)
        def finish():
            read()
            if reply.error() != QNetworkReply.NetworkError.NoError or write_failed[0]:
                output.cancelWriting(); self.message.setText('Export failed: '+reply.errorString())
            elif not output.commit(): self.message.setText('Export failed: '+output.errorString())
            else: self.message.setText('Exported '+path)
            reply.deleteLater()
        reply.finished.connect(finish)
