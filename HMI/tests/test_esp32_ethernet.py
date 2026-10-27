"""ESP32 TCP integration tests. Author: R.E. van Eekelen."""
import asyncio
import unittest
from backend.esp32 import ESP32Client, parse_status

FRAME = ("STATUS state=FAULT fault=EMERGENCY_STOP estop_pressed=1 "
         "reset_required=1 sensor_ok=1 motor_ok=0 pv=11.2 sp=15.0 "
         "pressure_pa=75.1 temperature_c=22.4 sim=0 running=0 "
         "relay=0 commissioned=0 actual_hz=0 out=0 filter_hz=1.0")


class ParserTests(unittest.TestCase):
    def test_status(self):
        data = parse_status(FRAME)
        self.assertEqual(data["state"], "FAULT")
        self.assertEqual(data["pv"], 11.2)
        for bad in ("", FRAME.replace("pv=11.2", "pv=nan"),
                    FRAME.replace("state=FAULT", "state=UNKNOWN"),
                    FRAME + " state=READY"):
            self.assertIsNone(parse_status(bad))


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
        for _ in range(100):
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
        self.assertEqual(await self.client.command("STOP"), "OK QUEUED")
        self.assertEqual(await self.client.command("SETPOINT 12.000"), "OK QUEUED")
        for denied in ("START", "RESET", "OFF2", "SETPOINT nan"):
            with self.assertRaises(ValueError):
                await self.client.command(denied)
        self.assertEqual(self.client.snapshot()["fault"], "EMERGENCY_STOP")

    async def test_stale_status(self):
        self.send_status = False
        await asyncio.sleep(0.5)
        self.assertEqual(self.client.snapshot()["state"], "UNKNOWN")
        with self.assertRaises(ConnectionError):
            await self.client.command("STOP")
