/* Executed by run_host_tests.ps1 with the host calibration boundary. */
#include "TorqueModel.h"
#include "AppConfig.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int close_to(float a,float b,float tol) {return fabsf(a-b)<=tol;}
int main(void)
{
 TorqueDesignInput in={
 .jm=J_MOTOR_KGM2,.jfan=J_FAN_KGM2,
 .frequency_hz=DISTURBANCE_FREQUENCY_HZ,
 .disturbance_ms=DISTURBANCE_AMPLITUDE_MS,
 .error_ms=ALLOWABLE_ERROR_MS,.nominal_rpm=DESIGN_NOMINAL_RPM,
 .nominal_airspeed_ms=LUCHTSNELHEID_NOM,
 .sample_s=CONTROL_CLOCK_PERIOD_S,
 .minimum_pm_deg=DESIGN_MIN_PHASE_MARGIN_DEG};
 TorqueDesignOutput out={0};
 assert(TorqueModel_Design(&in,&out));
 assert(close_to(J_MOTOR_KGM2,0.0182f,1e-7f));
 assert(close_to(J_FAN_KGM2,0.3530f,1e-7f));
 assert(close_to(J_TOTAL_KGM2,0.3712f,1e-6f));
 assert(close_to(out.j,J_TOTAL_KGM2,1e-6f));
 assert(MOTOR_VARIANT_CONFIRMED==1);
 /* Manufacturer-supplied inertia does not enable hardware. */
 assert(MOTOR_MODEL_TORQUE_TO_V20_ENABLED==0);
 assert(SAFETY_IO_COMMISSIONED==1);
 assert(V20_RUN_COMMISSIONED==1);
 assert(close_to(out.ti,10.0f/out.wb,1e-5f));
 assert(close_to(out.td,3.0f/out.wb,1e-5f));
 assert(close_to(out.tf,1.0f/(10.0f*out.wb),1e-5f));
 assert(out.error_ms<=in.error_ms);
 assert(out.pm_deg>=in.minimum_pm_deg);
 printf("Kp=%g Ti=%g Td=%g Tf=%g PM=%g error=%g\n",
  out.kp,out.ti,out.td,out.tf,out.pm_deg,out.error_ms);
 in.jfan=0.0f;
 assert(!TorqueModel_Design(&in,&out));
 in.jfan=J_FAN_KGM2;
 in.error_ms=0.0f;
 assert(!TorqueModel_Design(&in,&out));
 puts("PASS");
 return 0;
}
