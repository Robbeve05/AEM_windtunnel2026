"""Generate plots and actual native-HMI captures. Never connects to hardware."""
from pathlib import Path
import argparse
import asyncio
import json
import math
import os
import random
import sys
import tempfile
import time
from datetime import datetime, timezone
from unittest.mock import patch

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

PACKAGE = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, default=PACKAGE.parents[1])
args = parser.parse_args()
REPO = args.repo.resolve()
sys.path.insert(0, str(REPO/'HMI'))
from backend.fan_dynamics import simc_parameters, simulate_candidate

RED, BLUE, GREY = '#C5221F', '#233D60', '#7B8794'
plt.rcParams.update({'font.family':'DejaVu Sans', 'font.size':12,
                     'axes.spines.top':False, 'axes.spines.right':False,
                     'axes.labelsize':12, 'legend.fontsize':11, 'pdf.fonttype':42})
d = np.genfromtxt(PACKAGE/'plots/simulation.csv', delimiter=',', names=True)
p = json.loads((PACKAGE/'plots/parameters.json').read_text())
candidate = simc_parameters(p['frequency_K'], p['frequency_tau'], 0, p['frequency_lambda'])
assert abs(candidate['kp']-p['frequency_Kp']) < 1e-6
# Reuse the existing Python candidate simulator as an independent nominal smoke
# check. The comparative traces use the exact production C controller functions.
python_candidate = simulate_candidate(p['frequency_K'],p['frequency_tau'],0,candidate)
(PACKAGE/'plots/python_candidate.json').write_text(json.dumps(python_candidate,indent=2),encoding='utf-8')

def save(fig, name):
    fig.tight_layout(pad=1.0)
    fig.savefig(PACKAGE/'plots'/f'{name}.pdf',bbox_inches='tight')
    fig.savefig(PACKAGE/'plots'/f'{name}.png',dpi=190,bbox_inches='tight')
    plt.close(fig)

t=d['time_s']
fig,ax=plt.subplots(figsize=(10.2,4.5))
ax.plot(t,d['target_ms'],color='black',ls='--',lw=1.4,label='Target: 20 m/s')
ax.plot(t,d['reference_ms'],color=GREY,ls=':',lw=1.6,label='Shared shaped reference')
ax.plot(t,d['frequency_speed_ms'],color=RED,lw=2,label='Frequency PI')
ax.plot(t,d['torque_speed_ms'],color=BLUE,lw=2,ls='-.',label='Torque-model PI + feedforward')
ax.set(xlim=(0,44),ylim=(-.5,22),xlabel='Time [s]',ylabel='Filtered airspeed [m/s]')
ax.grid(alpha=.2); ax.legend(loc='lower right',frameon=False)
save(fig,'airspeed_response')

fig,ax=plt.subplots(figsize=(10.2,4.1))
for prefix,color,label in [('frequency',RED,'Frequency PI'),('torque',BLUE,'Torque-model PI + FF')]:
    ax.plot(t,d[prefix+'_command_hz'],color=color,lw=1.8,label=label)
    ax.plot(t,d[prefix+'_actual_hz'],color=color,lw=.9,ls=':',alpha=.8)
ax.axvline(45,color=GREY,lw=1,ls='--')
ax.text(46,ax.get_ylim()[1]*.94,'Shared disturbance starts',fontsize=10,color=GREY)
ax.set(xlim=(0,120),xlabel='Time [s]',ylabel='V20 frequency [Hz]')
ax.grid(alpha=.2); ax.legend(loc='lower right',frameon=False)
save(fig,'frequency_output')

fig,ax=plt.subplots(figsize=(10.2,4.1))
ax.plot(t,d['disturbance_ms'],color=GREY,lw=1,ls=':',label='Applied output disturbance')
for prefix,color,label in [('frequency',RED,'Frequency PI'),('torque',BLUE,'Torque-model PI + FF')]:
    ax.plot(t,d['reference_ms']-d[prefix+'_speed_ms'],color=color,lw=1.8,label=label)
ax.axhline(0,color='black',lw=.5)
ax.set(xlim=(45,120),xlabel='Time [s]',ylabel='Tracking error [m/s]')
ax.grid(alpha=.2); ax.legend(loc='upper right',frameon=False)
save(fig,'disturbance_error')

metrics={}
for prefix in ['frequency','torque']:
    step=(t>=5)&(t<45); y=d[prefix+'_speed_ms'][step]; ts=t[step]
    t10=ts[np.flatnonzero(y>=2)[0]]; t90=ts[np.flatnonzero(y>=18)[0]]
    outside=np.flatnonzero(np.abs(y-20)>.4)
    settling=float(ts[outside[-1]+1]-5) if len(outside) and outside[-1]+1<len(ts) else None
    disturbed=t>=45
    metrics[prefix]=dict(rise_10_90_s=float(t90-t10),settling_2percent_after_step_s=settling,
        overshoot_percent=max(0,float((max(y)-20)/20*100)),
        steady_state_error_40_45_ms=float(np.mean(20-d[prefix+'_speed_ms'][(t>=40)&(t<45)])),
        max_command_hz=float(max(d[prefix+'_command_hz'])),
        IAE_shaped_5_45_m=float(np.trapezoid(abs(d['reference_ms'][step]-y),ts)),
        disturbance_RMSE_ms=float(np.sqrt(np.mean((20-d[prefix+'_speed_ms'][disturbed])**2))))
(PACKAGE/'plots/metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
def n(value): return '--' if value is None else f'{value:.2f}'
rows=[]
for prefix,label in [('frequency','Frequency PI'),('torque','Torque model')]:
    m=metrics[prefix]
    rows.append(label+' & '+ ' & '.join(n(m[k]) for k in ['rise_10_90_s','settling_2percent_after_step_s','overshoot_percent','disturbance_RMSE_ms'])+r' \\')
(PACKAGE/'plots/metrics_table.tex').write_text('\n'.join(rows),encoding='utf-8')
print(json.dumps(metrics,indent=2))

# Actual native UI widgets, populated through the real local simulation backend.
os.environ['QT_QPA_PLATFORM']='offscreen'
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer, QRect
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtGui import QImage,QPainter,QColor,QFontDatabase,QFont
from desktop import HMIWindow
from main import Controller
from backend.aerodynamics.models import ExperimentConfig,AnalysisSettings

app=QApplication.instance() or QApplication([])
# Offscreen Qt does not automatically enumerate system fonts in this runtime.
# Load the same Arial family explicitly; this changes capture setup, not HMI code.
for font in ['arial.ttf','arialbd.ttf','ariali.ttf','arialbi.ttf']:
    font_path=Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts'/font
    if font_path.is_file():
        assert QFontDatabase.addApplicationFont(str(font_path)) >= 0, font_path
app.setFont(QFont('Arial',10))
random.seed(42)
with tempfile.TemporaryDirectory() as root:
    base=Path(root); (base/'config').mkdir()
    config=json.loads((REPO/'HMI/config/config.json').read_text())
    config['simulation']=True
    (base/'config/config.json').write_text(json.dumps(config),encoding='utf-8')
    controller=Controller(base)
    controller.tunnel.start()
    for k in range(1200):
        controller.sample=controller.tunnel.read(.05)
        controller.sample.update(controller._flow_metrics(controller.sample['airspeed_ms'],time.monotonic()+k*.05))
    controller.sample['host_timestamp']=datetime.now(timezone.utc).isoformat()
    controller.sample['airspeed_kmh']=controller.sample['airspeed_ms']*3.6
    controller.last_valid_sample_monotonic=time.monotonic()

    async def analysis_preview():
        cfg=ExperimentConfig(name='SIM_Wing_AoA5',airfoil='Synthetic diamond section',chord_mm=100,
                             alpha_deg=5,source='simulation',averaging_s=.5,sample_rate_hz=20)
        detail=await controller.aerodynamics.create(cfg); identifier=detail['metadata']['id']
        await controller.aerodynamics.start(identifier)
        await controller.aerodynamics.tasks[identifier]
        await controller.aerodynamics.run_analysis(identifier,AnalysisSettings())
        await controller.aerodynamics.tasks[identifier]
        return controller.aerodynamics.detail(identifier)

    detail=asyncio.run(analysis_preview())
    assert detail['analysis'] is not None,detail['metadata']
    (PACKAGE/'assets/simulated_aerodynamics.json').write_text(json.dumps(detail,indent=2),encoding='utf-8')
    with patch.object(HMIWindow,'_get',lambda *a,**kw:None),patch.object(HMIWindow,'_post',lambda *a,**kw:None):
        w=HMIWindow(); w.resize(1600,900); w.show()
        for timer in w.findChildren(QTimer): timer.stop()
        w._on_config(config,None); w._on_status(controller.status(),None)
        now=time.monotonic()
        w.history=[dict(t=now-60+i*.1,speed=15+.035*math.sin(i*.12),mean=15) for i in range(601)]
        w.chart.set_points(w.history,40)
        app.processEvents()
        w.grab().save(str(PACKAGE/'assets/hmi_dashboard_full.png'))
        # Preserve actual widgets, crop only to keep slide evidence readable.
        rect=w.pages.geometry(); top_left=w.pages.mapTo(w,w.pages.rect().topLeft())
        w.grab(QRect(top_left.x(),top_left.y(),w.pages.width(),min(w.pages.height(),570))).save(str(PACKAGE/'assets/hmi_dashboard.png'))
        w.right.grab().save(str(PACKAGE/'assets/hmi_control.png'))
        w._show_page('cp'); panel=w.aerodynamic_panel
        panel.identifier=detail['metadata']['id']; panel.experiments=[detail['metadata']]
        panel.render(detail); panel.tabs.setCurrentIndex(1)
        app.processEvents()
        w.grab().save(str(PACKAGE/'assets/hmi_analytics_full.png'))
        panel.cp_plot.grab().save(str(PACKAGE/'assets/hmi_cp_plot.png'))
        w.close()

renderer=QSvgRenderer(str(PACKAGE/'assets/avans-wordmark.svg'))
image=QImage(480,184,QImage.Format.Format_ARGB32); image.fill(QColor(0,0,0,0))
painter=QPainter(image); renderer.render(painter); painter.end()
image.save(str(PACKAGE/'assets/avans-wordmark.png'))
print('Saved real native HMI dashboard, control and analytics captures (simulation only).')
