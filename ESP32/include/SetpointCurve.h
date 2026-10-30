#pragma once
#include <stdbool.h>
typedef struct {
    double start, target, value, elapsed, duration;
    /* Existing names: first/second derivatives for an airspeed reference.
     * For frequency they represent slew (Hz/s) and acceleration (Hz/s2). */
    double requested_time, max_acceleration, max_jerk;
    double max_third_derivative; /* Optional; 0 disables. Frequency jerk: Hz/s3. */
} SetpointCurve;
bool SetpointCurve_Init(SetpointCurve *curve, float initial_mps,
                        float transition_s, float acceleration_mps2, float jerk_mps3);
/* Finish an active segment before accepting the latest target: C2 continuity.
 * Caller supplies the latest requested target every sample; intermediate requests
 * are coalesced. Stop/fault must bypass this curve entirely. */
bool SetpointCurve_Step(SetpointCurve *curve, float target_mps, float dt, float *output_mps);
