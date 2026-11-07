# ISA airspeed controller and selectable plants

AUTO has one generic `Regelaar` instance and one selected plant. MANUAL retains its independent quintic frequency trajectory. `ControlTask.c` coordinates state, safety, the shaped reference, one regulator step, the selected model, optional integral rejection and a recomputed motor request. The modules contain no allocation or blocking work in the control loop.

## Generic ISA/Tustin controller

`ESP32/src/Regelaar.c` implements the ideal form

\[
C(s)=K_p\left(1+\frac{1}{T_i s}+\frac{T_d s}{T_f s+1}\right).
\]

`RegelaarParameters` exposes `kp`, `ti_s`, `td_s`, `tf_s`, `sample_s`, and `mode`. Kp has plant-specific output/input units; all time constants are seconds. The runtime state contains only controller parameters and histories, never mechanical or hardware information. At Ts = 0.01 s:

\[
I_k=I_{k-1}+\frac{K_pT_s}{2T_i}(e_k+e_{k-1}),
\quad
D_k=\frac{(2T_f-T_s)D_{k-1}-2K_pT_d(y_k-y_{k-1})}{2T_f+T_s},
\quad u_k=K_pe_k+I_k+D_k.
\]

The first valid sample supplies P only and seeds history: no implicit first-sample integration or derivative kick. NONE disables all feedback; P disables I and D; PI disables D. Inactive equations are not evaluated. Ti must be positive for PI/PID; active derivative requires positive Tf. Negative/nonfinite parameters and nonfinite inputs/results are rejected. Failed steps do not advance history. Parameter changes reset history.

The legacy TCP command remains **`PID Kp Ki Kd`**, with the same torque units and meanings. At that boundary `Ti=Kp/Ki`, `Td=Kd/Kp`; the configured feedback mode still caps active terms. Zero integral gain selects P when appropriate. Independent nonzero Ki/Kd with Kp=0, and a PD-only legacy request without a finite integral time, are rejected because they have no supported finite ISA representation. No protocol field is silently reinterpreted. Frequency tuning is not uploaded through this legacy command.

Conventions and file organization were reviewed against [Delta Robot Regelaar.c](https://github.com/Raph-vk/EMPD2026-DeltaRobot/blob/main/src/Regelaar.c) at repository revision `6a0beb2f4182b92e40610a26d322cba82310706c`. Its ISA time-constant and Tustin conventions inform this implementation. Its motor constants, two-mass plant, error derivative and hardware assumptions are not wind-tunnel parameters.

## Torque plant and design

`TorqueModel.c` owns the one-mass mechanics, theoretical design, inverse slip model, acceleration/load feedforward and torque actuator decisions:

\[
J\dot\omega=T_m-T_L,\quad T_L=k_L\omega|\omega|,
\quad G_T(s)=\frac{K_v}{Js},\quad \hat\omega=\frac{v_{measured}}{K_v}.
\]

For RUCK EL 710 D4 02, article 146531, manufacturer-supplied inertias are Jm=0.0182, Jfan=0.3530, Jtotal=0.3712 kg m² (RUCK technical support, supplied 2026-11-01). `MOTOR_VARIANT_CONFIRMED=1` records those inertia data only. The 4 kW/1450 rpm nameplate assumption, Kv, slip slope, fan-load coefficient and quasi-static frequency-to-torque mapping still require installation validation. A 5.5 kW system rating is not a verified motor shaft-output rating.

The retained disturbance design uses wd=2πfd, m=disturbance/allowed_error, Kp=m J wd/Kv, wb=Kp Kv/J, Ti=10/wb, Td=3/wb, Tf=1/(10wb). It checks sensitivity at wd, continuous Routh-Hurwitz stability, crossover, minimum phase margin and sample-rate bandwidth. Default `CONTROLLER_SETTING=CONTROLLER_PI` executes PI; the retained theoretical design diagnostics describe the full continuous PID template, not a measured PI stability certificate.

The generic correction is in **Nm**. Without a calibrated characteristic:

\[
T_{acc}=J\dot v_{ref}/K_v,\quad
T_m=u_{PID}+T_{acc}+T_L,\quad
f_{requested}=\frac{p}{2\pi}\left(\hat\omega+\frac{T_m}{k_s}\right).
\]

Each physical contribution is included exactly once. `TorqueModel_Inverse` is the sole implementation of acceleration and load calculations. The old path calculated diagnostic feedforward separately and passed only PID to the inverse: the final command did not double count. The merged implementation removes those duplicate diagnostic calculations.

With valid fan calibration, the measured stationary map already contains stationary load/slip. The final command instead is

\[
f_{requested}=f_{cal}(v_{ref})+\frac{p}{2\pi k_s}(u_{PID}+T_{acc}).
\]

Do not add fan load again. Below the first calibration point, the established physical stationary fallback blends into the first measured endpoint; above the final point the lookup clamps. Diagnostics retain modeled acceleration/load/total torque and requested/limited frequency, including in calibrated operation. They describe the model, not measured motor torque. Frequency limits use the configured motor maximum, currently 50 Hz.

## Frequency plant and design

`FrequencyModel.c` models the independent **Hz → m/s** path:

\[
G_F(s)=\frac{K e^{-Ls}}{\tau s+1},\qquad
K_p=\frac{\tau}{K(\lambda+L)},\quad T_i=\min(\tau,4(\lambda+L)),\quad T_d=0.
\]

K is in (m/s)/Hz, tau/L/lambda in seconds, Kp in Hz/(m/s). Tf is zero and unused in PI mode. No identified installation model is supplied by this refactor: K/tau default to zero, `FREQUENCY_MODEL_VALIDATED=0`, `FREQUENCY_MODEL_COMMISSIONED=0`. Approved measured values must be reviewed and configured explicitly; mathematical estimates are not hardware commissioning.

The generic output is a **frequency correction in Hz**:

\[
f_{requested}=f_{cal}(v_{ref})+u_{PI}.
\]

Frequency AUTO requires an available valid calibration. Invalid/unavailable feedforward faults safely; it never substitutes a measured airspeed as Hz. Below the first measured velocity the existing lookup blends from zero; above the last it holds the final measured frequency. These extrapolation limits must be included in commissioning. Invalid/nonfinite characteristic output or feedforward above the configured limit is rejected. Final requested/limited frequencies remain separate.

## Saturation and ramp tracking

The plant model decides whether the latest integral increment would worsen upper/lower final frequency saturation or V20 tracking lag (>0.2 Hz in the corresponding direction). Tracking decisions use only status-valid, healthy, finite, in-range feedback newer than `MOTOR_STATUS_TIMEOUT_MS`. Stale feedback never drives tracking anti-windup; existing communication/sensor safety checks retain authority.

The regulator stores both integral states. A model request to reject integration restores the previous integral once, preserves measurement/error/derivative history and updates the generic output. The coordinator then reruns the **whole selected model and frequency limiting**. It does not subtract from an already clamped frequency. Negative increments that recover from upper saturation, and positive increments that recover from lower saturation, are allowed. No second PID step runs during recomputation. STOP/FAULT still disarm/reset independently.

## Model selection and unchanged infrastructure

`CONTROL_TORQUE_PI=0`, `CONTROL_FREQUENCY_PI=1`; torque remains the default `CONTROL_TYPE`. Only the selected model has state or runs in `ControlContext`; both use the same generic controller and filtered Pitot signal. Torque execution mode is capped by `CONTROLLER_SETTING`. Frequency is ISA PI with its own plant design and units. MANUAL never executes the generic regulator.

The alternating 5 ms notifications, 100 Hz ControlTask on Core 1, SensorTask ownership/Core 0, five-sample pressure average, 1 Hz Pitot pressure filter, pressure zero, quintic references, Modbus, state machine, buttons, safety I/O, enable chain, watchdogs, telemetry and calibration CRC/NVS semantics remain. Layout changes cover every application C/H file; private prototypes precede implementation and initialization precedes module functionality.

Build (no upload):

```powershell
cd ESP32
pio run -e waveshare_s3 -e waveshare_s3_frequency
./tests/run_host_tests.ps1
python -m unittest discover -s tests -p test_source_layout.py -v
```

## Identification and commissioning

HMI Settings → Calibration retains the MANUAL sweep and physical START/STOP workflow. Saving a run also generates `dynamics.json` and `dynamic_transitions.csv`. Offline fitting uses **measured actual V20 Hz trajectories**, estimates static K from adjacent measured steady states and grid-fits positive tau plus effective delay L. Fit output includes RMSE, R², timestamps/data completeness, residual bias/lag correlation, delay identifiability, warnings and response traces. The fitted output is already filtered Pitot airspeed: the effective model includes the existing measurement dynamics; simulation does not add another measurement filter.

`fit_acceptable` is a screening result only. `model_valid` and `hardware_commissioned` remain false until independently approved. Static-only fitting APIs return unvalidated models; their provisional time constant must not be used as measured dynamics. No actual installation time-series dataset was present for this verification; identified regression results are synthetic, not measured plant parameters.

The HMI shows actual Hz, measured/model airspeed, K/tau/L, quality, candidate **Kp/Ti**, configurable lambda and offline unit-step simulation. Simulation uses the effective model, Tustin PI, 0–50 Hz limiting and an assumed 2 Hz/s drive ramp with conditional integration. This assumed ramp is not a validated V20 ramp. Stored exports default to lambda=1 s; the dynamics endpoint recalculates advisory candidates without changing saved fits or hardware. Historical runs without dynamics remain readable and report dynamics unavailable.

Physical operation remains blocked by existing `SAFETY_IO_COMMISSIONED=0`, `V20_RUN_COMMISSIONED=0`, and `MOTOR_MODEL_TORQUE_TO_V20_ENABLED=0`. Frequency operation additionally requires reviewed identification, explicit frequency commissioning and a valid calibration. Validate nameplate/slip/load/Kv, drive ramps, filtered aerodynamic response, safety wiring, fresh feedback, watchdogs, timing and guarded closed-loop behavior before commissioning. No firmware upload or physical motor test is part of software verification.

See [refactor validation](REFACTOR-VALIDATION.md) for numerical regressions and executed checks.
