///////////////////////////////////////////////////////////////////////////////
// File information

/* Ideal ISA PID, Tustin integral and filtered derivative on measurement. */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "Regelaar.h"

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static bool parameters_valid(const RegelaarParameters *p);

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// Regelaar_Init()

bool Regelaar_Init(Regelaar *c, const RegelaarParameters *p)
{
    if (!c || !parameters_valid(p))
        return false;
    c->parameters = *p;
    Regelaar_Reset(c);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// parameters_valid()

static bool parameters_valid(const RegelaarParameters *p)
{
    return p && isfinite(p->kp) && p->kp >= 0 && isfinite(p->ti_s) && p->ti_s >= 0 &&
           isfinite(p->td_s) && p->td_s >= 0 && isfinite(p->tf_s) && p->tf_s >= 0 &&
           isfinite(p->sample_s) && p->sample_s > 0 && p->mode >= REGELAAR_NONE &&
           p->mode <= REGELAAR_PID && (p->mode < REGELAAR_PI || p->ti_s > 0) &&
           (p->mode != REGELAAR_PID || p->td_s == 0 || p->tf_s > 0);
}

///////////////////////////////////////////////////////////////////////////////
// Regelaar_Reset()

void Regelaar_Reset(Regelaar *c)
{
    if (!c)
        return;
    const RegelaarParameters p = c->parameters;
    memset(c, 0, sizeof(*c));
    c->parameters = p;
    c->valid = parameters_valid(&p);
}

///////////////////////////////////////////////////////////////////////////////
// Regelaar_SetParameters()

bool Regelaar_SetParameters(Regelaar *c, const RegelaarParameters *p)
{
    return Regelaar_Init(c, p);
}

///////////////////////////////////////////////////////////////////////////////
// Regelaar_FromLegacy()

bool Regelaar_FromLegacy(float kp, float ki, float kd, float tf, float dt, RegelaarMode mode,
                         RegelaarParameters *out)
{
    if (!out || !isfinite(kp) || kp < 0 || !isfinite(ki) || ki < 0 || !isfinite(kd) || kd < 0)
        return false;
    /* Independent integral/derivative with Kp=0 has no finite ISA equivalent. */
    if (kp == 0 && (ki > 0 || kd > 0))
        return false;
    if (mode >= REGELAAR_PI && ki == 0)
    {
        if (mode == REGELAAR_PID && kd > 0)
            return false;
        mode = REGELAAR_P;
    }
    const RegelaarParameters p = {.kp = kp,
                                  .ti_s = ki > 0 ? kp / ki : 0,
                                  .td_s = kp > 0 ? kd / kp : 0,
                                  .tf_s = tf,
                                  .sample_s = dt,
                                  .mode = mode};
    if (!parameters_valid(&p))
        return false;
    *out = p;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Regelaar_Step()

bool Regelaar_Step(Regelaar *c, float reference, float measurement, RegelaarOutput *out)
{
    if (!c || !out || !c->valid || !parameters_valid(&c->parameters) || !isfinite(reference) ||
        !isfinite(measurement))
        return false;
    const RegelaarParameters *p = &c->parameters;
    const float e = reference - measurement;
    const float proportional = p->mode == REGELAAR_NONE ? 0 : p->kp * e;
    float integral = 0, derivative = 0;
    if (p->mode >= REGELAAR_PI)
        integral =
            c->integral +
            (c->previous_valid ? p->kp * p->sample_s / (2 * p->ti_s) * (e + c->previous_error) : 0);
    if (p->mode == REGELAAR_PID && p->td_s > 0 && c->previous_valid)
        derivative = ((2 * p->tf_s - p->sample_s) * c->derivative -
                      2 * p->kp * p->td_s * (measurement - c->previous_measurement)) /
                     (2 * p->tf_s + p->sample_s);
    const float value = proportional + integral + derivative;
    if (!isfinite(e) || !isfinite(integral) || !isfinite(derivative) || !isfinite(value))
        return false;
    *out = (RegelaarOutput){.proportional = proportional,
                            .integral = integral,
                            .derivative = derivative,
                            .value = value,
                            .error = e,
                            .integral_increment = integral - c->integral};
    c->integral_before_step = c->integral;
    c->integral = integral;
    c->derivative = derivative;
    c->previous_error = e;
    c->previous_measurement = measurement;
    c->previous_valid = true;
    c->integration_pending = p->mode >= REGELAAR_PI;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Regelaar_RejectLatestIntegration()

bool Regelaar_RejectLatestIntegration(Regelaar *c, RegelaarOutput *out)
{
    if (!c || !out || !c->valid || !c->integration_pending)
        return false;
    const float value = out->proportional + c->integral_before_step + out->derivative;
    if (!isfinite(value))
        return false;
    c->integral = c->integral_before_step;
    c->integration_pending = false;
    out->integral = c->integral;
    out->integral_increment = 0;
    out->value = value;
    return true;
}
