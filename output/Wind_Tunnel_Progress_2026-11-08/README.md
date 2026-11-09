# Wind Tunnel Project - progress presentation

English, 12-slide LaTeX Beamer presentation. Speaker plan: 575 s (9 min 35 s), with 25 s transition margin. Academic Madrid layout follows the earlier server presentation, using Avans red and the existing project attribution. The new cabinet photo was supplied by the owner.

## Present

Open `wind_tunnel_progress.pdf`. Read `speaker_notes.md` for timing, technical qualifications and slide-specific sources. All HMI/analytics data and controller plots are explicitly simulated. No physical commissioning is claimed.

## Contents

- `wind_tunnel_progress.tex`: editable Beamer source.
- `diagrams/*.tex`: editable TikZ system, mechanical, state-machine and control diagrams.
- `assets/`: supplied cabinet photo, actual current native-HMI captures and simulation analysis data.
- `plots/`: vector PDF/PNG plots, exact source output CSV, parameters and computed metrics.
- `reproduction/windtunnel_dual_controller_sim.py`: owner's supplied script, unchanged.
- `reproduction/make_plots.py`: reruns that script's model/defaults, computes metrics and applies the deck plot style.
- `reproduction/capture_hmi.py`: renders actual native widgets with a local simulated backend; it does not connect to hardware.
- `provenance.json`: input hashes and software snapshot identifier.

## Reproduce the calculation

Use Python 3.11+ with `numpy` and `matplotlib` installed:

```powershell
$env:MPLBACKEND='Agg'
$env:MPLCONFIGDIR="$PWD/build/mplconfig"
python reproduction/make_plots.py
```

The optional `control` package is not needed for the three required comparison plots. The supplied script's optional linear unit-input plant plot is not used, because its two input units differ.

The calculation uses unchanged source defaults: sample time 0.01 s; duration 115 s; 0-50 Hz; assumed 2 Hz/s actuator; 1 Hz pressure filter; common 0/20/30/15 m/s quintic reference; common 1 m/s, 0.02 Hz measured-output disturbance from 70 s. Motor/slip/load parameters and the V20 ramp are assumptions, not validated measurements. Both controllers are PI. Frequency gains are legacy provisional values, not identified active firmware gains. Current firmware requires missing positive identification coefficients and fan calibration.

The comparison reuses identical plant, timing, initial conditions, limits, filter and disturbance. Tuning and feedforward differ as in the supplied script, so it cannot isolate controller structure or prove general superiority. Metrics come from the generated traces. Rise/settling use the first transition; MAE windows are explicitly labeled. The final window includes a down-transition plus continuing disturbance.

## Reproduce HMI captures

Install the current repository's `HMI/requirements-desktop.txt`, then:

```powershell
python reproduction/capture_hmi.py --repo C:/Software/AEM_windtunnel2026
```

The script imports the real HMI/backend modules, creates only a temporary simulated configuration, patches network requests out, generates actual synthetic pressure analysis and captures Qt widgets offscreen. On Windows it explicitly loads Arial system fonts. Dashboard chart history is a deterministic preview, not a measurement log. No production configuration or permission flag is changed.

## Compile PDF

With Tectonic available:

```powershell
tectonic --keep-logs --outdir . wind_tunnel_progress.tex
```

Or with TeX Live/MiKTeX:

```powershell
pdflatex -interaction=nonstopmode -halt-on-error wind_tunnel_progress.tex
pdflatex -interaction=nonstopmode -halt-on-error wind_tunnel_progress.tex
```

Packages: Beamer/Madrid, Latin Modern, fontenc, graphicx, booktabs, amsmath, TikZ with arrows.meta/positioning/calc. Run in the presentation folder so relative asset paths resolve.

## Evidence and limits

Software status is based on commit `b130348` and its recorded regression logs: 23 host executables, three builds, 104 HMI tests, seven I/O checks, one layout test and seven migration checks. The original Beamer source/PDF and project workplan were inspected. Mechanical diagrams are functional concepts, not CAD/as-built drawings. No confirmed dimensions were supplied.

Outstanding physical work: independent safety and DO1/In1/OFF2/BICO checks; V20 watchdog/restart/acknowledgment/latency behavior; mechanical/airflow survey; sensor tubing/channel calibration; measured fan/dynamic identification; repeatable Cp/XFOIL comparisons. The project remains uncommissioned physically.
