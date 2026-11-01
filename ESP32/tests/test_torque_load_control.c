/* Host test: cc -std=c11 -I../include ../src/TorqueLoadControl.c
 * test_torque_load_control.c -lm -o test_torque_load_control
 */
#include "TorqueLoadControl.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void)
{
    RegelaarDesignOutput d={.j=0.3f,.kv=0.25f,.kp=2.0f,
       .ki=1.0f,.kd=0.0f,.tf=0.01f};
    TorqueLoadControl c;
    TorqueLoadOutput o;
    assert(TorqueLoadControl_Init(&c,&d,0.005f));
    assert(TorqueLoadControl_Step(&c,20.0f,20.0f,1.0f,0.001f,&o));
    assert(fabsf(o.pid_nm)<1.0e-5f);
    assert(fabsf(o.acceleration_nm-1.2f)<1.0e-5f);
    assert(fabsf(o.omega_est_rad_s-80.0f)<1.0e-5f);
    assert(fabsf(o.fan_load_nm-6.4f)<1.0e-4f);
    assert(fabsf(o.total_nm-7.6f)<1.0e-4f);
    assert(TorqueLoadControl_Step(&c,22.0f,20.0f,0.0f,0.0f,&o));
    assert(o.pid_nm > 4.0f);
    assert(TorqueLoadControl_SetGains(&c, 3.0f, 1.0f, 0.0f));
    assert(c.design.kp==3.0f && !c.previous_valid);
    assert(TorqueLoadControl_Step(&c,22.0f,20.0f,0.0f,0.0f,&o));
    assert(TorqueLoadControl_Step(&c,22.0f,20.0f,0.0f,0.0f,&o));
    const float prior_integral=c.integral_nm;
    assert(TorqueLoadControl_Step(&c,22.0f,20.0f,0.0f,0.0f,&o));
    TorqueLoadControl_RejectLatestIntegration(&c,&o);
    assert(c.integral_nm==prior_integral);
    TorqueLoadControl_Reset(&c);
    assert(!TorqueLoadControl_Step(&c,22.0f,-1.0f,0.0f,0.0f,&o));
    puts("PASS torque-load diagnostics");
    return 0;
}
