from pathlib import Path
p=Path('ESP32/src/ControlTask.c');s=p.read_text();a=s.index('    /* A new physical START');b=s.index('    /* Disarm immediately',a);s=s[:a]+s[a:b].replace('FAULT_INTERNAL','FAULT_CONFIGURATION')+s[b:];p.write_text(s)
p=Path('ESP32/tests/test_control.c');s=p.read_text().replace('c.machine.fault==FAULT_INTERNAL && arms==before','c.machine.fault==FAULT_CONFIGURATION && arms==before');p.write_text(s)
