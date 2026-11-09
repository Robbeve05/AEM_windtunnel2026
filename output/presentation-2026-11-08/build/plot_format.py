from pathlib import Path
p=Path('output/presentation-2026-11-08/reproduction/make_plots.py')
s=p.read_text(encoding='utf-8').replace("'font.size':12", "'font.size':14").replace("'axes.labelsize':12,'legend.fontsize':11", "'axes.labelsize':16,'legend.fontsize':13")
s=s.replace("ylim=(-.5,33)", "ylim=(-.5,max(float(max(torque['measured'])),float(max(frequency['measured'])))+1)")
s=s.replace("figsize=(10.2,3.25)","figsize=(5.4,3.7)")
s=s.replace("ax.grid(alpha=.2); ax.legend(loc='lower right',frameon=False,ncol=2)","ax.grid(alpha=.2)")
s=s.replace("ax.grid(alpha=.2); ax.legend(loc='upper left',frameon=False,ncol=2)","ax.grid(alpha=.2)")
p.write_text(s,encoding='utf-8')
