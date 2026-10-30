/* Host test for theoretical one-mass design.
 * Compile: cc -std=c11 -Wall -Wextra -I../include ../src/RegelaarDesign.c test_regelaar_design.c -lm -o test_design
 */
#include "RegelaarDesign.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int close_to(float a,float b,float tol) {return fabsf(a-b)<=tol;}
int main(void)
{
 RegelaarDesignInput in={
 .jm=0.1f,.jfan=0.2f,.frequency_hz=0.02f,.disturbance_ms=5.0f,
 .error_ms=0.5f,.nominal_rpm=1450.0f,.nominal_airspeed_ms=40.0f,
 .sample_s=0.005f,.minimum_pm_deg=45.0f};
 RegelaarDesignOutput out={0};
 assert(RegelaarDesign_Calculate(&in,&out));
 assert(close_to(out.j,0.3f,1e-6f));
 assert(close_to(out.ti,10.0f/out.wb,1e-5f));
 assert(close_to(out.td,3.0f/out.wb,1e-5f));
 assert(close_to(out.tf,1.0f/(10.0f*out.wb),1e-5f));
 assert(out.error_ms<=in.error_ms);
 assert(out.pm_deg>=in.minimum_pm_deg);
 printf("Kp=%g Ti=%g Td=%g Tf=%g PM=%g error=%g\n",
  out.kp,out.ti,out.td,out.tf,out.pm_deg,out.error_ms);
 in.jfan=0.0f;
 assert(!RegelaarDesign_Calculate(&in,&out));
 in.jfan=0.2f;
 in.error_ms=0.0f;
 assert(!RegelaarDesign_Calculate(&in,&out));
 puts("PASS");
 return 0;
}
