/* Quasi-static induction motor and fan model. SIMULATION ONLY. */
#pragma once
#include <stdbool.h>
typedef struct {
 float inertia_kgm2, slip_gain_nm_per_rad_s, load_coeff_nm_per_rad_s2;
 float pole_pairs, nominal_air_omega_gain;
 float max_command_hz;
} MotorModel;
typedef struct {
 float rotor_rad_s, reference_accel_ms2, pid_torque_nm;
} MotorModelRequest;
typedef struct {
 float feedforward_torque_nm, load_torque_nm, total_torque_nm, frequency_hz;
} MotorModelResult;
/* Uses Tmotor=Ks*(2*pi*f/p-omega), Tload=b*omega*abs(omega).
 * This local approximation is NOT a V20 drive mapping. */
bool MotorModel_TorqueToFrequency(const MotorModel *model,const MotorModelRequest *request,MotorModelResult *result);
