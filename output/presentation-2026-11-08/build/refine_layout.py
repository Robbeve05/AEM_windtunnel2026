from pathlib import Path
p=Path('output/presentation-2026-11-08'); f=p/'wind_tunnel_progress.tex';s=f.read_text(encoding='utf-8')
s=s.replace('height=5.25cm','height=5.12cm')
a=s.index('\\begin{frame}\n');b=s.index('\\end{frame}',a)+len('\\end{frame}')
title=r'''\begin{frame}
\vspace{.3cm}
\begin{beamercolorbox}[wd=\linewidth,sep=10pt,center,rounded=true]{title}
{\LARGE Wind Tunnel Project}\par\vspace{.2cm}
{\large Architecture, HMI and control comparison}
\end{beamercolorbox}
\vspace{.45cm}
\begin{center}
Konstantin Nikitin \quad Bas Keultjes\\[.15cm]
Dante Bloemendaal \quad Robbe van Eekelen\\[.4cm]
Aerospace Engineering \& Maintenance\\
Avans University of Applied Sciences\\[.2cm]
8 November 2026\\[.45cm]
{\color{AvansBlue}\bfseries Software demonstrated; physical commissioning pending}
\end{center}
\end{frame}'''
s=s[:a]+title+s[b:];f.write_text(s,encoding='utf-8')
f=p/'reproduction/make_plots.py';s=f.read_text(encoding='utf-8').replace('figsize=(5.4,3.7)','figsize=(5.4,2.8)');f.write_text(s,encoding='utf-8')
