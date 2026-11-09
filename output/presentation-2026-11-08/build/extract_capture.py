from pathlib import Path
p=Path('output/presentation-2026-11-08/reproduction')
s=(p/'make_assets.py').read_text(encoding='utf-8'); body=s[s.index('# Actual native UI widgets'):]
header='''from pathlib import Path
import argparse,asyncio,json,math,os,random,sys,tempfile,time
from datetime import datetime,timezone
from unittest.mock import patch
PACKAGE=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--repo',type=Path,required=True)
args=parser.parse_args(); REPO=args.repo.resolve()
sys.path.insert(0,str(REPO/'HMI'))
'''
(p/'capture_hmi.py').write_text(header+body,encoding='utf-8')
