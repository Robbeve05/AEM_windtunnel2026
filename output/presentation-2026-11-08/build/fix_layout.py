from pathlib import Path
p=Path('output/presentation-2026-11-08')
f=p/'wind_tunnel_progress.tex';s=f.read_text(encoding='utf-8').replace('aspectratio=169,11pt','aspectratio=169,10pt')
s=s.replace('Item & Current software design','Item & Configured')
s=s.replace(r'\resizebox{.94\textwidth}{!}{\input{diagrams/mechanical_concept.tex}}',r'\resizebox{!}{3.6cm}{\input{diagrams/mechanical_concept.tex}}')
s=s.replace(r'\resizebox{\linewidth}{!}{\input{diagrams/system_architecture.tex}}',r'\centering\resizebox{!}{4.8cm}{\input{diagrams/system_architecture.tex}}')
s=s.replace(r'\includegraphics[width=\linewidth]{assets/hmi_dashboard.png}',r'\includegraphics[width=\linewidth,height=5.25cm,keepaspectratio]{assets/hmi_dashboard.png}')
s=s.replace(r'\resizebox{.98\textwidth}{!}{\input{diagrams/control_methods.tex}}',r'\resizebox{!}{3.8cm}{\input{diagrams/control_methods.tex}}')
a=s.index(r'\begin{tabular}{@{}lrrrr@{}}');b=s.index(r'\bottomrule\end{tabular}',a)+len(r'\bottomrule\end{tabular}')
s=s[:a]+r'\input{plots/metrics_table.tex}'+s[b:]
f.write_text(s,encoding='utf-8')
f=p/'diagrams/state_machine.tex';s=f.read_text(encoding='utf-8').replace(r'{STOPPED\_PREPARE}',r'{STOPPED\\PREPARE}');f.write_text(s,encoding='utf-8')
f=p/'reproduction/make_plots.py';s=f.read_text(encoding='utf-8');needle="(ROOT/'plots/metrics_table.tex').write_text('\\n'.join(rows),encoding='utf-8')"
replacement="""table_header = r'''\\begin{tabular}{@{}lrrrr@{}}\\toprule
 & Rise [s] & Settle [s] & MAE$_{25-38\\,s}$ & MAE$_{99-114\\,s}$\\\\\\midrule
'''
(ROOT/'plots/metrics_table.tex').write_text(table_header+'\\n'.join(rows)+'\\n'+r'\\bottomrule\\end{tabular}'+'\\n',encoding='utf-8')"""
assert needle in s;s=s.replace(needle,replacement);f.write_text(s,encoding='utf-8')
