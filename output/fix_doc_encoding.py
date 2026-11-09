from pathlib import Path
for file in ['docs/V20_STARTUP_AUDIT.md','ESP32/README.md','README.md','docs/hardware/V20_MODBUS.md','docs/hardware/MACHINE-IO.md']:
 p=Path(file); text=p.read_bytes().decode('utf-8',errors='surrogateescape')
 text=''.join(bytes([ord(c)-0xdc00]).decode('cp1252',errors='replace') if 0xdc80<=ord(c)<=0xdcff else c for c in text)
 p.write_text(text,encoding='utf-8')
