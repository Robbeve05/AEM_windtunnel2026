#include "Regelaar.h"
#include <math.h>

static float clamp(float x, float lo, float hi) { return fminf(hi, fmaxf(lo, x)); }
void Regelaar_Reset(Regelaar *p)
{
    p->integral = p->previous_error = p->previous_measurement = p->derivative = 0;
    p->initialized = false;
}
bool Regelaar_Init(Regelaar *p, float kp, float ki, float kd, float dt,
                   float lo, float hi, float tau)
{
    if (!p || !isfinite(kp) || !isfinite(ki) || !isfinite(kd) ||
        !isfinite(dt) || !isfinite(lo) || !isfinite(hi) || !isfinite(tau) ||
        kp < 0 || ki < 0 || kd < 0 || dt <= 0 || lo >= hi || tau < 0) return false;
    *p = (Regelaar){.kp=kp, .ki=ki, .kd=kd, .dt=dt,
                    .minimum=lo, .maximum=hi, .filter_tau=tau};
    return true;
}
bool Regelaar_Step(Regelaar *p, float sp, float y, float *out)
{
    if (!p || !out) return false;
    *out = 0;
    if (!isfinite(sp) || !isfinite(y) || !isfinite(p->dt) || p->dt <= 0 ||
        !isfinite(p->filter_tau) || p->filter_tau < 0) return false;
    float error = sp - y;
    float integral_delta = p->initialized
        ? p->ki * p->dt * 0.5f * (error + p->previous_error)
        : p->ki * p->dt * 0.5f * error;
    float integral = p->integral + integral_delta;

    /* Bilinear (Tustin) form of a filtered derivative on the measurement. */
    float derivative = 0;
    if (p->initialized && p->kd > 0) {
        float denominator = 2.0f*p->filter_tau + p->dt;
        float previous_factor = (2.0f*p->filter_tau - p->dt) / denominator;
        float measurement_factor = 2.0f*p->kd / denominator;
        derivative = previous_factor*p->derivative -
            measurement_factor*(y-p->previous_measurement);
    }
    float requested = p->kp*error + integral + derivative;
    if (!isfinite(integral) || !isfinite(requested) || !isfinite(derivative))
        return false;
    /* Conditional integration: stop opbouwen als de fout verder de limiet in duwt. */
    if ((requested > p->maximum && error > 0) || (requested < p->minimum && error < 0))
        integral_delta = 0;
    integral = p->integral + integral_delta;
    requested = p->kp*error + integral + derivative;
    if (!isfinite(requested)) return false;
    p->integral = integral;
    p->previous_error = error;
    p->derivative = derivative;
    p->previous_measurement = y;
    p->initialized = true;
    *out = clamp(requested, p->minimum, p->maximum);
    return true;
}
