# Aerodynamic analysis and Reynolds setpoints

This implementation extends the native PySide6 HMI and its existing single-worker, localhost FastAPI backend. It does not change ESP32 firmware, drive commands, commissioning flags, the Pitot filter, ISA/Tustin control, controller selection or the fifth-order reference curve. Acquisition buttons collect pressure observations only. Physical START, STOP, RESET and emergency-stop controls retain authority.

## Start and test on Windows

From the repository root, using an installed Python 3.11 or newer:

```powershell
python -m venv .venv-hmi
.\.venv-hmi\Scripts\python.exe -m pip install -r HMI\requirements-test.txt
cd HMI
..\.venv-hmi\Scripts\python.exe start_windows.py
```

The existing local environment is already installed on this workstation. The launcher checks the aerodynamic analysis route to reject an outdated backend. Close an old HMI/backend normally before restarting. Do not terminate an unrelated service on port 8000.

1. Open Overview and scroll to **Aerodynamic Experiment**. General measurement recording remains available separately.
2. Enter an experiment name (ASCII letters/numbers, spaces, dot, hyphen or underscore), airfoil identifier, chord in mm, incidence in degrees and averaging duration (default 5 s).
3. Explicitly select **SIMULATION — synthetic pressure data**. The general HMI configuration can stay in ESP32 mode with no ESP32 connected. No firmware command or fan start is needed.
4. Optional advanced settings expose sampling rate, synthetic airspeed, noise, seed and tap geometry JSON. Blank simulation taps use the documented synthetic diamond fixture. The chosen airfoil name is a label, not a claim of validated airfoil performance.
5. Select **Prepare Experiment**, **Start Measurement**, wait for `COMPLETED / NOT_ANALYZED`, then **Run Analysis** and **View Results**. A completed acquisition alone is not a validated analysis.
6. Prepare another experiment at a different angle. In Aerodynamic Analysis → Compare, filter and check completed experiments, then select **Compare selected**. Cl–alpha requires full Cl at two distinct angles of the same airfoil and source. Partial Cl remains available as a labelled result but is not plotted as a full polar.
7. Export CSV, JSON, PNG, SVG or PDF from analysis. Storage also exports completed folders to configured USB storage, retaining local copies.

```powershell
# Run from HMI. A writable cache avoids restricted Windows user-profile font caches.
$env:MPLCONFIGDIR = "$env:TEMP\windtunnel-matplotlib"
$env:QT_QPA_PLATFORM = 'offscreen'
..\.venv-hmi\Scripts\python.exe -m unittest discover -s tests -v
```

Unset `QT_QPA_PLATFORM` before starting the visible desktop application. Offscreen rendering is a test environment setting, not a production UI setting.

## Raspberry Pi

Use the existing supported 64-bit Raspberry Pi OS/Qt deployment. Transfer this uncommitted checkout to the device for testing; the normal Git updater cannot fetch unpublished local changes. Keep the existing `config/config.json` and `data/`.

```bash
cd ~/HMI
.venv/bin/python -m pip install -r requirements-test.txt
QT_QPA_PLATFORM=offscreen .venv/bin/python -m unittest discover -s tests -v
systemctl --user restart windtunnel-hmi.service
~/.local/bin/windtunnel-hmi
```

For a new installation in `~/HMI`, the existing `deploy/install-pi.sh` installs production dependencies and the user service/desktop launcher; `RUN_TESTS=1 bash deploy/install-pi.sh` also installs test dependencies and runs the suite. Analysis and export use pathlib, asyncio threads, Matplotlib Agg and ReportLab without Windows-only dependencies. Actual ARM/Pi execution still needs verification on the device; Windows tests do not establish device performance or hardware commissioning.

## Architecture and changed files

| Files | Responsibility |
|---|---|
| `desktop.py`, `native_aerodynamics.py` | Navigation, acquisition card, four native tabs, responsive Cp/Cl-alpha plots, hover inspection, visibility toggles, comparison filters, network downloads |
| `main.py` | Existing controller integration, mutual acquisition/calibration guards, Reynolds validation and aerodynamic API |
| `backend/aerodynamics/models.py` | Validated experiment, tap, analysis and comparison configuration |
| `acquisition.py`, `experiments.py` | Hardware-independent adapter contract, deterministic simulator, persistent asynchronous state machine |
| `pressure.py`, `integration.py`, `analysis.py`, `quality.py` | Shared pressure calculations, geometry-aware integration, instantaneous averaging and validity |
| `comparison.py`, `export.py` | Condition checks, Cl-alpha grouping, CSV/JSON/PNG/SVG/PDF exports |
| `backend/cp.py` | Legacy Cp now reuses the pressure primitives |
| `backend/measurements.py`, `backend/storage.py` | Existing recordings/history, Reynolds fields, recursive USB export including analysis revisions |
| `backend/reynolds.py`, `native_reynolds.py`, `backend/models.py` | Shared Sutherland/Re calculations, two-input native setpoint page, compatible optional request metadata |
| `native_diagnostics.py`, `desktop.py` | Grouped diagnostics, compact health display, preserved scroll position and resizable sidebars |
| `requirements*.txt`, `start_windows.py`, `deploy/install-pi.sh` | Export/test dependencies and matching backend startup |
| `tests/test_aerodynamics.py`, `test_reynolds.py`, `test_native_hmi.py`, `test_windows_launcher.py` | Numerical, workflow, API, export, UI and startup coverage |

The Qt event loop performs asynchronous HTTP requests. Acquisition, CSV I/O, analysis and rendering run in backend worker threads/tasks. The desktop polls an active experiment without rebuilding the page on every telemetry frame. Tables and graphs update only when state, progress or analysis revision changes. Each analysis tab has a single outer scroll area for smaller displays; tables retain their usual scrollbars. Sidebar widths and diagnostic height remain adjustable.

## API

All routes are local backend routes under `/api/aerodynamics`:

| Method and path | Behavior |
|---|---|
| `GET /experiments` | List aerodynamic experiments; optional `airfoil`, `name`, `date` (ISO date prefix), `alpha_min`, `alpha_max` filters |
| `POST /experiments` | Prepare a validated experiment; explicit `source` required |
| `GET /experiments/{id}` | Configuration, workflow/analysis status, result and available files |
| `PUT /experiments/{id}` | Update a preparation before raw data exists |
| `POST /experiments/{id}/start` | Start selected adapter acquisition; never sends a motor command |
| `POST /experiments/{id}/stop` | Cancel pressure acquisition; retains partial raw samples |
| `GET /status` | Active task identifiers and physical adapter availability |
| `POST /experiments/{id}/analyze` | Start background analysis; accepts analysis settings (`{}` uses defaults) |
| `GET /experiments/{id}/cp` | Cp points and analysis status |
| `GET /experiments/{id}/coefficients` | Coefficients with per-value availability, coverage status and reasons |
| `POST /compare` | `{ "identifiers": ["id1", "id2"] }`; 1–12 completed analyses |
| `GET /experiments/{id}/files/{filename}` | Whitelisted result download; blocks acquisition/processing |

Existing `/api/measurement/*`, `/api/measurements`, `/api/storage/*`, calibration and setpoint routes remain compatible. Historical recordings without aerodynamic metadata continue to open. Aerodynamic folders appear in history and Storage; their full analysis is under Aerodynamic Analysis.

Example preparation:

```json
{"name":"SIM_Wing_5","airfoil":"Synthetic fixture","chord_mm":150,
 "alpha_deg":5,"averaging_s":5,"source":"simulation",
 "simulation_airspeed_ms":15,"simulation_noise_pa":0.1,"simulation_seed":42}
```

## Acquisition and persistence

States: `PREPARED → ACQUIRING → COMPLETED → PROCESSING → COMPLETED`; cancellation and failure are explicit. Analysis status is independently `NOT_ANALYZED`, `PROCESSING`, `FULL`, `PARTIAL`, `INVALID` or `FAILED`. Restarted interrupted work becomes `FAILED / INTERRUPTED`, retaining files. Cancelled acquisitions cannot be passed off as completed measurements. Prepared configuration is immutable after acquisition.

Each folder is under the existing configured measurement root. `raw_data.csv` is created exclusively and appended during acquisition, then never rewritten by analysis. It includes per-channel sampling time, paired reference time, host UTC arrival time, pressure/reference pressure, reference velocity, density, temperature, channel, validity and source. No assumption of simultaneous multiplexed channels is made. The simulator emits separate channel times and explicitly paired references.

Derived files are `processed_data.csv`, `cp_curve.csv`, `coefficients.csv`, `analysis.json`, `cp_distribution.png`, `cp_distribution.svg`, and `analysis_report.pdf`. Cp graphs/CSV require actual valid Cp; coefficient CSV requires at least one available coefficient. Invalid analyses still produce an honest JSON/PDF quality report rather than empty graphs. Analysis saves settings, software version, revision and original raw SHA-256. Every reprocessing revision persists in `analysis_history/0001/`, `0002/`, etc.; top-level derived files point to the latest completed result. Analysis and USB publishing share a lock. USB exports copy complete folders and revision history, fsync files and retain local data. Unfinished experiments are rejected.

## Pressure and coefficient calculations

All internal quantities use SI units. Tap coordinates are normalized by chord; x/c runs from leading to trailing edge and z/c is upward. Upper/lower surfaces are explicit, not inferred from z sign (cambered surfaces may cross the chord).

For each valid pressure/reference pair:

```text
q = 0.5 rho V²
Cp = (p - p_inf) / q
```

Require finite pressures, positive density, nonnegative velocity, `q >= 1 Pa` (configurable upward), valid channel/source/status, and reference timing within 0.1 s (configurable). Duplicate tap/sample observations are rejected. Each enabled tap requires five valid observations by default. Cp is the arithmetic mean of **instantaneous, time-aligned Cp**, not a ratio formed from averaged pressures. Pressure and Cp standard deviations are population standard deviations of those observations; these quantify scatter, not calibrated measurement uncertainty. Reference means are means across accepted paired observations. Invalid or missing data remain unavailable; no zero substitution occurs.

For Cn, merge the nonuniform upper/lower x grids over their common measured interval, use explicit piecewise-linear interpolation between existing taps, then trapezoidal integration:

```text
Cn = integral (Cp_lower - Cp_upper) d(x/c)
```

No leading/trailing-edge extrapolation is allowed. Missing taps may leave a piecewise-linear segment between remaining valid taps, visibly marked partial when configured observations are missing. Coverage `[start, end]` and `full_chord` are stored. Ca/Cm additionally require supplied surface z/c; no flat plate or inferred airfoil coordinates are inserted. For both surfaces parameterized LE→TE, the dimensionless pressure-force elements are:

```text
upper: dCa = +Cp dz/c, dCn = -Cp dx/c
lower: dCa = -Cp dz/c, dCn = +Cp dx/c
dCm = (z/c) dCa - (x/c - x_ref/c) dCn
Cl = Cn cos(alpha) - Ca sin(alpha)
```

Moment is positive nose-up; the default reference is quarter-chord and is configurable. Pressure and supplied surface coordinates are linear within each panel; force integration is trapezoidal and pressure-times-lever-arm moments are integrated exactly for those linear functions. Open end-face contributions are not invented: Ca/Cm are labelled partial when the measured boundaries are not closed. Without z/c, Cn is supported; Cl equals Cn exactly at zero incidence, but nonzero-incidence Cl and Ca/Cm are unavailable. Total Cd is always unavailable because pressure taps alone do not establish total drag, including viscous effects.

The pressure-normal surface integration follows the framework described by [NASA Glenn](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/aerodynamic-force/). The implemented panel sign and moment conventions are explicitly stated above and verified analytically, including Cn=2, Ca=0, Cl=2, Cm=-0.5 for constant upper/lower Cp=-1/+1 at zero incidence on closed symmetric geometry. A sloped nonconstant case verifies Cn=0.5, Ca=0.05, Cm=-31/150. The selected interpolation is an approximation between measured taps, not a reconstruction of missing aerodynamic features.

`FULL` describes the configured measured chord and accepted sample coverage, not validation of the pressure system, airfoil geometry accuracy or tunnel corrections. Geometry discretization, finite tap spacing, 3D/end effects, blockage, wall corrections and calibration uncertainty remain experimental limitations. Different airfoils, sources, chords, speeds (>10%) or densities (>5%) generate comparison warnings. No Cl-alpha curve is fabricated from one measurement or mixed simulation/physical sources.

## Real pressure hardware

Physical acquisition is disabled by default with `UnconfiguredPhysicalAdapter`; there is no fallback to simulation. A real implementation must supply `available=False` until commissioned, `simulated=False`, `validate(config, density)` and `sample(config, sample_index, time_s, density)` according to `PressureAcquisitionAdapter`. Validation must verify enabled channel mappings, pressure/reference sensors, calibration validity, measurement range, current reference V/q and synchronization. `sample` must have bounded hardware timeouts, report actual per-channel/reference times on the same clock, fresh calibrated SI values and explicit validity (`OK`/`VALID`). Loss of reference validity or communication must raise/mark invalid; never synthesize a replacement reading.

Tap configuration includes ID, surface, x/c, optional actual z/c, physical location, channel, sensor configuration, calibration and enable status. Hardware architecture (dedicated sensors, scanner or multiplexer) is adapter-specific. Upper/lower coordinates and the model's measured incidence must be configured from the actual installed model. At present the ESP32 Pitot connection alone does not provide multipoint pressures; no firmware command or unimplemented sensor has been invented.

The controller also requires fresh valid ESP32/Pitot data, fault-free `RUNNING` state, no active calibration and no general recording. The adapter validates pressure/reference readiness before acquisition and must maintain it throughout sampling. Simulation bypasses physical sensor availability explicitly, while preserving acquisition/calibration exclusivity and never changing machine state.

## Reynolds setpoint

The third selector is **AUTO — Reynolds number**, mapped to firmware AUTO. It adds exactly two editable values: desired dimensionless Re and chord in mm. Measured temperature and existing Pitot density are read-only. Missing, invalid or stale measured temperature inhibits Apply with a reason; neither zero nor a default temperature is substituted. Temperature uses the existing unique Pitot sample path and status timeout, not merely a fresh TCP heartbeat. Existing ESP32 density is used when reported, otherwise the existing HMI Pitot configuration.

```text
T_K = measured_temperature_C + 273.15
mu = 1.458e-6 T_K^1.5 / (T_K + 110.4)  [Pa s]
V_requested = Re_requested mu / (rho chord_m)
Re_actual = rho V_measured chord_m / mu
```

Sutherland constants follow the US Standard Atmosphere formulation. The accepted temperature range is -25…105 °C, consistent with the existing MS4525DO operating range. Only a finite positive Re/chord and a calculated speed within the existing configured AUTO maximum, never above 40 m/s, may be applied. Over-limit previews show the computed value and reject application; no clamping. Existing AUTO/READY/freshness/calibration conditions remain authoritative. The server recalculates from its own current telemetry and rejects mismatched client previews, then sends the existing `SETPOINT <m/s>` command only. The explicit button applies once; changing temperature does not automatically resend setpoints.

The existing request `{ "airspeed_ms": 12 }` remains valid. Reynolds adds paired optional `requested_reynolds` and `chord_mm` to that same `POST /api/tunnel/setpoint`. No new ESP32 mode or command is introduced. Applied requested Re, applied speed, chord, temperature, density, viscosity, current measured speed and achieved Re persist in recording rows/history/exports. Aerodynamic acquisitions also record the applied context and calculate achieved Re from their own measured reference speed, measured temperature and experiment chord. The applied context separately retains its original chord if it differs from the experiment's chord. Missing valid temperature leaves achieved Re unavailable while pressure-only analysis can still proceed.

## Validation record (2026-11-07)

The Windows suite passed **101 tests** (0 failed, 0 skipped; Python 3.12/PySide6). It covers pressure/q/Cp, low-q/nonfinite rejection, exact nonuniform integration and signs, missing geometry/taps/cycles, time alignment, duplicate observations, averaging and scatter, state transitions/cancellation/failure/recovery, immutable raw data/revisions, CSV/JSON/PNG/SVG/PDF, restart/USB persistence, comparison conditions, legacy API/history, Reynolds/environment freshness, limits, native tabs/controls/plot direction, successful native binary downloads and launcher compatibility.

The visible Windows application launched with its matching backend. A separate Qt client exercised the real local HTTP path through Prepare → Acquire → Analyze → View and compared two explicit simulated incidences (0° and 5°); native PDF/CSV/JSON/PNG/SVG save actions succeeded. Screens were inspected at 1800×1100 and 1280×800, and every page of the two-page sample PDF was rendered and visually checked. `pip check`, Python compilation and `git diff --check` passed. A restricted Windows user-profile Matplotlib font cache delayed the first test run; setting `MPLCONFIGDIR` to a writable test cache resolved that environmental issue without changing calculation logic.

Both `waveshare_s3` and `waveshare_s3_frequency` firmware builds passed on this workstation. No upload or physical operation was performed. Raspberry Pi runtime, ARM dependency installation and real pressure acquisition cannot be verified without the corresponding device/hardware. Cross-platform implementation and deployment paths are retained; they do not substitute for those checks.
