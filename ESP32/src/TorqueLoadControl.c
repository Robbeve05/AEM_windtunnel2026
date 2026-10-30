/* Torque-domain one-mass load controller; simulation/diagnostics only.
 * R.E. van Eekelen. The V20 does NOT accept this torque as a setpoint.
 */
#include "TorqueLoadControl.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

bool TorqueLoadControl_Init(TorqueLoadControl *c,
                            const RegelaarDesignOutput *d, float sample_s)
{
    if (!c || !d || !isfinite(sample_s) || sample_s <= 0.0f ||
        !isfinite(d->j) || d->j <= 0.0f ||
        !isfinite(d->kv) || d->kv <= 0.0f ||
        !isfinite(d->kp) || !isfinite(d->ki) || !isfinite(d->kd) ||
        !isfinite(d->tf) || d->tf <= 0.0f ||
        d->kp < 0.0f || d->ki < 0.0f || d->kd < 0.0f)
        return false;
    memset(c, 0, sizeof(*c));
    c->design = *d;
    c->sample_s = sample_s;
    return true;
}

void TorqueLoadControl_Reset(TorqueLoadControl *c)
{
    if (!c) return;
    c->integral_nm = 0.0f;
    c->previous_error_ms = 0.0f;
    c->previous_measurement_ms = 0.0f;
    c->derivative_nm = 0.0f;
    c->previous_reference_ms = 0.0f;
    c->previous_valid = false;
}

bool TorqueLoadControl_Step(TorqueLoadControl *c, float reference_ms,
                            float measured_ms, float reference_accel_ms2,
                            float fan_load_coefficient, TorqueLoadOutput *out)
{
    if (!c || !out || !isfinite(reference_ms) || !isfinite(measured_ms) ||
        !isfinite(reference_accel_ms2) ||
        !isfinite(fan_load_coefficient) || fan_load_coefficient < 0.0f ||
        measured_ms < 0.0f)
        return false;
    *out = (TorqueLoadOutput){0};
    const float dt = c->sample_s;
    const float error = reference_ms - measured_ms;
    const float kp = c->design.kp, ki = c->design.ki;
    const float kd = c->design.kd, tf = c->design.tf;

    /* Tustin integral; no implicit actuator anti-windup. Use diagnostics only. */
    const float integral = c->integral_nm +
         (c->previous_valid ? ki * dt * 0.5f * (error + c->previous_error_ms) : 0.0f);
    /* Filtered derivative on measurement avoids reference-setpoint derivative kick. */
    const float derivative = c->previous_valid ?
         ((2.0f * tf - dt) * c->derivative_nm -
          2.0f * kd * (measured_ms - c->previous_measurement_ms)) /
          (2.0f * tf + dt) : 0.0f;
    const float omega_est = measured_ms / c->design.kv;
    const float pid = kp * error + integral + derivative;
    const float accel = c->design.j * reference_accel_ms2 / c->design.kv;
    const float fan = fan_load_coefficient * omega_est * fabsf(omega_est);
    const float total = pid + accel + fan;
    if (!isfinite(integral) || !isfinite(derivative) || !isfinite(pid) ||
        !isfinite(accel) || !isfinite(fan) || !isfinite(total)) return false;
    c->integral_nm = integral;
    c->derivative_nm = derivative;
    c->previous_error_ms = error;
    c->previous_measurement_ms = measured_ms;
    c->previous_reference_ms = reference_ms;
    c->previous_valid = true;
    *out = (TorqueLoadOutput){.pid_nm=pid,.acceleration_nm=accel,
         .fan_load_nm=fan,.total_nm=total,.omega_est_rad_s=omega_est};
    return true;
}
