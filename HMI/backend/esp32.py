"""ESP32 Ethernet STATUS/command adapter. Author: R.E. van Eekelen.
Physical START and RESET are never sent over the network."""
import asyncio
import logging
import math
import re
import time

STATES = {"INIT", "WAIT", "READY", "RUNNING", "STOP", "FAULT"}
FLOATS = {"manual_hz", "applied_hz", "sp", "pv", "out", "sp_ramp", "pressure_pa", "temperature_c", "actual_hz", "desired_hz",
          "control_hz", "modbus_hz", "filter_hz"}
INTS = {"running", "sensor_ok", "motor_ok", "sim", "drive_status", "drive_fault",
        "estop", "relay", "reset_required", "stop", "inputs_valid", "pi", "commissioned",
        "estop_pressed", "safety_relay_ok", "stop_button_ok", "pi_connected", "taps_valid",
        "sample_seq", "sensor_errors", "modbus_errors", "modbus_timeouts", "control_period_us",
        "execution_us", "missed_deadlines", "overruns"}

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
    if out.get("state") not in STATES or "fault" not in out:
        return None
    if not {"pv", "sp", "sensor_ok", "motor_ok", "estop_pressed", "reset_required"} <= out.keys():
        return None
    return out

class ESP32Client:
    def __init__(self, host, port=5000, *, status_timeout=1.0):
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

    def snapshot(self):
        if self.fresh:
            return dict(self._latest, connected=True, stale=False)
        return dict(state="UNKNOWN", fault="UNKNOWN", connected=False, stale=True,
                    estop_pressed=None, reset_required=True, motor_ok=False, sensor_ok=False,
                    pv=None, sp=None, out=None, warning="Fan may still be rotating.")

    async def command(self, command):
        if command == "PING" or command in ("MODE AUTO","MODE MANUAL"):
            pass
        elif re.fullmatch(r"FREQ [0-9]+(?:\.[0-9]{1,3})?",command):
            if float(command.split()[1])>20: raise ValueError("Manual frequency too high")
        elif re.fullmatch(r"SETPOINT [0-9]+(?:\.[0-9]{1,3})?", command):
            pass
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
