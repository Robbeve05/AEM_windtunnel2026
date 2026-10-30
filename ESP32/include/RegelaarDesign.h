/* Theoretical one-mass controller design, R.E. van Eekelen. */
#pragma once
#include <stdbool.h>
typedef struct {
 float jm,jfan,frequency_hz,disturbance_ms,error_ms;
 float nominal_rpm,nominal_airspeed_ms,sample_s,minimum_pm_deg;
} RegelaarDesignInput;
typedef struct {
 float j,kv,kp,ki,kd,ti,td,tf,wb,wc,pm_deg,error_ms;
} RegelaarDesignOutput;
bool RegelaarDesign_Calculate(const RegelaarDesignInput *input,RegelaarDesignOutput *output);
