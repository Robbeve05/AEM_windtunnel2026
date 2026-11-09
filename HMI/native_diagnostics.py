"""Read-only, grouped controller diagnostics for the native HMI."""
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QAbstractItemView, QHeaderView, QLabel, QTreeWidget, QTreeWidgetItem,
    QVBoxLayout, QWidget,
)

from backend.esp32 import DIAGNOSTIC_SIGNALS, V20_COMM_ERRORS, V20_OPERATIONS, I2C_FIELDS


class DiagnosticsPanel(QWidget):
    """Update cells in place, retaining expansion, selection and scroll position."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName('diagnosticsPanel')
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 6)
        layout.setSpacing(6)
        self.summary = QLabel('Waiting for ESP32 telemetry')
        self.summary.setWordWrap(True)
        self.summary.setObjectName('diagnosticsSummary')
        layout.addWidget(self.summary)
        self.tree = QTreeWidget()
        self.tree.setObjectName('diagnosticsTree')
        self.tree.setHeaderLabels(['Signal', 'Status'])
        self.tree.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.tree.setAlternatingRowColors(True)
        self.tree.setIndentation(14)
        self.tree.setUniformRowHeights(True)
        self.tree.header().setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        self.tree.header().setStretchLastSection(False)
        self.tree.setColumnWidth(0, 180)
        self.tree.setColumnWidth(1, 105)
        layout.addWidget(self.tree)
        self.groups = {}
        self.rows = {}
        for title in ('Controller', 'Connection', 'Safety', 'V20 drive', 'Outputs', 'Sensors', 'Registers', 'I2C bus'):
            item = QTreeWidgetItem(self.tree, [title, ''])
            font = item.font(0)
            font.setBold(True)
            item.setFont(0, font)
            item.setBackground(0, QColor('#f0f1f2'))
            item.setBackground(1, QColor('#f0f1f2'))
            item.setExpanded(title in ('Controller', 'Connection'))
            self.groups[title] = item
        for key, label in (
            ('state', 'Machine state'), ('fault', 'Current fault'), ('last_fault', 'Last fault'),
            ('diag_run_authorized', 'RUN authorized'),
        ):
            self._row('Controller', key, label)
        for key, label in (
            ('connection', 'ESP32 connection'), ('ethernet_link', 'Ethernet link'),
            ('ipv4', 'IPv4 address'), ('tcp', 'TCP application'), ('heartbeat', 'Pi heartbeat'),
        ):
            self._row('Connection', 'system_'+key, label)
        for key, label in DIAGNOSTIC_SIGNALS:
            if key in self.rows:
                continue
            if any(word in key for word in ('estop', 'stop_ok', 'reset', 'safety')):
                group = 'Safety'
            elif any(word in key for word in ('digital_outputs', 'do1', '11k3')):
                group = 'Outputs'
            elif any(word in key for word in ('modbus', 'drive_communication', 'ethernet', 'pi_')):
                group = 'Connection'
            elif any(word in key for word in ('pitot', 'sensor')):
                group = 'Sensors'
            elif any(word in key for word in ('reference', 'watchdog')):
                group = 'Registers'
            else:
                group = 'V20 drive'
            short = label.removeprefix('V20 ').removeprefix('Overall ')
            short = short.replace(' (not a RUN command)', '').replace(' this cycle', '')
            short = short.replace('DO1/11K3 output-driver acknowledgement', 'DO1 driver acknowledgement')
            self._row(group, key, short, label)
        for key, label, group in (
            ('diag_v20_status_word', 'Status word', 'V20 drive'),
            ('diag_v20_control_word_requested', 'Control word requested', 'V20 drive'),
            ('diag_v20_control_word_acknowledged', 'Control word acknowledged', 'V20 drive'),
            ('diag_modbus_errors', 'Modbus errors', 'Connection'),
            ('diag_modbus_timeouts', 'Modbus timeouts', 'Connection'),
            ('diag_v20_last_error', 'Last communication error', 'Connection'),
            ('diag_v20_last_operation', 'Last operation', 'Connection'),
            ('diag_v20_reference_value', 'Reference register (5000 expected)', 'Registers'),
            ('diag_v20_watchdog_register_value', 'Watchdog register', 'Registers'),
        ):
            self._row(group, key, label)
        self._row('Connection', 'modbus_wire_load_permille', 'Estimated wire load (permille)')
        self._row('Connection', 'modbus_transactions_per_minute', 'Transactions per minute (boot average)')
        self._row('Connection', 'modbus_write_cycle_us', 'Write cycle (us)')
        self._row('Connection', 'modbus_status_cycle_us', 'Status cycle (us)')
        self._row('Connection', 'modbus_status_cycle_max_us', 'Maximum status cycle (us)')
        self._row('Connection', 'modbus_write_jitter_max_us', 'Maximum write jitter (us)')
        self._row('Connection', 'modbus_write_deadline_misses', 'Late running write intervals')
        self._row('Connection', 'modbus_transaction_max_us', 'Maximum transaction duration (us)')
        self._row('Connection', 'modbus_idle_us', 'Observed idle before TX (us)')
        self._row('Connection', 'modbus_idle_min_us', 'Minimum observed idle (us)')
        self._row('Connection', 'modbus_idle_max_us', 'Maximum observed idle (us)')
        self._row('Connection', 'modbus_service_gap_max_us', 'Maximum pending service gap (us)')
        self._row('Connection', 'modbus_retries', 'Read retries')
        self._row('Connection', 'modbus_recovered_transactions', 'Recovered read transactions')
        self._row('Connection', 'modbus_final_comm_errors', 'Final communication errors')
        self._row('I2C bus', 'i2c_hz', 'Configured bus speed (Hz)')
        self._row('I2C bus', 'i2c_diag_v', 'Diagnostic format version')
        self.i2c_devices = {}
        for address, name in ((0x20, 'DO expander'), (0x28, 'Pitot'), (0x51, 'RTC'),
                              (0x70, 'Mux 0'), (0x71, 'Mux 1'), (0x29, 'Tap (if configured)')):
            self._i2c_device(f'i2c_{address:02x}', name)
        for key, label in (('output_write_ack_valid', 'Last write I2C ACK (not contact feedback)'),
                           ('output_latch_valid', 'Latch read received'),
                           ('output_latch_image', 'Read output latch'),
                           ('output_latch_match', 'Latch equals requested image'),
                           ('output_read_errors', 'Latch read errors'),
                           ('output_latch_mismatches', 'Latch mismatches')):
            self._row('Outputs', key, label)
        self.update_status({}, stale=True)

    def _i2c_device(self, key, name):
        parent = QTreeWidgetItem(self.groups['I2C bus'], [f'{name} 0x{key[4:].upper()}', 'Not accessed'])
        self.i2c_devices[key] = parent
        for field in I2C_FIELDS:
            label = field.replace('_', ' ')
            row = QTreeWidgetItem(parent, [label, '—'])
            row.setToolTip(0, 'Times in microseconds; last error is the raw ESP-IDF code. Zero counters do not prove device presence.')
            self.rows[key + '_' + field] = row

    def _row(self, group, key, label, tooltip=None):
        row = QTreeWidgetItem(self.groups[group], [label, '—'])
        row.setToolTip(0, tooltip or label)
        self.rows[key] = row

    def update_status(self, drive, *, stale, simulation=False, system=None):
        self.summary.setText('SIMULATION · physical diagnostics unavailable' if simulation else
                             'ESP32 offline or telemetry stale · no current diagnostics' if stale else
                             'LIVE · read only · expand a group for details')
        drive = dict(drive)
        for key, value in list(drive.items()):
            if key.startswith('i2c_') and isinstance(value, dict):
                # Address inventory comes from firmware, including custom tap addresses.
                if key not in self.i2c_devices:
                    self._i2c_device(key, 'Device')
                for field, number in value.items():
                    row_key = key + '_' + field
                    if row_key not in self.rows:
                        self._row('I2C bus', row_key, f'0x{key[4:].upper()} / {field}')
                    drive[row_key] = number
        flags = {key for key, _ in DIAGNOSTIC_SIGNALS}
        unavailable = stale or simulation
        for key, parent in self.i2c_devices.items():
            stats = drive.get(key)
            parent.setText(1, '—' if unavailable or stats is None else
                           'Not accessed' if stats.get('wire_calls') == 0 else
                           'OK' if stats.get('last_result') == 0 else 'ERROR / BUSY')
        for key, row in self.rows.items():
            value = ((system or {}).get(key[7:]) if key.startswith('system_') else
                     None if unavailable else drive.get(key))
            if value is None:
                text = '—'
            elif key in flags:
                # YES/NO describe the signal, not inferred safety or readiness.
                text = 'YES' if value == 1 else 'NO' if value == 0 else '—'
            elif key.endswith('_word') or key in ('diag_v20_control_word_requested', 'diag_v20_control_word_acknowledged'):
                valid_key = key+'_valid'
                valid = key == 'diag_v20_status_word' or drive.get(valid_key) == 1
                text = f'0x{value:04X}' if valid and isinstance(value, int) else '—'
            elif key in ('diag_v20_last_error', 'diag_v20_last_operation'):
                choices = V20_COMM_ERRORS if key.endswith('_error') else V20_OPERATIONS
                text = choices[value] if isinstance(value, int) and 0 <= value < len(choices) else '—'
            else:
                text = str(value)
            if row.text(1) != text:
                row.setText(1, text)
                row.setToolTip(1, text)

    def verticalScrollBar(self):
        return self.tree.verticalScrollBar()

    def horizontalScrollBar(self):
        return self.tree.horizontalScrollBar()

    def toPlainText(self):
        """Plain-text representation for inspection/copying without losing groups."""
        return self.summary.text()+'\n'+'\n'.join(
            group.text(0)+'\n'+'\n'.join(
                f'{group.child(i).text(0)}: {group.child(i).text(1)}'
                for i in range(group.childCount())) for group in self.groups.values())
