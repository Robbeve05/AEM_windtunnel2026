///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * TorqueModel.c
 *
 * Torque plant design, inertia/load feedforward and inverse motor model.
 * Model: incremental shaft torque [Nm] -> airspeed [m/s].
 * Never apply gains directly to a frequency-commanded V20.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <math.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "TorqueModel.h"
#include "FanCalibration.h"

///////////////////////////////////////////////////////////////////////////////
// Local definitions

#define PI_F 3.14159265358979323846f

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static bool loop_value(const TorqueDesignOutput *d, float w, float *re, float *im);

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// loop_value()

static bool loop_value(const TorqueDesignOutput *d, float w, float *re, float *im)
{
    if (!(w > 0.0f) || !isfinite(w))
        return false;
    const float x = d->tf * w;
    const float cr = 1.0f + d->td * d->tf * w * w / (1.0f + x * x);
    const float ci = -1.0f / (d->ti * w) + d->td * w / (1.0f + x * x);
    const float scale = d->kp * d->kv / (d->j * w);
    *re = scale * ci;
    *im = -scale * cr;
    return isfinite(*re) && isfinite(*im);
}

///////////////////////////////////////////////////////////////////////////////
// TorqueModel_Design()

bool TorqueModel_Design(const TorqueDesignInput *in, TorqueDesignOutput *out)
{
    if (!in || !out)
        return false;
    *out = (TorqueDesignOutput){0};
    const float values[] = {in->jm,
                            in->jfan,
                            in->frequency_hz,
                            in->disturbance_ms,
                            in->error_ms,
                            in->nominal_rpm,
                            in->nominal_airspeed_ms,
                            in->sample_s,
                            in->minimum_pm_deg};
    for (unsigned k = 0; k < sizeof(values) / sizeof(values[0]); ++k)
        if (!isfinite(values[k]) || values[k] <= 0.0f)
            return false;
    if (in->error_ms >= in->disturbance_ms || in->minimum_pm_deg >= 180.0f)
        return false;
    TorqueDesignOutput d = {0};
    d.j = in->jm + in->jfan;
    d.kv = in->nominal_airspeed_ms / (2.0f * PI_F * in->nominal_rpm / 60.0f);
    const float wd = 2.0f * PI_F * in->frequency_hz;
    const float m = in->disturbance_ms / in->error_ms;
    d.kp = m * d.j * wd / d.kv;
    d.wb = d.kp * d.kv / d.j; /* 0 dB of proportional open loop */
    d.ti = 10.0f / d.wb;
    d.td = 3.0f / d.wb;
    d.tf = 1.0f / (10.0f * d.wb);
    if (!isfinite(d.kp) || !isfinite(d.tf))
        return false;
    float re, im;
    if (!loop_value(&d, wd, &re, &im))
        return false;
    d.error_ms = in->disturbance_ms / hypotf(1.0f + re, im);
    /* Final PID must satisfy exact output sensitivity, not just P-anchor. */
    if (!isfinite(d.error_ms) || d.error_ms > in->error_ms * 1.00001f)
    {
        *out = d;
        return false;
    }
    /* Third-order continuous closed-loop characteristic: Routh-Hurwitz. */
    const float a3 = d.j * d.ti * d.tf;
    const float a2 = d.j * d.ti + d.kp * d.kv * d.ti * (d.tf + d.td);
    const float a1 = d.kp * d.kv * (d.ti + d.tf);
    const float a0 = d.kp * d.kv;
    if (!(a3 > 0.0f && a2 > 0.0f && a1 > 0.0f && a0 > 0.0f && a2 * a1 > a3 * a0))
    {
        *out = d;
        return false;
    }
    float lo = d.wb * 0.0001f, hi = d.wb * 1000.0f;
    if (!loop_value(&d, lo, &re, &im) || hypotf(re, im) <= 1.0f || !loop_value(&d, hi, &re, &im) ||
        hypotf(re, im) >= 1.0f)
    {
        *out = d;
        return false;
    }
    for (unsigned n = 0; n < 80; ++n)
    {
        const float mid = sqrtf(lo * hi);
        if (!loop_value(&d, mid, &re, &im))
        {
            *out = d;
            return false;
        }
        if (hypotf(re, im) > 1.0f)
            lo = mid;
        else
            hi = mid;
    }
    d.wc = sqrtf(lo * hi);
    if (!loop_value(&d, d.wc, &re, &im))
    {
        *out = d;
        return false;
    }
    d.pm_deg = 180.0f + atan2f(im, re) * 180.0f / PI_F;
    *out = d;
    return isfinite(d.pm_deg) && d.pm_deg >= in->minimum_pm_deg &&
           d.wc < 2.0f * PI_F / (10.0f * in->sample_s);
}

///////////////////////////////////////////////////////////////////////////////
// TorqueModel_Inverse()

bool TorqueModel_Inverse(const TorqueModel *m, const TorqueModelRequest *r, TorqueModelResult *out)
{
    if (!m || !r || !out)
        return false;
    *out = (TorqueModelResult){0};
    const float vals[] = {m->inertia_kgm2, m->slip_gain_nm_per_rad_s, m->load_coeff_nm_per_rad_s2,
                          m->pole_pairs,   m->nominal_air_omega_gain, m->max_command_hz,
                          r->rotor_rad_s,  r->reference_accel_ms2,    r->pid_torque_nm};
    for (unsigned k = 0; k < sizeof(vals) / sizeof(vals[0]); ++k)
        if (!isfinite(vals[k]))
            return false;
    if (m->inertia_kgm2 <= 0 || m->slip_gain_nm_per_rad_s <= 0 || m->pole_pairs <= 0 ||
        m->nominal_air_omega_gain <= 0 || m->max_command_hz <= 0 ||
        m->load_coeff_nm_per_rad_s2 < 0 || r->rotor_rad_s < 0)
        return false;
    const float ff = m->inertia_kgm2 * r->reference_accel_ms2 / m->nominal_air_omega_gain;
    const float load = m->load_coeff_nm_per_rad_s2 * r->rotor_rad_s * fabsf(r->rotor_rad_s);
    const float torque = ff + load + r->pid_torque_nm;
    const float frequency =
        (r->rotor_rad_s + torque / m->slip_gain_nm_per_rad_s) * m->pole_pairs / (2.0f * PI_F);
    if (!isfinite(frequency) || !isfinite(torque))
        return false;
    const bool saturated = frequency < 0.0f || frequency > m->max_command_hz;
    const float limited = fminf(m->max_command_hz, fmaxf(0.0f, frequency));
    *out = (TorqueModelResult){.feedforward_torque_nm = ff,
                               .load_torque_nm = load,
                               .total_torque_nm = torque,
                               .frequency_hz = limited,
                               .requested_frequency_hz = frequency,
                               .saturated = saturated};
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// TorqueModel_Command()

bool TorqueModel_Command(const TorqueModel *m, float reference, float measurement,
                         float acceleration, float correction, bool calibrated, float base_hz,
                         TorqueModelResult *out)
{
    if (!m || !out || !isfinite(reference) || reference < 0 || !isfinite(measurement) ||
        measurement < 0)
        return false;
    const TorqueModelRequest request = {.rotor_rad_s = measurement / m->nominal_air_omega_gain,
                                        .reference_accel_ms2 = acceleration,
                                        .pid_torque_nm = correction};
    if (!TorqueModel_Inverse(m, &request, out))
        return false;
    if (calibrated)
    {
        if (!isfinite(base_hz) || base_hz < 0 || base_hz > m->max_command_hz)
            return false;
        /* Measured stationary characteristic already contains fan load and slip.
         * Add only incremental PID and acceleration, once each. */
        const float hz = base_hz + m->pole_pairs / (2 * PI_F * m->slip_gain_nm_per_rad_s) *
                                       (correction + out->feedforward_torque_nm);
        if (!isfinite(hz))
            return false;
        out->requested_frequency_hz = hz;
        out->frequency_hz = fminf(m->max_command_hz, fmaxf(0, hz));
        out->saturated = hz != out->frequency_hz;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// TorqueModel_RejectIntegration()

bool TorqueModel_RejectIntegration(const TorqueModelResult *out, float max_hz, float increment,
                                   bool fresh, float actual_hz)
{
    if (!out || !isfinite(increment))
        return false;
    return (increment > 0 &&
            (out->requested_frequency_hz > max_hz ||
             (fresh && isfinite(actual_hz) && out->frequency_hz > actual_hz + 0.2f))) ||
           (increment < 0 &&
            (out->requested_frequency_hz < 0 ||
             (fresh && isfinite(actual_hz) && out->frequency_hz < actual_hz - 0.2f)));
}

///////////////////////////////////////////////////////////////////////////////
// TorqueModel_Apply()

bool TorqueModel_Apply(const TorqueModel *m, float reference, float measurement, float acceleration,
                       float correction, TorqueModelResult *out)
{
    if (!m)
        return false;
    const bool calibrated = FanCalibration_IsValid();
    float base = 0;
    if (calibrated)
    {
        const TorqueModelRequest stationary = {.rotor_rad_s =
                                                   reference / m->nominal_air_omega_gain};
        TorqueModelResult result;
        if (!TorqueModel_Inverse(m, &stationary, &result))
            return false;
        base = FanCalibration_GetBaseFrequency(reference, result.frequency_hz);
    }
    return TorqueModel_Command(m, reference, measurement, acceleration, correction, calibrated,
                               base, out);
}
