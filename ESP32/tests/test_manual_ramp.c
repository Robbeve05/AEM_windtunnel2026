/* Offline cascade: production curve + linear V20 RFG. No hardware writes. */
#include "AppConfig.h"
#include "SetpointCurve.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void init(SetpointCurve *c, float start)
{
    assert(SetpointCurve_Init(c,start,MANUAL_TRANSITION_TIME_S,
        MANUAL_MAX_SLEW_HZ_PER_S,MANUAL_MAX_ACCEL_HZ_PER_S2));
    c->max_third_derivative=MANUAL_MAX_JERK_HZ_PER_S3;
}
static void segment(float start,float target)
{
    SetpointCurve c; init(&c,start);
    float out=start,previous=start;
    for(int n=1;n<=500;n++) {
        assert(SetpointCurve_Step(&c,target,.01f,&out));
        assert(c.duration==5);
        double q=c.elapsed/c.duration,d=target-start,T=c.duration;
        double slew=d*30*q*q*(1-q)*(1-q)/T;
        double accel=d*60*q*(1-q)*(1-2*q)/(T*T);
        double jerk=d*(60-360*q+360*q*q)/(T*T*T);
        assert(fabs(slew)<=MANUAL_MAX_SLEW_HZ_PER_S+1e-6);
        assert(fabs(accel)<=MANUAL_MAX_ACCEL_HZ_PER_S2+1e-6);
        assert(fabs(jerk)<=MANUAL_MAX_JERK_HZ_PER_S3+1e-6);
        assert(out>=fminf(start,target) && out<=fmaxf(start,target));
        assert(target>start ? out>=previous : out<=previous);
        if(n==499) assert(c.elapsed<c.duration);
        previous=out;
    }
    assert(c.elapsed==5 && out==target);
}
static void retarget(void)
{
    SetpointCurve c; init(&c,0); float out=0;
    for(int n=0;n<500;n++) {
        /* 20 is superseded by 30 before the active curve ends. */
        float target=n<100?50.0f:n<200?20.0f:30.0f;
        assert(SetpointCurve_Step(&c,target,.01f,&out));
        assert(c.target==50 && out>=0 && out<=50);
    }
    assert(out==50);
    assert(SetpointCurve_Step(&c,30,.01f,&out));
    assert(c.start==50 && c.target==30 && out<=50);
    for(int n=1;n<500;n++) {
        assert(SetpointCurve_Step(&c,30,.01f,&out));
        assert(out>=30 && out<=50);
    }
    assert(out==30 && c.elapsed==5);
}
/* P1082=50Hz, no rounding/current limit/slip model. Optional zero-order
 * held Modbus HSW with 100ms updates and actual 14-bit reference encoding.
 * 1ms integration, 10ms production control steps. Returns first endpoint time. */
static double cascade(float ramp_s,int write_ms)
{
    SetpointCurve c; init(&c,0);
    float out=0; double actual=0,held=0,at5=0,reached=0;
    for(int ms=0;ms<30000;ms++) {
        if(ms>0 && ms%10==0) assert(SetpointCurve_Step(&c,50,.01f,&out));
        if(ms%write_ms==0)
            held=lround(out*16384.0/50.0)*50.0/16384.0;
        if(ms==5000) at5=actual;
        double delta=held-actual,limit=50.0/ramp_s*.001;
        actual+=fmin(limit,fmax(-limit,delta));
        assert(actual>=0 && actual<=50);
        if(actual>=50-1e-8 && reached==0) reached=(ms+1)/1000.0;
        if(reached>0 && ms>=5000) {
            assert(c.elapsed==5);
            printf("P1120=%4.1fs; updates=%3dms; software=5.000s; simulated_50Hz=%.3fs; Hz_at_5s=%.3f\n",
                ramp_s,write_ms,reached,at5);
            if(ramp_s==2) assert(reached>=4.9 && reached<5.02);
            if(ramp_s==10) assert(reached>10 && reached<12);
            return reached;
        }
    }
    assert(0); return 0;
}
int main(void)
{
    segment(0,50); segment(50,20); retarget();
    puts("PASS manual quintic exact 500 ticks, three derivative limits, no overshoot and latest-target queue");
    const float ramps[]={2,5,10,20};
    const double fine[]={4.931,5.744,10.495,20.340};
    const double serial[]={5.001,5.834,10.583,20.430};
    for(unsigned i=0;i<sizeof(ramps)/sizeof(ramps[0]);i++) {
        assert(fabs(cascade(ramps[i],1)-fine[i])<.002);
        assert(fabs(cascade(ramps[i],(int)V20_COMMAND_PERIOD_MS)-serial[i])<.002);
    }
    return 0;
}
