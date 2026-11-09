"""Wind tunnel: compare torque-model vs frequency-model ISA PI/PID at 100 Hz.

Educational/offline simulation only. No hardware communication or commissioning.
Uses Python Control Systems Library for linear plant diagrams when installed,
and direct discrete-time integration for the real-time nonlinear ISA controller,
frequency drive, fan load, saturation and Pitot filter.

Run: pip install numpy scipy matplotlib control
     python windtunnel_dual_controller_sim.py
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse
import csv
import math

import numpy as np
import matplotlib.pyplot as plt

try:
    import control as ct
except ImportError:
    ct = None


@dataclass
class Config:
    ts: float = 0.01
    duration_s: float = 115.0
    inertia_motor: float = 0.0182
    inertia_fan: float = 0.3530
    nominal_rpm: float = 1450.0  # ASSUMPTION, not validated V20/motor condition
    nominal_power_w: float = 4000.0  # ASSUMPTION
    nominal_airspeed_ms: float = 40.0  # measured near 50 Hz in earlier setup
    pole_pairs: float = 2.0  # ASSUMPTION
    max_frequency_hz: float = 50.0
    max_airspeed_ms: float = 40.0
    v20_ramp_hz_per_s: float = 2.0  # SIMULATION assumption, not confirmed actual V20 setting
    pitot_cutoff_hz: float = 1.0
    frequency_kp: float = 0.75  # legacy provisional parameter, Hz/(m/s)
    frequency_ti: float = 0.75 / 0.35  # equivalent ISA Ti (s)
    torque_pid_mode: str = "PI"  # NONE/P/PI/PID
    frequency_pid_mode: str = "PI"
    # Optional comparison case: sine disturbance on measured airspeed
    disturbance_amplitude_ms: float = 1.0
    disturbance_start_s: float = 70.0
    disturbance_frequency_hz: float = 0.02
    reference_start_s: float = 5.0
    reference_duration_s: float = 15.0
    # additive disturbance uses small offset, deliberately not modelled in plant

    @property
    def j(self):
        return self.inertia_motor + self.inertia_fan

    @property
    def omega_nominal(self):
        return 2 * math.pi * self.nominal_rpm / 60

    @property
    def kv(self):
        return self.nominal_airspeed_ms / self.omega_nominal

    @property
    def ks(self):
        torque = self.nominal_power_w / self.omega_nominal
        omega_s = (2 * math.pi * 50 / self.pole_pairs)
        return torque / (omega_s - self.omega_nominal)

    @property
    def kload(self):
        return (self.nominal_power_w / self.omega_nominal) / self.omega_nominal ** 2


@dataclass
class ISAParameters:
    kp: float
    ti_s: float
    td_s: float = 0.0
    tf_s: float = 0.0
    mode: str = "PI"


class ISAController:
    """Single shared Tustin implementation, plant-independent."""
    def __init__(self, p: ISAParameters, ts: float):
        assert ts > 0 and p.mode in {"NONE", "P", "PI", "PID"}
        self.p, self.ts = p, ts
        self.integral = 0.0
        self.previous_error = 0.0
        self.previous_measurement = 0.0
        self.derivative = 0.0
        self.valid = False

    def tentative(self, ref: float, measured: float):
        error = ref - measured
        p = self.p.kp * error if self.p.mode != "NONE" else 0.0
        delta_i = 0.0
        if self.p.mode in ("PI", "PID") and self.valid:
            delta_i = self.p.kp * self.ts / (2 * self.p.ti_s) * (error + self.previous_error)
        d = 0.0
        if self.p.mode == "PID" and self.valid:
            d = ((2 * self.p.tf_s - self.ts) * self.derivative -
                 2 * self.p.kp * self.p.td_s * (measured - self.previous_measurement)) / (2 * self.p.tf_s + self.ts)
        return p, self.integral + delta_i, d, delta_i, error

    def commit(self, terms, measured: float, reject_increment: bool):
        p, tentative_i, d, delta_i, error = terms
        self.integral = tentative_i - delta_i if reject_increment else tentative_i
        self.derivative = d
        self.previous_error = error
        self.previous_measurement = measured
        self.valid = True
        return p + self.integral + d


def torque_design(cfg: Config) -> ISAParameters:
    """Reproduces existing RegelaarDesign.c design equations, not validated gains."""
    wd = 2 * math.pi * 0.02
    m = 5.0 / 0.5
    kp = m * cfg.j * wd / cfg.kv
    wb = kp * cfg.kv / cfg.j
    return ISAParameters(kp, ti_s=10 / wb, td_s=3 / wb, tf_s=1 / (10 * wb), mode=cfg.torque_pid_mode)


def quintic(t, begin, duration, target):
    q = float(np.clip((t - begin) / duration, 0.0, 1.0))
    value = target * (10 * q**3 - 15 * q**4 + 6 * q**5)
    derivative = 0.0 if not 0 < q < 1 else target * (30 * q**2 - 60 * q**3 + 30 * q**4) / duration
    return value, derivative


def setpoint(t, cfg):
    # Example scenario 0->20->30->15 m/s, all with fifth-order profiles.
    if t < 39:
        return quintic(t, cfg.reference_start_s, cfg.reference_duration_s, 20.0)
    if t < 76:
        r, dr = quintic(t, 39.0, 15.0, 10.0)
        return 20.0+r, dr
    r, dr = quintic(t, 76.0, 15.0, -15.0)
    return 30.0+r, dr


def simulate(cfg: Config, variant: str):
    """Both use SAME physical fan/motor plant for a fair comparison."""
    assert variant in ("torque", "frequency")
    params = torque_design(cfg) if variant == "torque" else ISAParameters(cfg.frequency_kp, cfg.frequency_ti, mode=cfg.frequency_pid_mode)
    reg = ISAController(params, cfg.ts)
    t = np.arange(0, cfg.duration_s + cfg.ts / 2, cfg.ts)
    names = ("reference", "airspeed", "measured", "frequency_request", "frequency_actual", "pid", "ff", "omega", "saturation")
    data = {name: np.zeros(len(t)) for name in names}
    omega = 0.0
    actual_hz = 0.0
    filtered_pressure = 0.0
    pitot_alpha = 1.0 - math.exp(-2 * math.pi * cfg.pitot_cutoff_hz * cfg.ts)

    for i, time in enumerate(t):
        ref, reference_accel = setpoint(time, cfg)
        actual_airspeed = cfg.kv * omega
        disturbance = (cfg.disturbance_amplitude_ms * math.sin(2 * math.pi * cfg.disturbance_frequency_hz * (time - cfg.disturbance_start_s))
                       if time >= cfg.disturbance_start_s else 0.0)
        # Pitot pressure is filtered, THEN converted to speed (as in firmware).
        raw_pressure = 0.5 * 1.2 * max(0.0, actual_airspeed + disturbance) ** 2
        filtered_pressure += pitot_alpha * (raw_pressure - filtered_pressure)
        measured = math.sqrt(2 * filtered_pressure / 1.2)

        terms = reg.tentative(ref, measured)
        preliminary_pid = terms[0] + terms[1] + terms[2]
        if variant == "torque":
            omega_est = measured / cfg.kv
            torque_ff = cfg.j * reference_accel / cfg.kv + cfg.kload * omega_est * abs(omega_est)
            # Once-only torque load + acceleration ff; rotor-speed estimate follows Pitot.
            def plant_to_hz(pid):
                return (omega_est + (torque_ff + pid) / cfg.ks) * cfg.pole_pairs / (2 * math.pi)
            ff_label = torque_ff
        else:
            # Provisional 0..50 Hz -> 0..40 m/s linear feedforward; replace with CSV-calibration.
            frequency_ff = ref * cfg.max_frequency_hz / cfg.nominal_airspeed_ms
            def plant_to_hz(pid):
                return frequency_ff + pid
            ff_label = frequency_ff

        trial = plant_to_hz(preliminary_pid)
        bounded = float(np.clip(trial, 0, cfg.max_frequency_hz))
        # Conditional anti-windup if the integrator step makes saturation worse.
        sat_high = trial > cfg.max_frequency_hz and terms[3] > 0
        sat_low = trial < 0.0 and terms[3] < 0
        # Model the slow drive ramp: reject integration that pushes demand farther
        # from actual drive frequency, even when not at hard saturation.
        ramp_lag_high = bounded > actual_hz + 0.20 and terms[3] > 0
        ramp_lag_low = bounded < actual_hz - 0.20 and terms[3] < 0
        reject = sat_high or sat_low or ramp_lag_high or ramp_lag_low
        pid = reg.commit(terms, measured, reject_increment=reject)
        command_hz = float(np.clip(plant_to_hz(pid), 0, cfg.max_frequency_hz))
        dfmax = cfg.v20_ramp_hz_per_s * cfg.ts
        actual_hz += float(np.clip(command_hz - actual_hz, -dfmax, dfmax))
        omega_sync = actual_hz * 2 * math.pi / cfg.pole_pairs
        motor_torque = cfg.ks * (omega_sync - omega)
        fan_load = cfg.kload * omega * abs(omega)
        omega = max(0, omega + cfg.ts * (motor_torque - fan_load) / cfg.j)
        for name, value in {"reference":ref, "airspeed":actual_airspeed, "measured":measured,
                            "frequency_request":command_hz,"frequency_actual":actual_hz,
                            "pid":pid,"ff":ff_label,"omega":omega,
                            "saturation":float(reject)}.items():
            data[name][i] = value
    data["time"] = t
    return data, params


def response_metrics(x, target, window):
    z = x[window]
    return {"max_error_ms":float(np.max(np.abs(z - target))),
            "mean_abs_error_ms":float(np.mean(np.abs(z - target)))}


def plot_results(cfg, torque, frequency, outdir):
    outdir.mkdir(parents=True, exist_ok=True)
    t = torque["time"]
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, torque["reference"], "k--", linewidth=1.7, label="Setpoint (quintic)")
    ax.plot(t, torque["measured"], label="Torque model / ISA")
    ax.plot(t, frequency["measured"], label="Frequency model / ISA")
    ax.set(xlabel="Time [s]", ylabel="Filtered Pitot airspeed [m/s]", title="Airspeed tracking: shared physical fan model")
    ax.grid(True, alpha=0.3);ax.legend();fig.tight_layout()
    fig.savefig(outdir / "airspeed_response.png", dpi=160)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, torque["frequency_request"], label="Torque: V20 requested")
    ax.plot(t, torque["frequency_actual"], label="Torque: V20 actual", ls="--")
    ax.plot(t, frequency["frequency_request"], label="Frequency: V20 requested")
    ax.plot(t, frequency["frequency_actual"], label="Frequency: V20 actual", ls="--")
    ax.set(xlabel="Time [s]", ylabel="Frequency [Hz]", title="V20 request and ramp-limited output")
    ax.grid(True, alpha=0.3);ax.legend();fig.tight_layout()
    fig.savefig(outdir / "v20_frequency.png", dpi=160)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, torque["reference"]-torque["measured"], label="Torque model error")
    ax.plot(t, frequency["reference"]-frequency["measured"], label="Frequency model error")
    ax.axhline(0, color="k", linewidth=0.8)
    ax.set(xlabel="Time [s]", ylabel="Airspeed error [m/s]", title="Tracking error")
    ax.grid(True, alpha=0.3);ax.legend();fig.tight_layout()
    fig.savefig(outdir / "tracking_error.png", dpi=160)
    plt.close(fig)

    with (outdir / "simulation.csv").open("w", newline="", encoding="utf-8") as f:
        cols=["time_s","reference_ms","torque_airspeed_ms","frequency_airspeed_ms", "torque_requested_hz", "torque_actual_hz", "frequency_requested_hz","frequency_actual_hz"]
        wr=csv.writer(f);wr.writerow(cols)
        for i in range(len(t)):
            wr.writerow([t[i],torque["reference"][i],torque["measured"][i],frequency["measured"][i],
                         torque["frequency_request"][i],torque["frequency_actual"][i],frequency["frequency_request"][i],frequency["frequency_actual"][i]])


def optional_linear_plant_plots(cfg, outdir):
    """Use python-control to inspect open-loop nominal linear plant assumptions."""
    if ct is None:
        print("Note: install `control` to generate linear plant step-response and Bode plots.")
        return
    s = ct.TransferFunction.s
    # Torque to airspeed, linearized about nominal rotor speed: J*s + dTload/domega.
    torque_plant = cfg.kv / (cfg.j * s + 2 * cfg.kload * cfg.omega_nominal)
    # Frequency to airspeed, first-order proxy (placeholder; replace with ID).
    frequency_plant = 0.8 / (2.0*s+1.0)
    fig, ax=plt.subplots(figsize=(10,4))
    for label, plant in [("Torque plant [m/s per Nm]", torque_plant), ("Frequency plant [m/s per Hz]", frequency_plant)]:
        r=ct.step_response(plant, T=np.linspace(0, 15, 400))
        ax.plot(r.time, r.outputs, label=label)
    ax.set(xlabel="Time [s]",ylabel="Response for unit input",title="Illustrative linearized plant responses (different units)")
    ax.grid(True,alpha=0.3);ax.legend();fig.tight_layout();fig.savefig(outdir/"linear_plant_steps.png",dpi=160);plt.close(fig)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,default=Path(__file__).resolve().parent / "results")
    parser.add_argument("--v20-ramp",type=float,default=2.0,help="Assumed V20 ramp in Hz/s")
    parser.add_argument("--frequency-kp",type=float,default=0.75)
    parser.add_argument("--frequency-ti",type=float,default=0.75/0.35)
    args=parser.parse_args()
    cfg=Config(v20_ramp_hz_per_s=args.v20_ramp,frequency_kp=args.frequency_kp,frequency_ti=args.frequency_ti)
    if min(cfg.v20_ramp_hz_per_s, cfg.frequency_ti, cfg.ts) <= 0:
        parser.error("ramp, frequency-ti and sample time must be positive")
    torque,p_t=simulate(cfg,"torque")
    freq,p_f=simulate(cfg,"frequency")
    plot_results(cfg,torque,freq,args.out)
    optional_linear_plant_plots(cfg,args.out)
    print("SIMULATION ONLY — unvalidated motor/slip, V20 ramp and frequency model.")
    print("Torque ISA params:",p_t)
    print("Frequency ISA params:",p_f)
    for label,result in [("Torque",torque),("Frequency",freq)]:
        for a,b,ref in [(25,38,20),(60,69,30),(99,114,15)]:
            window=(result["time"]>=a)&(result["time"]<b)
            print(label, f"{a:.0f}-{b:.0f}s",response_metrics(result["measured"],ref,window))
    print("Plots and CSV:",args.out.resolve())

if __name__=="__main__":
    main()
