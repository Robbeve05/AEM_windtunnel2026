/* Executed by run_host_tests.ps1 with the host calibration boundary. */
#include "TorqueModel.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 const float pi=3.14159265358979323846f;
 const float omega0=2*pi*1450.0f/60.0f;
 const float torque0=4000.0f/omega0;
 const float ks=torque0/(pi*50.0f-omega0);
 const float kl=torque0/(omega0*omega0);
 TorqueModel m={.inertia_kgm2=0.3f,.slip_gain_nm_per_rad_s=ks,
   .load_coeff_nm_per_rad_s2=kl,.pole_pairs=2.0f,
   .nominal_air_omega_gain=40.0f/omega0,.max_command_hz=50.0f};
 TorqueModelRequest q={.rotor_rad_s=omega0,.reference_accel_ms2=0,.pid_torque_nm=0};
 TorqueModelResult o;
 assert(TorqueModel_Inverse(&m,&q,&o));
 assert(fabsf(o.frequency_hz-50.0f)<1.0e-3f);
 assert(fabsf(o.load_torque_nm-torque0)<1.0e-3f);
 q.reference_accel_ms2=1.0f;
 assert(TorqueModel_Inverse(&m,&q,&o));
 assert(o.saturated && o.requested_frequency_hz>50.0f && o.frequency_hz==50.0f);
 q.reference_accel_ms2=0; q.rotor_rad_s=-1;
 assert(!TorqueModel_Inverse(&m,&q,&o));
 puts("PASS motor-model tests");
 return 0;
}
