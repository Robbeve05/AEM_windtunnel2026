"""ESP32 Ethernet STATUS/command adapter. Author: R.E. van Eekelen.
Physical START and RESET are never sent over the network."""
import asyncio
import logging
import math
import re
import time
from urllib.parse import unquote

STATES = {"INIT", "WAIT", "READY", "RUNNING", "STOP", "FAULT"}
FLOATS = {"air_density", "manual_hz", "applied_hz", "sp", "pv", "out", "sp_ramp", "pressure_pa", "temperature_c", "actual_hz", "desired_hz",
          "control_hz", "modbus_hz", "filter_hz"}
INTS = {"cal_valid", "cal_active", "cal_points", "cal_crc", "cal_version", "outputs_ok", "output_write_errors", "output_image", "output_ack_image", "output_ack_valid", "external_leds_enabled","led_green", "led_yellow", "led_red","running", "sensor_ok", "motor_ok", "sim", "drive_status", "drive_fault",
        "estop", "relay", "reset_required", "stop", "inputs_valid", "pi", "commissioned",
        "estop_pressed", "safety_relay_ok", "stop_button_ok", "pi_connected", "taps_valid",
        "ethernet", "ethernet_link", "ipv4_configured", "pi_tcp_connected",
        "pi_heartbeat_received", "pi_heartbeat_fresh",
        "sample_seq", "sensor_errors", "modbus_errors", "modbus_timeouts", "control_period_us",
        "execution_us", "missed_deadlines", "overruns", "diag_blockers",
        "diag_estop_released", "diag_stop_ok", "diag_reset_detected", "diag_reset_accepted",
        "diag_reset_pending", "diag_safety_inputs_valid", "diag_safety_inputs_fresh",
        "diag_safety_commissioned", "diag_safety_relay_ok", "diag_safety_outputs_healthy",
        "diag_digital_outputs_initialized", "diag_digital_outputs_healthy",
        "diag_modbus_initialized", "diag_modbus_response_received", "diag_v20_status_word",
        "diag_v20_status_healthy", "diag_v20_ready", "diag_drive_communication_ok",
        "diag_modbus_errors", "diag_modbus_timeouts", "diag_v20_run_commissioned",
        "diag_drive_fault_free", "diag_operational_ready", "diag_run_authorized",
        "diag_v20_diagnostics_complete", "diag_v20_reference_valid",
        "diag_v20_reference_config_valid", "diag_v20_reference_value",
        "diag_v20_watchdog_valid", "diag_v20_watchdog_config_known",
        "diag_v20_watchdog_config_valid", "diag_v20_watchdog_register_value",
        "diag_v20_watchdog_parameter_expected_value", "diag_v20_last_error",
        "diag_v20_last_operation", "diag_v20_last_error_register",
        "diag_v20_last_error_expected", "diag_v20_last_error_actual",
        "diag_v20_exception_code", "diag_v20_last_success_age_ms",
        "init_deadline_exceeded",
        "diag_pitot_communication_ok", "diag_sensor_data_valid", "diag_sensor_data_fresh",
        "diag_sensor_ok", "diag_ethernet_link", "diag_pi_heartbeat_received",
        "diag_pi_heartbeat_fresh", "diag_pi_ok"}

WAIT_BLOCKERS = (
    (1 << 0, "E-stop pressed"),
    (1 << 1, "Stop input unhealthy"),
    (1 << 2, "Safety input data invalid"),
    (1 << 3, "Safety input data stale"),
    (1 << 4, "Safety I/O not commissioned"),
    (1 << 5, "Safety relay feedback unhealthy"),
    (1 << 6, "Safety outputs unhealthy"),
    (1 << 7, "VFD fault input active"),
    (1 << 8, "Contactor feedback unhealthy"),
    (1 << 9, "Pitot sensor data invalid or stale"),
    (1 << 10, "Modbus not initialized"),
    (1 << 11, "No recent V20 status response"),
    (1 << 12, "V20 status word not healthy"),
    (1 << 13, "V20 communication/drive status unhealthy"),
    (1 << 14, "V20 ready signal unavailable"),
    (1 << 15, "V20 RUN commissioning lock active"),
    (1 << 16, "Raspberry Pi heartbeat not healthy"),
    (1 << 17, "Physical RESET sequence not accepted"),
    (1 << 18, "Reset feedback check pending"),
    (1 << 19, "V20 status reports an active drive fault"),
    (1 << 20, "V20 configuration does not match expected values"),
    (1 << 21, "V20 is communicating but operationally inhibited"),
)

V20_COMM_ERRORS = (
    "NONE", "UART_INIT_FAILED", "UART_TX_FAILED", "UART_RX_FAILED", "TIMEOUT",
    "CRC_ERROR", "INVALID_FRAME", "MODBUS_EXCEPTION", "UNEXPECTED_REGISTER_VALUE",
    "STALE_RESPONSE",
)
V20_OPERATIONS = ("NONE", "READ_STATUS", "READ_REFERENCE", "READ_WATCHDOG", "WRITE_CONTROL")

DIAGNOSTIC_SIGNALS = (
    ("diag_estop_released", "E-stop released"),
    ("diag_stop_ok", "Stop input healthy"),
    ("diag_reset_detected", "Reset button sequence detected"),
    ("diag_reset_accepted", "Reset sequence accepted"),
    ("diag_reset_pending", "Reset feedback pending"),
    ("diag_safety_inputs_valid", "Safety input data valid"),
    ("diag_safety_inputs_fresh", "Safety input data fresh"),
    ("diag_safety_commissioned", "Safety I/O commissioned"),
    ("diag_safety_relay_ok", "Safety relay feedback"),
    ("diag_safety_outputs_healthy", "Safety outputs healthy"),
    ("diag_digital_outputs_initialized", "Digital outputs initialized"),
    ("diag_digital_outputs_healthy", "Digital outputs healthy"),
    ("diag_modbus_initialized", "Modbus initialized"),
    ("diag_modbus_response_received", "Recent V20 response received"),
    ("diag_v20_status_healthy", "V20 status response is fault-free"),
    ("diag_drive_fault_free", "V20 reports no active drive fault"),
    ("diag_v20_ready", "V20 operational status bits ready"),
    ("diag_operational_ready", "V20 operational readiness"),
    ("diag_drive_communication_ok", "Drive communication healthy"),
    ("diag_v20_run_commissioned", "V20 RUN commissioned"),
    ("diag_v20_diagnostics_complete", "Read-only V20 diagnostics complete"),
    ("diag_v20_reference_valid", "Reference register read"),
    ("diag_v20_reference_config_valid", "Reference frequency matches expected"),
    ("diag_v20_watchdog_valid", "Watchdog register read"),
    ("diag_v20_watchdog_config_known", "Watchdog register mapping confirmed"),
    ("diag_v20_watchdog_config_valid", "Watchdog configuration matches expected"),
    ("diag_run_authorized", "Motor RUN currently authorized"),
    ("diag_pitot_communication_ok", "Pitot communication healthy"),
    ("diag_sensor_data_valid", "Sensor data valid"),
    ("diag_sensor_data_fresh", "Sensor data fresh"),
    ("diag_sensor_ok", "Overall sensor_ok"),
    ("diag_ethernet_link", "Ethernet link"),
    ("diag_pi_heartbeat_received", "Pi heartbeat received"),
    ("diag_pi_heartbeat_fresh", "Pi heartbeat fresh"),
    ("diag_pi_ok", "Overall pi_ok"),
)


def wait_diagnostics(drive):
    """Return blockers and a readable signal list from the ESP32's diagnostic frame."""
    waiting_for = []
    details = [
        f"State: {drive.get('state', 'UNKNOWN')} · last fault: {drive.get('last_fault', 'UNKNOWN')}",
        f"V20 status word: 0x{drive['diag_v20_status_word']:04X}"
        if isinstance(drive.get("diag_v20_status_word"), int)
        else "V20 status word: unavailable",
        f"Modbus errors/timeouts: {drive.get('diag_modbus_errors', 'unavailable')}/"
        f"{drive.get('diag_modbus_timeouts', 'unavailable')}",
        f"V20 communication error/operation: "
        f"{_enum_name(drive.get('diag_v20_last_error'), V20_COMM_ERRORS)}/"
        f"{_enum_name(drive.get('diag_v20_last_operation'), V20_OPERATIONS)}",
        f"Reference register: {drive.get('diag_v20_reference_value', 'unavailable')} "
        f"(expected 5000 centi-Hz)",
        f"Watchdog register: {drive.get('diag_v20_watchdog_register_value', 'unavailable')} "
        f"(P2014 expected {drive.get('diag_v20_watchdog_parameter_expected_value', 'unavailable')} ms; "
        f"register mapping "
        f"{'confirmed' if drive.get('diag_v20_watchdog_config_known') == 1 else 'unverified'})",
    ]
    for key, label in DIAGNOSTIC_SIGNALS:
        value = drive.get(key)
        details.append(f"{label}: {'OK' if value == 1 else 'FAIL' if value == 0 else 'unavailable'}")
    blockers = drive.get("diag_blockers")
    if isinstance(blockers, int):
        waiting_for.extend(label for bit, label in WAIT_BLOCKERS if blockers & bit)
        unknown = blockers & ~sum(bit for bit, _ in WAIT_BLOCKERS)
        if unknown:
            waiting_for.append(f"Unknown blocker flags: 0x{unknown:08X}")
    elif drive.get("state") == "WAIT":
        waiting_for.append("Detailed WAIT diagnostics unavailable (update ESP32 firmware)")
    if drive.get("state") == "INIT":
        if drive.get("init_deadline_exceeded") == 1:
            waiting_for.append("Initialization deadline exceeded; controller remains safely in INIT")
        if drive.get("diag_modbus_initialized") == 0:
            waiting_for.append("V20 UART initialization failed")
        if drive.get("diag_drive_communication_ok") == 0:
            waiting_for.append("No recent valid Modbus response")
        if drive.get("diag_modbus_response_received") == 0:
            waiting_for.append("V20 status-word response missing")
        if drive.get("diag_drive_fault_free") == 0:
            waiting_for.append("V20 fault status is active or unavailable")
        if drive.get("diag_v20_diagnostics_complete") == 0:
            waiting_for.append("Read-only V20 diagnostics incomplete")
    return waiting_for, details


def _enum_name(value, choices):
    return choices[value] if isinstance(value, int) and 0 <= value < len(choices) else "unavailable"

def parse_status(line):
    if not line.startswith("STATUS ") or len(line) > 4096:
        return None
    out = {}
    for token in line[7:].split():
        if "=" not in token:
            return None
        key, val = token.split("=", 1)
        if not key or not val or key in out:
            return None
        try:
            if key in FLOATS or re.fullmatch(r"p(?:[0-9]|1[0-5])", key):
                val = float(val)
                if not math.isfinite(val):
                    return None
            elif key in INTS:
                val = int(val)
        except ValueError:
            return None
        out[key] = val
    if out.get("mode") not in {"AUTO", "MANUAL"}:
        return None
    if out.get("state") not in STATES or "fault" not in out:
        return None
    if not {"pv", "sp", "manual_hz", "applied_hz", "actual_hz", "sensor_ok", "motor_ok", "estop_pressed", "reset_required"} <= out.keys():
        return None
    flags = {"led_green", "led_yellow", "led_red"}
    if flags & out.keys() and (not flags <= out.keys() or any(out[k] not in (0, 1) for k in flags)):
        return None
    return out


def status_indication(drive):
    """Use ESP32 StatusIndicators colors; never guess from local machine state."""
    keys = ("led_green", "led_yellow", "led_red")
    available = not drive.get('stale', False) and all(drive.get(k) in (0, 1) for k in keys)
    return dict(available=available,
                green=bool(drive['led_green']) if available else None,
                yellow=bool(drive['led_yellow']) if available else None,
                red=bool(drive['led_red']) if available else None,
                text=unquote(drive.get('status_text', 'STATUS UNKNOWN')) if available else
                     'ESP32 disconnected/stale; fan may still rotate')

class ESP32Client:
    def __init__(self, host, port=5000, *, status_timeout=1.0,
                 max_airspeed_ms=None, max_frequency_hz=None):
        from backend.models import FIRMWARE_MAX_AIRSPEED_MS, FIRMWARE_MAX_FREQUENCY_HZ
        self.max_airspeed_ms = float(max_airspeed_ms if max_airspeed_ms is not None else FIRMWARE_MAX_AIRSPEED_MS)
        self.max_frequency_hz = float(max_frequency_hz if max_frequency_hz is not None else FIRMWARE_MAX_FREQUENCY_HZ)
        if not (0 < self.max_airspeed_ms <= FIRMWARE_MAX_AIRSPEED_MS and
                0 < self.max_frequency_hz <= FIRMWARE_MAX_FREQUENCY_HZ):
            raise ValueError('Configured limits exceed validated ESP32 firmware ceilings')
        self.host, self.port, self.status_timeout = host, port, status_timeout
        self._writer = self._reader = None
        self._connected = False
        self._last = 0
        self._latest = {}
        self._lock = asyncio.Lock()
        self._pending = None
        self._stopped = False

    @property
    def fresh(self):
        return self._connected and self._last > 0 and time.monotonic() - self._last <= self.status_timeout

    @property
    def connection_status(self):
        if not self._connected:
            return "OFFLINE"
        if not self.fresh:
            return "TCP_CONNECTED_NO_FRESH_STATUS"
        if self._latest.get("ethernet_link") == 0:
            return "ETHERNET_DISCONNECTED"
        if self._latest.get("ipv4_configured") == 0:
            return "CONNECTED_NO_IP"
        if self._latest.get("pi_tcp_connected") == 0:
            return "APPLICATION_DISCONNECTED"
        heartbeat = self._latest.get("pi_heartbeat_fresh")
        received = self._latest.get("pi_heartbeat_received")
        if heartbeat is None:
            heartbeat = self._latest.get("pi_connected")
        if received == 0:
            return "HEARTBEAT_NOT_RECEIVED"
        if heartbeat == 0:
            return "HEARTBEAT_TIMEOUT"
        if heartbeat is None or self._latest.get("ethernet_link") is None or self._latest.get("ipv4_configured") is None:
            return "STATUS_FRESH"
        return "ONLINE"

    def snapshot(self):
        if self.fresh:
            return dict(self._latest, connected=True, tcp_connected=True, stale=False,
                        connection_status=self.connection_status)
        return dict(state="UNKNOWN", fault="UNKNOWN", connected=False, stale=True,
                    tcp_connected=self._connected, connection_status=self.connection_status,
                    ethernet_link=None, ipv4_configured=None,
                    pi_heartbeat_received=None, pi_heartbeat_fresh=None,
                    estop_pressed=None, reset_required=True, motor_ok=False, sensor_ok=False,
                    pv=None, sp=None, out=None, warning="Fan may still be rotating.")

    async def command(self, command):
        if "\n" in command or "\r" in command:
            raise ValueError("Commands must be one line")
        if command in ("PING", "STOP") or command in ("MODE AUTO","MODE MANUAL"):
            pass
        elif re.fullmatch(r"FREQ [0-9]+(?:\.[0-9]{1,3})?",command):
            if float(command.split()[1]) > self.max_frequency_hz:
                raise ValueError(f"Frequency exceeds {self.max_frequency_hz:g} Hz maximum")
        elif re.fullmatch(r"SETPOINT [0-9]+(?:\.[0-9]{1,3})?", command):
            if float(command.split()[1]) > self.max_airspeed_ms:
                raise ValueError(f"Airspeed exceeds {self.max_airspeed_ms:g} m/s maximum")
        elif command in ("CAL_INFO", "CAL_COMMIT", "CAL_ABORT", "CAL_RESET") or re.fullmatch(r"CAL_(?:BEGIN|POINT) [0-9]+ [0-9]+ [0-9]+", command):
            pass
        elif command.startswith("PID "):
            parts = command.split(" ")
            if len(parts) != 4 or any(not math.isfinite(float(v)) or not 0 <= float(v) <= 1000 for v in parts[1:]):
                raise ValueError("Invalid PID")
        elif command.startswith("FILTER "):
            try:
                f = float(command.split(" ", 1)[1])
            except ValueError as ex:
                raise ValueError("Invalid filter value") from ex
            if not math.isfinite(f) or not 0.1 <= f <= 10:
                raise ValueError("Filter out of range")
        else:
            raise ValueError("Unsupported command; START and RESET are physical only")
        async with self._lock:
            if not self.fresh or self._writer is None:
                raise ConnectionError("ESP32 not connected or status is stale")
            loop = asyncio.get_running_loop()
            future = loop.create_future()
            self._pending = future
            try:
                self._writer.write((command + "\n").encode("ascii"))
                await asyncio.wait_for(self._writer.drain(), 0.5)
                reply = await asyncio.wait_for(future, 1.0)
                if reply.startswith("ERR "):
                    raise ValueError("ESP32 rejected command: " + reply[4:])
                if not reply.startswith("OK "):
                    raise ValueError("Invalid ESP32 acknowledgement")
                return reply
            finally:
                self._pending = None

    async def _read(self, callback):
        while not self._stopped:
            line = await asyncio.wait_for(self._reader.readline(), 1.25)
            if not line or len(line) > 4096:
                raise ConnectionError("ESP32 lost or oversized telemetry")
            try:
                text = line.decode("ascii").strip()
            except UnicodeDecodeError:
                continue
            if text.startswith("STATUS "):
                data = parse_status(text)
                if data:
                    if self._last == 0:
                        logging.info(
                            "ESP32 STATUS state=%s ethernet=%s ipv4=%s pi_tcp=%s heartbeat=%s",
                            data.get("state"), data.get("ethernet_link"),
                            data.get("ipv4_configured"), data.get("pi_tcp_connected"),
                            data.get("pi_heartbeat_fresh"),
                        )
                    self._latest = data
                    self._last = time.monotonic()
                    await callback(data)
            elif text.startswith(("OK ", "ERR ")) and self._pending and not self._pending.done():
                self._pending.set_result(text)

    async def _ping(self):
        while not self._stopped:
            await asyncio.sleep(0.5)
            if not self.fresh:
                raise ConnectionError("ESP32 telemetry stale")
            await self.command("PING")

    async def run(self, on_status, on_event):
        self._stopped = False
        delay = 0.5
        while not self._stopped:
            try:
                logging.info("Connecting to ESP32 TCP listener %s:%d", self.host, self.port)
                self._reader, self._writer = await asyncio.wait_for(
                    asyncio.open_connection(self.host, self.port, limit=8192), 3.0)
                self._connected, self._last = True, 0
                on_event("ESP32 Ethernet connected; awaiting STATUS.", "info")
                reader = asyncio.create_task(self._read(on_status))
                ping = asyncio.create_task(self._ping())
                try:
                    done, _ = await asyncio.wait((reader, ping), return_when=asyncio.FIRST_COMPLETED)
                    for task in done:
                        task.result()
                finally:
                    reader.cancel()
                    ping.cancel()
                    await asyncio.gather(reader, ping, return_exceptions=True)
                delay = 0.5
            except asyncio.CancelledError:
                raise
            except (OSError, ConnectionError, ValueError, asyncio.TimeoutError) as ex:
                logging.warning("ESP32 TCP: %s", ex)
            finally:
                if self._connected:
                    on_event("ESP32 status UNKNOWN; fan may still rotate.", "warning")
                self._connected, self._last, self._latest = False, 0, {}
                if self._pending and not self._pending.done():
                    self._pending.set_exception(ConnectionError("ESP32 connection lost"))
                self._pending = None
                if self._writer is not None:
                    self._writer.close()
                    try:
                        await asyncio.wait_for(self._writer.wait_closed(), 0.5)
                    except (OSError, asyncio.TimeoutError):
                        pass
                self._reader = self._writer = None
            if not self._stopped:
                await asyncio.sleep(delay)
                delay = min(delay * 2, 5.0)

    async def close(self):
        self._stopped = True
        if self._writer is not None:
            self._writer.close()
