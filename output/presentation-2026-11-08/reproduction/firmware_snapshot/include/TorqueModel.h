///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * TorqueModel.h
 *
 * Torque plant design, inertia/load feedforward and inverse motor model.
 *
 * Author: R.E. van Eekelen
 */

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
    float jm, jfan, frequency_hz, disturbance_ms, error_ms;
    float nominal_rpm, nominal_airspeed_ms, sample_s, minimum_pm_deg;
} TorqueDesignInput;
typedef struct
{
    float j, kv, kp, ti, td, tf, wb, wc, pm_deg, error_ms;
} TorqueDesignOutput;
typedef struct
{
    float inertia_kgm2, slip_gain_nm_per_rad_s, load_coeff_nm_per_rad_s2;
    float pole_pairs, nominal_air_omega_gain;
    float max_command_hz;
} TorqueModel;
typedef struct
{
    float rotor_rad_s, reference_accel_ms2, pid_torque_nm;
} TorqueModelRequest;
typedef struct
{
    float feedforward_torque_nm, load_torque_nm, total_torque_nm, frequency_hz;
    float requested_frequency_hz;
    bool saturated;
} TorqueModelResult;
/* Uses Tmotor=Ks*(2*pi*f/p-omega), Tload=b*omega*abs(omega).
 * Local approximation, not a calibrated V20 torque loop. Clips Hz to [0,max].
 */

///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool TorqueModel_Design(const TorqueDesignInput *input, TorqueDesignOutput *output);
bool TorqueModel_Inverse(const TorqueModel *model, const TorqueModelRequest *request,
                         TorqueModelResult *result);

/* Calibration replaces stationary fan/slip feedforward; never add it twice. */
bool TorqueModel_Command(const TorqueModel *, float reference, float measurement,
                         float acceleration, float correction, bool calibrated, float base_hz,
                         TorqueModelResult *);
bool TorqueModel_RejectIntegration(const TorqueModelResult *, float max_hz, float increment,
                                   bool feedback_fresh, float actual_hz);
bool TorqueModel_Apply(const TorqueModel *, float reference, float measurement, float acceleration,
                       float correction, TorqueModelResult *);
