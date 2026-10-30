/* Quasi-static slip/inertia fan model, R.E. van Eekelen.
 * No real V20 actuation should use this approximate model.
 */
#include "MotorModel.h"
#include <math.h>
#include <stddef.h>
#define PI_F 3.14159265358979323846f
bool MotorModel_TorqueToFrequency(const MotorModel *m,const MotorModelRequest *r,MotorModelResult *out)
{
 if(!m||!r||!out)return false;
 *out=(MotorModelResult){0};
 const float vals[]={m->inertia_kgm2,m->slip_gain_nm_per_rad_s,
  m->load_coeff_nm_per_rad_s2,m->pole_pairs,m->nominal_air_omega_gain,
  m->max_command_hz,r->rotor_rad_s,r->reference_accel_ms2,r->pid_torque_nm};
 for(unsigned k=0;k<sizeof(vals)/sizeof(vals[0]);++k)
  if(!isfinite(vals[k]))return false;
 if(m->inertia_kgm2<=0||m->slip_gain_nm_per_rad_s<=0||m->pole_pairs<=0||
    m->nominal_air_omega_gain<=0||m->max_command_hz<=0||
    m->load_coeff_nm_per_rad_s2<0||r->rotor_rad_s<0)return false;
 const float ff=m->inertia_kgm2*r->reference_accel_ms2/m->nominal_air_omega_gain;
 const float load=m->load_coeff_nm_per_rad_s2*r->rotor_rad_s*fabsf(r->rotor_rad_s);
 const float torque=ff+load+r->pid_torque_nm;
 const float frequency=(r->rotor_rad_s+torque/m->slip_gain_nm_per_rad_s)*
                        m->pole_pairs/(2.0f*PI_F);
 if(!isfinite(frequency)||!isfinite(torque)||frequency<0.0f||
    frequency>m->max_command_hz)return false;
 *out=(MotorModelResult){.feedforward_torque_nm=ff,.load_torque_nm=load,
  .total_torque_nm=torque,.frequency_hz=frequency};
 return true;
}
