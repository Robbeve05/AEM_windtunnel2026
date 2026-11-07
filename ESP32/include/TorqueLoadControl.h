///////////////////////////////////////////////////////////////////////////////
// File information

/* Airspeed torque PID with acceleration/load feedforward, in Nm.
 * Requires calibrated inverse VFD model; author R.E. van Eekelen. */
///////////////////////////////////////////////////////////////////////////////
// Include guard
#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes
#include "RegelaarDesign.h"

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    RegelaarDesignOutput design;
    float sample_s, integral_nm, integral_before_step_nm, previous_error_ms;
    float previous_measurement_ms, derivative_nm, previous_reference_ms;
    bool previous_valid;
} TorqueLoadControl;
typedef struct
{
    float pid_nm, acceleration_nm, fan_load_nm, total_nm;
    float omega_est_rad_s;
} TorqueLoadOutput;
///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool TorqueLoadControl_Init(TorqueLoadControl *c, const RegelaarDesignOutput *design,
                            float sample_s);
bool TorqueLoadControl_Step(TorqueLoadControl *c, float reference_ms, float measured_ms,
                            float reference_accel_ms2, float fan_load_coefficient,
                            TorqueLoadOutput *out);
void TorqueLoadControl_Reset(TorqueLoadControl *c);

bool TorqueLoadControl_SetGains(TorqueLoadControl *controller, float kp, float ki, float kd);
void TorqueLoadControl_RejectLatestIntegration(TorqueLoadControl *controller,
                                               TorqueLoadOutput *output);
