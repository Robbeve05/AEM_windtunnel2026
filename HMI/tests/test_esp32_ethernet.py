"""ESP32 TCP integration tests. Author: R.E. van Eekelen."""
import asyncio
import unittest
from backend.esp32 import ESP32Client, parse_status, parse_first_fault, status_indication, wait_diagnostics
from backend.models import ManualFrequency
from pydantic import ValidationError

FRAME = ("STATUS state=FAULT fault=EMERGENCY_STOP estop_pressed=1 "
         "reset_required=1 sensor_ok=1 motor_ok=0 pv=11.2 sp=15.0 "
         "pressure_pa=75.1 temperature_c=22.4 sim=0 running=0 "
         "relay=0 commissioned=0 actual_hz=0 out=0 filter_hz=1.0 "
         "mode=MANUAL manual_hz=50 applied_hz=12.5 ethernet=1 "
         "ethernet_link=1 ipv4_configured=1 pi_tcp_connected=1 "
         "pi_heartbeat_received=1 pi_heartbeat_fresh=1")


class ParserTests(unittest.TestCase):
    def test_preparation_starting_and_independent_drive_diagnostics(self):
        for state in ('STOPPED_PREPARE', 'STARTING'):
            data = parse_status(FRAME.replace('state=FAULT', 'state=' + state)
                                + ' diag_v20_run_locked=1 diag_v20_warning=0'
                                ' diag_v20_fault_ack_pending=0 diag_v20_stop_attempts=3')
            self.assertIsNotNone(data)
            self.assertEqual(data['state'], state)
            self.assertEqual(data['running'], 0)
            self.assertEqual(data['diag_v20_run_locked'], 1)
            self.assertEqual(data['diag_v20_stop_attempts'], 3)

    def test_authoritative_status_lamps(self):
        data = parse_status(FRAME + ' led_green=0 led_yellow=1 led_red=1 status_text=INITIALIZING external_leds_enabled=0 output_write_errors=3 output_ack_valid=0')
        indication = status_indication(data)
        self.assertTrue(indication['available'])  # HMI is independent of the disabled physical lamp feature.
        self.assertEqual(data['external_leds_enabled'],0)
        self.assertEqual(data['output_write_errors'],3)
        self.assertEqual(data['output_ack_valid'],0)
        self.assertEqual((indication['green'],indication['yellow'],indication['red']), (False,True,True))
        data['status_text'] = 'EMERGENCY%20STOP%20RELEASED%20-%20RESET%20REQUIRED'
        self.assertEqual(status_indication(data)['text'], 'EMERGENCY STOP RELEASED - RESET REQUIRED')
        # Missing/stale status must not infer a green lamp from RUNNING.
        self.assertFalse(status_indication({'state':'RUNNING'})['available'])
        self.assertIsNone(status_indication(dict(data, stale=True))['green'])
        for suffix in ('led_green=1', 'led_green=1 led_yellow=0 led_red=2',
                       'led_green=-1 led_yellow=0 led_red=0'):
            self.assertIsNone(parse_status(FRAME + ' ' + suffix))

    def test_manual_frequency_model(self):
        self.assertEqual(ManualFrequency(hz=0).hz, 0)
        self.assertEqual(ManualFrequency(hz=50).hz, 50)
        for hz in (-1, 50.001, 55, float('nan'), float('inf')):
            with self.assertRaises(ValidationError):
                ManualFrequency(hz=hz)

    def test_status(self):
        data = parse_status(FRAME)
        self.assertEqual(data["state"], "FAULT")
        self.assertEqual(data["pv"], 11.2)
        for bad in ("", FRAME.replace("pv=11.2", "pv=nan"),
                    FRAME.replace("state=FAULT", "state=UNKNOWN"),
                    FRAME + " state=READY", FRAME.replace("mode=MANUAL", "mode=BAD"),
                    FRAME.replace("manual_hz=50", "manual_hz=nan")):
            self.assertIsNone(parse_status(bad))

    def test_full_firmware_frame_size_limit(self):
        # New counters and lifetime timestamps must not cause a reconnect at 4 KiB.
        large = FRAME + ''.join(f' reserved{i}=4294967295' for i in range(200))
        self.assertGreater(len(large), 4096)
        self.assertLess(len(large), 6144)
        self.assertIsNotNone(parse_status(large))
        self.assertIsNone(parse_status(large + ' padding=' + 'x'*6144))

    def test_wait_diagnostics_protocol_and_multiple_blockers(self):
        frame = parse_status(
            FRAME.replace("state=FAULT", "state=WAIT")
            + " last_fault=NONE diag_blockers=163840 diag_v20_status_word=513"
            " diag_modbus_errors=2 diag_modbus_timeouts=1 diag_v20_ready=0"
            " diag_v20_ready_to_switch_on=0 diag_v20_ready_to_operate=0"
            " diag_v20_operation_enabled=0"
            " diag_v20_off2_active=1 diag_v20_off3_active=1"
            " diag_v20_switch_on_inhibited=1"
            " diag_v20_control_word_requested=1150 diag_v20_control_word_requested_valid=1"
            " diag_v20_control_word_acknowledged=1150 diag_v20_control_word_acknowledged_valid=1"
            " diag_do1_requested=0 diag_do1_acknowledged=0 diag_do1_ack_valid=1"
            " diag_11k3_feedback_ok=1"
            " diag_drive_communication_ok=1 diag_v20_run_commissioned=0"
            " diag_v20_diagnostics_complete=1 diag_drive_fault_free=1"
            " diag_v20_reference_value=5000 diag_v20_reference_valid=1"
            " diag_v20_watchdog_register_value=1000"
            " diag_v20_watchdog_parameter_expected_value=500"
            " diag_v20_watchdog_config_known=0 diag_v20_last_error=0"
            " diag_v20_last_operation=0"
        )
        self.assertIsNotNone(frame)
        self.assertEqual(frame["diag_v20_status_word"], 513)
        waiting, details = wait_diagnostics(frame)
        self.assertIn("V20 RUN commissioning lock active", waiting)
        self.assertIn("Physical RESET sequence not accepted", waiting)
        self.assertIn("V20 status word: 0x0201", details)
        self.assertIn("V20 ready to switch on: FAIL", details)
        self.assertIn("V20 control word requested: 0x047E", details)
        self.assertIn("V20 control word acknowledged: 0x047E", details)
        self.assertIn("Drive communication healthy: OK", details)
        self.assertIn(
            "Watchdog register: 1000 (P2014 expected 500 ms; register mapping unverified)",
            details,
        )

    def test_init_diagnostic_failures_are_reported_without_waiting_for_fault_state(self):
        frame = parse_status(
            FRAME.replace("state=FAULT", "state=INIT")
            + " init_deadline_exceeded=1 diag_modbus_initialized=1"
            " diag_drive_communication_ok=0 diag_modbus_response_received=0"
            " diag_drive_fault_free=0 diag_v20_diagnostics_complete=0"
            " diag_v20_last_error=4 diag_v20_last_operation=1"
        )
        waiting, _ = wait_diagnostics(frame)
        self.assertIn("Initialization deadline exceeded; controller remains safely in INIT", waiting)
        self.assertIn("No recent valid Modbus response", waiting)
        self.assertIn("V20 status-word response missing", waiting)
        self.assertIn("Read-only V20 diagnostics incomplete", waiting)

    def test_legacy_frame_remains_supported(self):
        frame = parse_status(FRAME)
        self.assertIsNotNone(frame)
        waiting, details = wait_diagnostics(frame)
        self.assertEqual(waiting, [])
        self.assertIn("V20 status word: unavailable", details)


class TCPTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.commands = []
        self.send_status = True
        self.callback_gate = None
        async def handler(reader, writer):
            async def send_frames():
                while True:
                    if self.send_status:
                        writer.write((FRAME + "\n").encode())
                        await writer.drain()
                    await asyncio.sleep(0.03)
            producer = asyncio.create_task(send_frames())
            try:
                while line := await reader.readline():
                    cmd = line.decode().strip()
                    self.commands.append(cmd)
                    writer.write(b"OK PING\n" if cmd == "PING" else b"OK QUEUED\n")
                    await writer.drain()
            finally:
                producer.cancel()
                await asyncio.gather(producer, return_exceptions=True)
                writer.close()
                await writer.wait_closed()
        self.server = await asyncio.start_server(handler, "127.0.0.1", 0)
        self.client = ESP32Client("127.0.0.1", self.server.sockets[0].getsockname()[1], status_timeout=0.25)
        self.task = asyncio.create_task(self.client.run(self._frame, lambda *_: None))
        for _ in range(300):
            if self.client.fresh:
                break
            await asyncio.sleep(0.01)
        self.assertTrue(self.client.fresh)

    async def _frame(self, frame):
        if self.callback_gate is not None:
            await self.callback_gate.wait()
        self.latest = frame

    async def asyncTearDown(self):
        self.task.cancel()
        await asyncio.gather(self.task, return_exceptions=True)
        await self.client.close()
        self.server.close()
        await self.server.wait_closed()

    async def test_commands_and_no_remote_start(self):
        snapshot = self.client.snapshot()
        self.assertEqual(snapshot["connection_status"], "ONLINE")
        self.assertTrue(snapshot["tcp_connected"])
        self.assertEqual(snapshot["ethernet_link"], 1)
        self.assertEqual(snapshot["ipv4_configured"], 1)
        self.assertEqual(snapshot["pi_heartbeat_fresh"], 1)
        self.assertEqual(await self.client.command("MODE AUTO"), "OK QUEUED")
        for command in ("MODE MANUAL", "FREQ 0", "FREQ 50", "FREQ 40", "STOP", "PID 1 0 0", "FILTER 1"):
            self.assertEqual(await self.client.command(command), "OK QUEUED")
        self.assertEqual(await self.client.command("FREQ 5.000"), "OK QUEUED")
        self.assertEqual(await self.client.command("SETPOINT 12.000"), "OK QUEUED")
        for denied in ("START", "RESET", "OFF2", "LED RED 0", "LAMP GREEN 1", "SETPOINT nan", "FREQ 50.1", "FREQ 55", "FREQ -1", "FREQ inf", "FREQ nan", "FREQ 50.001", "SETPOINT 40.1", "PID nan 0 0", "PID 1 0 0\n", "FILTER 1\nSTOP"):
            with self.assertRaises(ValueError):
                await self.client.command(denied)
        self.assertEqual(self.client.snapshot()["fault"], "EMERGENCY_STOP")

    async def test_stale_status(self):
        self.send_status = False
        await asyncio.sleep(0.5)
        self.assertEqual(self.client.snapshot()["state"], "UNKNOWN")
        self.assertIn(self.client.snapshot()["connection_status"],
                      {"TCP_CONNECTED_NO_FRESH_STATUS", "OFFLINE"})
        with self.assertRaises(ConnectionError):
            await self.client.command("STOP")

    async def test_heartbeat_independent_of_callback_and_command_lock(self):
        self.callback_gate = asyncio.Event()
        before = self.commands.count('PING')
        async with self.client._lock:
            await asyncio.sleep(2.1)
        self.assertGreaterEqual(self.commands.count('PING') - before, 3)
        self.assertTrue(self.client.fresh)
        self.assertFalse(self.task.done())
        self.callback_gate.set()
        self.assertEqual(await self.client.command('FREQ 50'), 'OK QUEUED')

    async def test_ping_ack_is_not_a_command_ack(self):
        before = self.commands.count('PING')
        for _ in range(4):
            results = await asyncio.gather(self.client.command('FREQ 50'), self.client.command('PING'))
            self.assertEqual(results, ['OK QUEUED', 'OK PING'])
        self.assertGreaterEqual(self.commands.count('PING') - before, 4)

    def test_first_fault_parser(self):
        line = ('FIRST_FAULT sequence=1 timestamp_ms=2000 previous_machine_state=RUNNING '
                'new_machine_state=FAULT first_fault_code=PI_CONNECTION '
                'first_fault_source=pi_heartbeat heartbeat_age_ms=2000 v20_setpoint_hz=50.0 '
                'configuration_failure_reason=FREQUENCY_MODEL_GAIN_INVALID configuration_mode=AUTO '
                'model_gain=0 model_tau_s=0 '
                'modbus_transaction_id=42 modbus_transaction_type=WRITE_CONTROL '
                'modbus_transaction_start_ms=1000 modbus_transaction_deadline_ms=1080 '
                'modbus_rx_bytes=3 modbus_expected_rx_bytes=8 modbus_tx_hex=011000630002 '
                'modbus_rx_hex=011000 '
                'event0=1990,RUNNING,567,1,1,1,1,1,1')
        result = parse_first_fault(line)
        self.assertEqual(result['heartbeat_age_ms'], 2000)
        self.assertEqual(result['modbus_transaction_id'], 42)
        self.assertEqual(result['modbus_transaction_type'], 'WRITE_CONTROL')
        self.assertEqual(result['modbus_tx_hex'], '011000630002')
        self.assertEqual(result['first_fault_code'], 'PI_CONNECTION')
        self.assertEqual(result['v20_setpoint_hz'], 50.0)
        self.assertEqual(result['configuration_failure_reason'], 'FREQUENCY_MODEL_GAIN_INVALID')
        self.assertEqual(parse_first_fault(line.replace('model_gain=0', 'model_gain=nan'))['model_gain'], 'nan')
        self.assertIsNone(parse_first_fault(line+' sequence=2'))
        self.assertIsNone(parse_first_fault(line.replace('50.0','nan')))

    async def test_network_health_classification(self):
        cases = (
            ({"ethernet_link": 0}, "ETHERNET_DISCONNECTED"),
            ({"ipv4_configured": 0}, "CONNECTED_NO_IP"),
            ({"pi_tcp_connected": 0}, "APPLICATION_DISCONNECTED"),
            ({"pi_heartbeat_received": 0}, "HEARTBEAT_NOT_RECEIVED"),
            ({"pi_heartbeat_fresh": 0}, "HEARTBEAT_TIMEOUT"),
        )
        for update, expected in cases:
            self.client._latest = {**parse_status(FRAME), **update}
            self.assertEqual(self.client.snapshot()["connection_status"], expected)


class ReconnectTests(unittest.IsolatedAsyncioTestCase):
    async def test_reconnects_after_server_closes_connection(self):
        connections = 0

        async def handler(reader, writer):
            nonlocal connections
            connections += 1
            if connections == 1:
                writer.write((FRAME + "\n").encode())
                await writer.drain()
                await asyncio.sleep(0.05)
                writer.close()
                try:
                    await writer.wait_closed()
                except (ConnectionError, OSError):
                    pass
                return
            try:
                while True:
                    writer.write((FRAME + "\n").encode())
                    await writer.drain()
                    await asyncio.sleep(0.03)
            except (ConnectionError, OSError):
                pass
            finally:
                writer.close()
                try:
                    await writer.wait_closed()
                except (ConnectionError, OSError):
                    pass

        server = await asyncio.start_server(handler, "127.0.0.1", 0)
        client = ESP32Client("127.0.0.1", server.sockets[0].getsockname()[1],
                             status_timeout=0.25)
        task = asyncio.create_task(client.run(lambda _: asyncio.sleep(0),
                                              lambda *_: None))
        try:
            for _ in range(250):
                if connections >= 2 and client.fresh:
                    break
                await asyncio.sleep(0.01)
            self.assertGreaterEqual(connections, 2)
            self.assertTrue(client.fresh)
            self.assertEqual(client.snapshot()["connection_status"], "ONLINE")
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            await client.close()
            server.close()
            await server.wait_closed()
