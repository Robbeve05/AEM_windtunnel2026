from pathlib import Path
sep='/'*79
for name in ['V20Simulation.h','V20Simulation.c']:
 p=Path('ESP32')/('include' if name.endswith('.h') else 'src')/name;s=p.read_text();meta=f'{sep}\n// File information\n\n/*\n * {name}\n *\n * Deterministic operational V20 simulation for software verification.\n *\n * Author: R.E. van Eekelen\n */\n\n';s=meta+s
 if name.endswith('.h'):
  s=s.replace('#pragma once',f'{sep}\n// Include guard\n\n#pragma once\n\n{sep}\n// System includes\n').replace('/* Operational test plant',f'{sep}\n// Public data structures\n\n/* Operational test plant').replace('void V20Simulation_Init',f'{sep}\n// Function declarations\n\nvoid V20Simulation_Init')
 else:
  s=s.replace('#include "V20Simulation.h"\n#include <math.h>',f'{sep}\n// System includes\n\n#include <math.h>\n\n{sep}\n// Application includes\n\n#include "V20Simulation.h"\n\n{sep}\n// Module-specific functionality')
  for fn in ['V20Simulation_Init','V20Simulation_Command','V20Simulation_Step','V20Simulation_Status']:
   import re
   s=re.sub(r'\n((?:void|uint16_t) '+fn+r'\()',f'\n{sep}\n// {fn}()\n\n'+r'\1',s)
 p.write_text(s)
p=Path('ESP32/src/V20Modbus.c');s=p.read_text();a=s.index('static V20Simulation simulated_drive;');b=s.index('#endif',a);simvars=s[a:b];s=s[:a]+s[b:];s=s.replace('static V20Context d;', 'static V20Context d;\n#if SIMULATION_MODE\n'+simvars+'#endif');
for fn in ['V20Modbus_RequestFaultAck','V20Modbus_StartConfirmed','V20Modbus_Simulate']:
 s=s.replace('bool '+fn+'(',sep+'\n// '+fn+'()\n\nbool '+fn+'(')
s=s.replace('    const bool stop_required = true;\n','').replace('    d.pending_stop = stop_required;','    d.pending_stop = true;');p.write_text(s)
