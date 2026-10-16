#include "SetpointCurve.h"
#include <math.h>
#include <stddef.h>
bool SetpointCurve_Init(SetpointCurve *c, float initial, float time, float acceleration, float jerk)
{
    if (!c || !isfinite(initial) || !isfinite(time) || !isfinite(acceleration) ||
        !isfinite(jerk) || time<=0 || acceleration<=0 || jerk<=0) return false;
    *c=(SetpointCurve){.start=initial,.target=initial,.value=initial,
        .requested_time=time,.max_acceleration=acceleration,.max_jerk=jerk};
    return true;
}
bool SetpointCurve_Step(SetpointCurve *c, float target, float dt, float *output)
{
    if (!c || !output || !isfinite(target) || !isfinite(dt) || dt<=0 ||
        c->requested_time<=0 || c->max_acceleration<=0 || c->max_jerk<=0) return false;
    if (c->elapsed>=c->duration && c->target!=(double)target) {
        c->start=c->value;
        c->target=target;
        const double delta=fabs(c->target-c->start);
        /* Airspeed trajectory v=v0+delta*(10q^3-15q^4+6q^5).
         * max |dv/dt| = (15/8)*|delta|/T (acceleration, m/s^2).
         * max |d2v/dt2| = (10/sqrt(3))*|delta|/T^2 (jerk, m/s^3).
         * v, acceleration and jerk join continuously at both endpoints.
         */
        c->duration=fmax(c->requested_time,
            fmax(1.875*delta/c->max_acceleration,
                 sqrt((10.0/sqrt(3.0))*delta/c->max_jerk)));
        c->elapsed=0;
    }
    if (c->elapsed<c->duration) {
        c->elapsed=fmin(c->duration,c->elapsed+(double)dt);
        double q=c->elapsed/c->duration;
        double blend=q*q*q*(10.0+q*(-15.0+6.0*q));
        c->value=c->start+(c->target-c->start)*blend;
        if (c->elapsed>=c->duration) c->value=c->target;
    }
    if (!isfinite(c->value)) return false;
    *output=(float)c->value;
    return true;
}
