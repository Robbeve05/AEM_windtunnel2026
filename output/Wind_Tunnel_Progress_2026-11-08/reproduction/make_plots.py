"""Replot the supplied dual-controller calculation without changing its model."""
from pathlib import Path
from dataclasses import asdict
import importlib.util
import json
import sys
import csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('provided_simulation',Path(__file__).with_name('windtunnel_dual_controller_sim.py'))
sim=importlib.util.module_from_spec(spec); sys.modules[spec.name]=sim; spec.loader.exec_module(sim)
cfg=sim.Config()
torque,tp=sim.simulate(cfg,'torque'); frequency,fp=sim.simulate(cfg,'frequency')
for result in (torque,frequency):
    assert all(np.all(np.isfinite(v)) for v in result.values())
    assert np.all((result['frequency_request']>=0)&(result['frequency_request']<=50))
    assert np.max(abs(np.diff(result['frequency_actual'])))<=cfg.v20_ramp_hz_per_s*cfg.ts+1e-9
sim.plot_results(cfg,torque,frequency,ROOT/'plots/original')
source=dict(config=asdict(cfg),torque_ISA=asdict(tp),frequency_ISA=asdict(fp),
    derived=dict(J=cfg.j,Kv=cfg.kv,Ks=cfg.ks,Kload=cfg.kload),
    provenance='Supplied windtunnel_dual_controller_sim.py, unchanged defaults; model-only, not firmware commissioning')
(ROOT/'plots/parameters.json').write_text(json.dumps(source,indent=2),encoding='utf-8')

RED,BLUE,GREY='#C5221F','#233D60','#7B8794'
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':14,'axes.spines.top':False,
    'axes.spines.right':False,'axes.labelsize':16,'legend.fontsize':13,'pdf.fonttype':42})
t=torque['time']; ref=torque['reference']
def save(fig,name):
    fig.tight_layout(pad=1)
    fig.savefig(ROOT/'plots'/f'{name}.pdf',bbox_inches='tight')
    fig.savefig(ROOT/'plots'/f'{name}.png',dpi=190,bbox_inches='tight')
    plt.close(fig)

fig,ax=plt.subplots(figsize=(10.2,4.25))
ax.plot(t,ref,color='black',ls='--',lw=1.5,label='Shared quintic reference')
ax.plot(t,frequency['measured'],color=RED,lw=1.8,label='Frequency PI')
ax.plot(t,torque['measured'],color=BLUE,lw=1.8,label='Torque-model PI + FF')
ax.axvspan(70,115,color=GREY,alpha=.08)
ax.set(xlim=(0,115),ylim=(-.5,max(float(max(torque['measured'])),float(max(frequency['measured'])))+1),xlabel='Time [s]',ylabel='Filtered Pitot airspeed [m/s]')
ax.grid(alpha=.2); ax.legend(loc='lower right',frameon=False)
save(fig,'airspeed_response')

fig,ax=plt.subplots(figsize=(5.4,2.8))
for result,color,label in [(frequency,RED,'Frequency PI'),(torque,BLUE,'Torque-model PI + FF')]:
    ax.plot(t,result['frequency_request'],color=color,lw=1.7,label=label)
    ax.plot(t,result['frequency_actual'],color=color,ls=':',lw=1,alpha=.8)
ax.axvline(70,color=GREY,lw=1,ls='--')
ax.set(xlim=(0,115),ylim=(-1,51),xlabel='Time [s]',ylabel='V20 frequency [Hz]')
ax.grid(alpha=.2)
save(fig,'frequency_output')

fig,ax=plt.subplots(figsize=(5.4,2.8))
for result,color,label in [(frequency,RED,'Frequency PI'),(torque,BLUE,'Torque-model PI + FF')]:
    ax.plot(t,ref-result['measured'],color=color,lw=1.7,label=label)
ax.axhline(0,color='black',lw=.6); ax.axvspan(70,115,color=GREY,alpha=.08)
ax.set(xlim=(0,115),xlabel='Time [s]',ylabel='Tracking error [m/s]')
ax.grid(alpha=.2)
save(fig,'disturbance_error')

metrics={}
for result,key in [(frequency,'frequency'),(torque,'torque')]:
    first=(t>=5)&(t<39); y=result['measured'][first]; ts=t[first]
    def crossing(level):
        indices=np.flatnonzero(y>=level)
        return float(ts[indices[0]]) if len(indices) else None
    c10,c90=crossing(2),crossing(18)
    violations=np.flatnonzero(abs(y-20)>.4)
    settling=float(ts[violations[-1]+1]-5) if len(violations) and violations[-1]+1<len(ts) else None
    metrics[key]=dict(rise_10_90_s=c90-c10 if c90 is not None else None,
        settling_2percent_after_start_s=settling,
        overshoot_percent=max(0,float((max(y)-20)/20*100)),
        max_frequency_hz=float(max(result['frequency_request'])),
        IAE_shaped_all_m=float(np.trapezoid(abs(ref-result['measured']),t)),
        windows={f'{a}-{b}s':sim.response_metrics(result['measured'],target,(t>=a)&(t<b))
                 for a,b,target in [(25,38,20),(60,69,30),(99,114,15)]})
(ROOT/'plots/metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
rows=[]
for key,label in [('frequency','Frequency PI'),('torque','Torque model')]:
    m=metrics[key]; values=[m['rise_10_90_s'],m['settling_2percent_after_start_s'],
        m['windows']['25-38s']['mean_abs_error_ms'],m['windows']['99-114s']['mean_abs_error_ms']]
    rows.append(label+' & '+' & '.join('--' if v is None else f'{v:.2f}' for v in values)+r' \\')
table_header = r'''\begin{tabular}{@{}lrrrr@{}}\toprule
 & Rise [s] & Settle [s] & MAE$_{25-38\,s}$ & MAE$_{99-114\,s}$\\\midrule
'''
(ROOT/'plots/metrics_table.tex').write_text(table_header+'\n'.join(rows)+'\n'+r'\bottomrule\end{tabular}'+'\n',encoding='utf-8')
print(json.dumps(metrics,indent=2))
print('PASS finite traces, 0..50 Hz commands, shared 2 Hz/s actuator limit')
