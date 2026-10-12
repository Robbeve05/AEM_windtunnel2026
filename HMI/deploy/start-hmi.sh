#!/bin/bash
set -euo pipefail
systemctl --user start windtunnel-hmi.service
"$HOME/HMI/.venv/bin/python" - <<'PY'
import time
from urllib.request import urlopen
for attempt in range(120):
    try:
        with urlopen('http://127.0.0.1:8000/api/status', timeout=2) as response:
            if response.status == 200:
                break
    except OSError:
        time.sleep(1)
else:
    raise SystemExit('HMI server did not become ready; inspect journalctl --user -u windtunnel-hmi')
PY
exec "$HOME/HMI/.venv/bin/python" "$HOME/HMI/desktop.py"
