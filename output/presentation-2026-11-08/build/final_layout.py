from pathlib import Path
p=Path('output/presentation-2026-11-08')
f=p/'wind_tunnel_progress.tex';s=f.read_text(encoding='utf-8').replace(r'\vspace{.5em}{\footnotesize',r'\vspace{.35em}{\footnotesize');f.write_text(s,encoding='utf-8')
f=p/'diagrams/system_architecture.tex';s=f.read_text(encoding='utf-8')
s=s.replace('Machine + 100 Hz control','100 Hz control').replace('I2C / data validation','Sensor acquisition').replace('E-stop / feedback / enable','E-stop + permission')
s='\n'.join(line for line in s.splitlines() if 'safety.west' not in line)+'\n';f.write_text(s,encoding='utf-8')
