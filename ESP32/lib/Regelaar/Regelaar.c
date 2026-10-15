#include "Regelaar.h"
#include <math.h>

static float clamp(float x, float lo, float hi) { return fminf(hi, fmaxf(lo, x)); }
void Regelaar_Reset(Regelaar *p)
{
    p->integral = p->previous_measurement = p->derivative = 0;
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
    if (!isfinite(sp) || !isfinite(y)) return false;
    float error = sp - y;
    float raw_d = p->initialized ? -(y-p->previous_measurement)/p->dt : 0;
    float derivative = p->derivative + p->dt/(p->filter_tau+p->dt)*(raw_d-p->derivative);
    float integral = p->integral + p->ki * p->dt * error;
    float requested = p->kp*error + integral + p->kd*derivative;
    if (!isfinite(requested) || !isfinite(derivative)) return false;
    /* Conditional integration: stop opbouwen als de fout verder de limiet in duwt. */
    if ((requested > p->maximum && error > 0) || (requested < p->minimum && error < 0))
        integral = p->integral;
    requested = p->kp*error + integral + p->kd*derivative;
    if (!isfinite(requested)) return false;
    p->integral = integral;
    p->derivative = derivative;
    p->previous_measurement = y;
    p->initialized = true;
    *out = clamp(requested, p->minimum, p->maximum);
    return true;
}
