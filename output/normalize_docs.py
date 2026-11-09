from pathlib import Path
for file in ['docs/V20_STARTUP_AUDIT.md','ESP32/README.md','README.md','docs/hardware/V20_MODBUS.md','docs/hardware/MACHINE-IO.md']:
 p=Path(file); s=p.read_bytes().replace(b'\r',b'').decode('utf-8')
 if file=='ESP32/README.md':
  s=s.replace('`INIT → WAIT → READY → RUNNING → STOP → FAULT`','`INIT → WAIT → STOPPED_PREPARE → READY → STARTING → RUNNING → STOP → READY`, with latched FAULT branches')
 p.write_bytes(s.encode('utf-8'))
