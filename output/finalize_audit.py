from pathlib import Path
p=Path('docs/V20_STARTUP_AUDIT.md')
s=p.read_bytes().replace(b'\r',b'').decode('utf-8')
baseline=s.split('## 16.',1)[0].rstrip()
update=Path('output/audit_update.md').read_text(encoding='utf-8-sig')
p.write_bytes((baseline+'\n\n'+update.replace('\r','').strip()+'\n').encode('utf-8'))
