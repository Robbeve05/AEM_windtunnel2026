///////////////////////////////////////////////////////////////////////////////
// File information

/* Quasi-static inverse motor/fan model. Physical use requires verified
 * calibration. */
///////////////////////////////////////////////////////////////////////////////
// Include guard
#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    float inertia_kgm2, slip_gain_nm_per_rad_s, load_coeff_nm_per_rad_s2;
    float pole_pairs, nominal_air_omega_gain;
    float max_command_hz;
} MotorModel;
typedef struct
{
    float rotor_rad_s, reference_accel_ms2, pid_torque_nm;
} MotorModelRequest;
typedef struct
{
    float feedforward_torque_nm, load_torque_nm, total_torque_nm, frequency_hz;
    float requested_frequency_hz;
    bool saturated;
} MotorModelResult;
/* Uses Tmotor=Ks*(2*pi*f/p-omega), Tload=b*omega*abs(omega).
 * Local approximation, not a calibrated V20 torque loop. Clips Hz to [0,max].
 */
///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool MotorModel_TorqueToFrequency(const MotorModel *model, const MotorModelRequest *request,
                                  MotorModelResult *result);
