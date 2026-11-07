"""No-hardware tests for the Windows HMI/backend compatibility launcher."""
import io
import json
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError

import start_windows


class LauncherTests(unittest.TestCase):
    def test_detects_new_backend_and_required_route(self):
        doc = {
            "info": {"title": "Avans Windtunnel HMI"},
            "paths": {start_windows.EXPECTED_ROUTE: {"post": {}}},
        }
        with patch.object(start_windows, "urlopen",
                          return_value=io.BytesIO(json.dumps(doc).encode())):
            self.assertEqual(start_windows.inspect_backend(), "current")

    def test_rejects_stale_backend_missing_graph_endpoint(self):
        doc = {"info": {"title": "Avans Windtunnel HMI"}, "paths": {"/api/config": {}}}
        with patch.object(start_windows, "urlopen",
                          return_value=io.BytesIO(json.dumps(doc).encode())):
            self.assertEqual(start_windows.inspect_backend(), "incompatible")

    def test_rejects_unrelated_webserver_or_404(self):
        with patch.object(start_windows, "urlopen",
                          side_effect=HTTPError(start_windows.OPENAPI_URL, 404,
                                                "Not Found", {}, None)):
            self.assertEqual(start_windows.inspect_backend(), "incompatible")

    def test_unreachable_port_is_not_confused_with_old_backend(self):
        with patch.object(start_windows, "urlopen",
                          side_effect=URLError("Connection refused")):
            self.assertEqual(start_windows.inspect_backend(), "absent")

    def test_launcher_disables_verbose_http_access_logging(self):
        # Keep the native HMI status polling rate unchanged: 10 Hz.
        # This test checks only how the locally spawned uvicorn is configured.
        with patch.object(start_windows, "inspect_backend", side_effect=[
            "absent", "current",
        ]):
            with patch.object(start_windows.subprocess, "Popen") as popen:
                with patch.object(start_windows.subprocess, "call", return_value=0):
                    popen.return_value.poll.return_value = None
                    self.assertEqual(start_windows.run(), 0)
                    args = popen.call_args.args[0]
                    self.assertIn("--no-access-log", args)
                    self.assertEqual(args[args.index("--port") + 1], "8000")
                    self.assertEqual(args[args.index("--workers") + 1], "1")
                    popen.return_value.terminate.assert_called_once()

    def test_never_kills_an_existing_incompatible_backend(self):
        with patch.object(start_windows, "inspect_backend", return_value="incompatible"):
            with patch.object(start_windows.subprocess, "Popen") as popen:
                self.assertEqual(start_windows.run(), 2)
                popen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
