#include "SetpointCurve.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void transition(float from,float to,float time,float a,float j)
{
    SetpointCurve c; float out;
    assert(SetpointCurve_Init(&c,from,time,a,j));
    assert(SetpointCurve_Step(&c,to,.001f,&out));
    if (from==to) { assert(out==to); return; }
    assert(c.duration+1e-9>=time);
    const double delta=to-from,T=c.duration;
    assert(1.875*fabs(delta)/T<=a+1e-9);
    assert(10/sqrt(3.0)*fabs(delta)/(T*T)<=j+1e-9);
    double last=c.value;
    for (int n=0;c.elapsed<c.duration && n<1000000;++n) {
        assert(SetpointCurve_Step(&c,to,.001f,&out));
        double q=c.elapsed/T;
        double acceleration=delta*30*q*q*(1-q)*(1-q)/T;
        double jerk=delta*60*q*(1-q)*(1-2*q)/(T*T);
        assert(fabs(acceleration)<=a+1e-9 && fabs(jerk)<=j+1e-9);
        assert(delta>0 ? c.value>=last-1e-10 : c.value<=last+1e-10);
        assert(c.value>=fmin(from,to)-1e-10 && c.value<=fmax(from,to)+1e-10);
        last=c.value;
    }
    assert(out==to && c.elapsed==c.duration);
    assert(SetpointCurve_Step(&c,to,.001f,&out) && out==to);
}
int main(void)
{
    transition(0,10,5,2,2);
    transition(20,0,5,2,2);
    transition(1,1,5,2,2);
    transition(0,.01f,5,2,2);
    transition(0,30,.1f,100,1); /* jerk determines duration */
    transition(0,10,20,2,2); /* requested duration dominates */
    SetpointCurve c; float out;
    assert(SetpointCurve_Init(&c,0,5,2,2));
    for(int n=0;n<1000;++n) assert(SetpointCurve_Step(&c,10,.001f,&out));
    double T=c.duration;
    /* Reversal requested midway: finish old segment without derivative jumps. */
    while(c.elapsed<T) { assert(SetpointCurve_Step(&c,0,.001f,&out)); assert(c.target==10); }
    assert(out==10);
    assert(SetpointCurve_Step(&c,0,.001f,&out) && c.start==10 && c.target==0);
    /* A stop/restart discards old profile, starting from measured airspeed. */
    assert(SetpointCurve_Init(&c,3,5,2,2) && c.value==3 && c.duration==0);
    assert(!SetpointCurve_Init(&c,0,0,2,2));
    assert(!SetpointCurve_Init(&c,NAN,5,2,2));
    assert(!SetpointCurve_Init(&c,0,5,0,2));
    assert(!SetpointCurve_Init(&c,0,5,2,-1));
    assert(SetpointCurve_Init(&c,0,5,2,2));
    assert(!SetpointCurve_Step(&c,NAN,.001f,&out));
    assert(!SetpointCurve_Step(&c,10,0,&out));
    c.max_third_derivative=NAN;
    assert(!SetpointCurve_Step(&c,10,.005f,&out));
    c.max_third_derivative=-1;
    assert(!SetpointCurve_Step(&c,10,.005f,&out));
    /* Frequency units: Hz, Hz/s, Hz/s2 and Hz/s3. Independent analytic derivatives. */
    assert(SetpointCurve_Init(&c,0,.005f,2,.5f));
    c.max_third_derivative=.5;
    double last_hz=0;
    do {
        assert(SetpointCurve_Step(&c,50,.005f,&out));
        double q=c.elapsed/c.duration, frequency_duration=c.duration;
        double slew=50*30*q*q*(1-q)*(1-q)/frequency_duration;
        double accel=50*(60*q-180*q*q+120*q*q*q)/(frequency_duration*frequency_duration);
        double jerk=50*(60-360*q+360*q*q)/(frequency_duration*frequency_duration*frequency_duration);
        assert(c.duration>=46.875 && fabs(slew)<=2.000001 && fabs(accel)<=.500001 && fabs(jerk)<=.500001);
        assert(fabs(c.value-last_hz)<=.010001);
        last_hz=c.value;
    } while(c.elapsed<c.duration);
    assert(out==50);
    puts("PASS frequency quintic 0..50Hz endpoints, 2Hz/s peak, acceleration, jerk and continuity");
    puts("PASS: quintic profile endpoints, monotonicity, duration, acceleration/jerk limits, queued retarget, reset, invalid input");
    return 0;
}
