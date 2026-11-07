"""Two operator inputs and read-only Reynolds preview using backend sensor data."""
from PySide6.QtCore import QRegularExpression
from PySide6.QtGui import QRegularExpressionValidator
from PySide6.QtWidgets import QGridLayout, QLabel, QLineEdit, QPushButton, QVBoxLayout, QWidget

from backend.reynolds import actual_reynolds, airspeed_for_reynolds, required_airspeed


class ReynoldsSetpointWidget(QWidget):
    def __init__(self, changed, apply, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(6)
        self.reynolds = self._input(layout, 'DESIRED REYNOLDS NUMBER · Re', 'e.g. 100000')
        self.chord = self._input(layout, 'AIRFOIL CHORD LENGTH · mm', 'e.g. 100')
        grid = QGridLayout()
        grid.setColumnStretch(1, 1)
        self.values = {}
        for row, (key, title) in enumerate((
            ('temperature', 'Measured temperature'), ('density', 'Existing air density'),
            ('viscosity', 'Dynamic viscosity'), ('airspeed', 'Required airspeed'),
            ('actual', 'Actual measured Reynolds'),
        )):
            caption = QLabel(title)
            caption.setObjectName('controlHelp')
            value = QLabel('—')
            value.setObjectName('reynoldsValue')
            value.setWordWrap(True)
            grid.addWidget(caption, row, 0)
            grid.addWidget(value, row, 1)
            self.values[key] = value
        layout.addLayout(grid)
        self.message = QLabel('Enter Reynolds number and chord length.')
        self.message.setWordWrap(True)
        self.message.setObjectName('controlHelp')
        layout.addWidget(self.message)
        self.apply_button = QPushButton('Apply Reynolds Setpoint')
        self.apply_button.setEnabled(False)
        self.apply_button.clicked.connect(apply)
        layout.addWidget(self.apply_button)
        self.reynolds.textChanged.connect(lambda _: changed())
        self.chord.textChanged.connect(lambda _: changed())
        self.preview = None
        self.error = ''

    def _input(self, layout, title, placeholder):
        label = QLabel(title)
        label.setObjectName('smallLabel')
        layout.addWidget(label)
        field = QLineEdit()
        field.setObjectName('operatorNumericInput')
        field.setPlaceholderText(placeholder)
        field.setMaxLength(32)
        field.setValidator(QRegularExpressionValidator(
            QRegularExpression(r'\d*(?:[.,]\d*)?(?:[eE][+-]?\d*)?'), field))
        layout.addWidget(field)
        return field

    def refresh(self, state, maximum):
        env = (state.get('reynolds') or {}).get('environment') or {}
        self.preview = None
        self.error = ''
        for value in self.values.values():
            value.setText('—')
        temperature = env.get('temperature_c')
        density = env.get('air_density_kg_m3')
        if temperature is not None:
            self.values['temperature'].setText(f'{temperature:.2f} °C')
        if density is not None:
            self.values['density'].setText(f'{density:.4f} kg/m³')
        try:
            if not env.get('valid'):
                raise ValueError(env.get('reason') or 'Fresh temperature telemetry is unavailable.')
            mu = env['dynamic_viscosity_pa_s']
            self.values['viscosity'].setText(f'{mu:.5g} Pa·s')
            try:
                chord = float(self.chord.text().replace(',', '.'))
                actual = actual_reynolds((state.get('sample') or {}).get('airspeed_ms'), chord,
                                         temperature, density)
                self.values['actual'].setText(f'{actual:,.0f}')
            except (ValueError, TypeError):
                pass
            try:
                requested = float(self.reynolds.text().replace(',', '.'))
                chord = float(self.chord.text().replace(',', '.'))
            except ValueError as exc:
                raise ValueError('Enter a positive Reynolds number and chord length.') from exc
            speed = airspeed_for_reynolds(requested, chord, temperature, density)
            self.values['airspeed'].setText(f'{speed:.3f} m/s')
            required_airspeed(requested, chord, temperature, density, maximum)
            if float(f'{speed:.3f}') <= 0:
                raise ValueError('Calculated airspeed is below the existing 0.001 m/s command resolution.')
            self.preview = dict(airspeed_ms=speed, requested_reynolds=requested, chord_mm=chord)
            self.message.setText('AUTO airspeed conversion only. Physical START remains required.')
        except ValueError as exc:
            self.error = str(exc)
            self.message.setText(self.error)

    def unavailable(self, reason):
        self.preview = None
        self.error = reason
        self.message.setText(reason)
        for key in ('temperature', 'viscosity', 'airspeed', 'actual'):
            self.values[key].setText('—')
        self.apply_button.setEnabled(False)
