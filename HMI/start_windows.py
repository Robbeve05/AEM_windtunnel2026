"""Launch the matching FastAPI backend and native Qt HMI on Windows.

Run from a Windows venv:  .\\.venv\\Scripts\\python.exe start_windows.py

The launcher never kills a process that it did not start. If port 8000 is
occupied by an outdated backend, it reports a version mismatch instead of
silently launching a mismatched native interface.
"""
from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parent
OPENAPI_URL = "http://127.0.0.1:8000/openapi.json"
EXPECTED_ROUTE = "/api/aerodynamics/experiments/{identifier}/analyze"
EXPECTED_TITLE = "Avans Windtunnel HMI"


def inspect_backend() -> str:
    """Return 'absent', 'current', or 'incompatible'; no process side effects."""
    try:
        with urlopen(OPENAPI_URL, timeout=2) as reply:
            document = json.load(reply)
    except HTTPError:
        return "incompatible"
    except URLError:
        return "absent"
    except (ValueError, KeyError, TypeError, AttributeError):
        return "incompatible"
    if (document.get("info", {}).get("title") == EXPECTED_TITLE
            and EXPECTED_ROUTE in document.get("paths", {})):
        return "current"
    return "incompatible"


def run() -> int:
    state = inspect_backend()
    if state == "incompatible":
        print(
            "ERROR: Port 8000 is occupied by an old or incompatible HMI backend.\n"
            "Close the old uvicorn process, then run this launcher again.\n"
            "To locate it in PowerShell:\n"
            "  Get-NetTCPConnection -LocalPort 8000 -State Listen | "
            "Select-Object -ExpandProperty OwningProcess -Unique\n"
            "Only stop the process after checking that it belongs to this HMI.",
            file=sys.stderr,
        )
        return 2

    server = None
    try:
        if state == "absent":
            print("Starting FastAPI backend from this HMI checkout...")
            server = subprocess.Popen(
                [
                    sys.executable, "-m", "uvicorn", "main:app",
                    "--host", "127.0.0.1", "--port", "8000", "--workers", "1",
                    # Native HMI polls /api/status ~10 times per second.
                    # Silence routine HTTP access lines, but keep actual errors
                    # and ESP32 connection warnings visible in this console.
                    "--no-access-log",
                ],
                cwd=ROOT,
            )
            for _ in range(80):
                if server.poll() is not None:
                    raise RuntimeError("The backend exited unexpectedly; inspect its output")
                state = inspect_backend()
                if state == "current":
                    break
                if state == "incompatible":
                    raise RuntimeError("Port 8000 became occupied by an incompatible backend")
                time.sleep(0.25)
            else:
                raise RuntimeError("The local backend did not become ready")

        print("Opening native Qt HMI using current backend...")
        return subprocess.call([sys.executable, str(ROOT / "desktop.py")], cwd=ROOT)
    finally:
        if server is not None and server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=8)
            except subprocess.TimeoutExpired:
                print("Backend did not stop cleanly; inspect the process before retrying",
                      file=sys.stderr)


if __name__ == "__main__":
    try:
        raise SystemExit(run())
    except (OSError, RuntimeError) as exc:
        print(f"HMI launcher failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
