/* One-mass theoretical PID design. Author: R.E. van Eekelen.
 * Model: incremental shaft torque [Nm] -> airspeed [m/s].
 * Never apply gains directly to a frequency-commanded V20.
 */
#include "RegelaarDesign.h"
#include <math.h>
#include <stddef.h>
#define PI_F 3.14159265358979323846f
static bool loop_value(const RegelaarDesignOutput *d,float w,float *re,float *im)
{
 if (!(w>0.0f) || !isfinite(w)) return false;
 const float x=d->tf*w;
 const float cr=1.0f+d->td*d->tf*w*w/(1.0f+x*x);
 const float ci=-1.0f/(d->ti*w)+d->td*w/(1.0f+x*x);
 const float scale=d->kp*d->kv/(d->j*w);
 *re=scale*ci; *im=-scale*cr;
 return isfinite(*re)&&isfinite(*im);
}
bool RegelaarDesign_Calculate(const RegelaarDesignInput *in,RegelaarDesignOutput *out)
{
 if(!in||!out) return false;
 *out=(RegelaarDesignOutput){0};
 const float values[]={in->jm,in->jfan,in->frequency_hz,in->disturbance_ms,
  in->error_ms,in->nominal_rpm,in->nominal_airspeed_ms,in->sample_s,
  in->minimum_pm_deg};
 for(unsigned k=0;k<sizeof(values)/sizeof(values[0]);++k)
  if(!isfinite(values[k])||values[k]<=0.0f) return false;
 if(in->error_ms>=in->disturbance_ms||in->minimum_pm_deg>=180.0f) return false;
 RegelaarDesignOutput d={0};
 d.j=in->jm+in->jfan;
 d.kv=in->nominal_airspeed_ms/(2.0f*PI_F*in->nominal_rpm/60.0f);
 const float wd=2.0f*PI_F*in->frequency_hz;
 const float m=in->disturbance_ms/in->error_ms;
 d.kp=m*d.j*wd/d.kv;
 d.wb=d.kp*d.kv/d.j; /* 0 dB of proportional open loop */
 d.ti=10.0f/d.wb;
 d.td=3.0f/d.wb;
 d.tf=1.0f/(10.0f*d.wb);
 d.ki=d.kp/d.ti;
 d.kd=d.kp*d.td;
 if(!isfinite(d.kp)||!isfinite(d.ki)||!isfinite(d.kd)||!isfinite(d.tf)) return false;
 float re,im;
 if(!loop_value(&d,wd,&re,&im)) return false;
 d.error_ms=in->disturbance_ms/hypotf(1.0f+re,im);
 /* Final PID must satisfy exact output sensitivity, not just P-anchor. */
 if(!isfinite(d.error_ms)||d.error_ms>in->error_ms*1.00001f) { *out=d;return false; }
 /* Third-order continuous closed-loop characteristic: Routh-Hurwitz. */
 const float a3=d.j*d.ti*d.tf;
 const float a2=d.j*d.ti+d.kp*d.kv*d.ti*(d.tf+d.td);
 const float a1=d.kp*d.kv*(d.ti+d.tf);
 const float a0=d.kp*d.kv;
 if(!(a3>0.0f&&a2>0.0f&&a1>0.0f&&a0>0.0f&&a2*a1>a3*a0)) {
  *out=d;return false;
 }
 float lo=d.wb*0.0001f,hi=d.wb*1000.0f;
 if(!loop_value(&d,lo,&re,&im)||hypotf(re,im)<=1.0f||
    !loop_value(&d,hi,&re,&im)||hypotf(re,im)>=1.0f) { *out=d;return false; }
 for(unsigned n=0;n<80;++n) {
  const float mid=sqrtf(lo*hi);
  if(!loop_value(&d,mid,&re,&im)) { *out=d;return false; }
  if(hypotf(re,im)>1.0f)lo=mid;else hi=mid;
 }
 d.wc=sqrtf(lo*hi);
 if(!loop_value(&d,d.wc,&re,&im)) { *out=d;return false; }
 d.pm_deg=180.0f+atan2f(im,re)*180.0f/PI_F;
 *out=d;
 return isfinite(d.pm_deg)&&d.pm_deg>=in->minimum_pm_deg&&
        d.wc<2.0f*PI_F/(10.0f*in->sample_s);
}
