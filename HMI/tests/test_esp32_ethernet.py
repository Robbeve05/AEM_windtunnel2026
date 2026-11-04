"""ESP32 TCP integration tests. Author: R.E. van Eekelen."""
import asyncio
import unittest
from backend.esp32 import ESP32Client, parse_status, status_indication, wait_diagnostics
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

    def test_wait_diagnostics_protocol_and_multiple_blockers(self):
        frame = parse_status(
            FRAME.replace("state=FAULT", "state=WAIT")
            + " last_fault=NONE diag_blockers=163840 diag_v20_status_word=513"
            " diag_modbus_errors=2 diag_modbus_timeouts=1 diag_v20_ready=0"
            " diag_drive_communication_ok=1 diag_v20_run_commissioned=0"
        )
        self.assertIsNotNone(frame)
        self.assertEqual(frame["diag_v20_status_word"], 513)
        waiting, details = wait_diagnostics(frame)
        self.assertIn("V20 RUN commissioning lock active", waiting)
        self.assertIn("Physical RESET sequence not accepted", waiting)
        self.assertIn("V20 status word: 0x0201", details)
        self.assertIn("V20 ready (software gate applied): FAIL", details)
        self.assertIn("Drive communication healthy: OK", details)

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
