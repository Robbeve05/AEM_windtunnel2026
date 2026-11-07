#include "Regelaar.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(float actual, float expected)
{
    assert(fabsf(actual-expected) < 0.0001f);
}
int main(void)
{
    Regelaar c;
    RegelaarOutput o;
    RegelaarParameters p = {.kp=2, .ti_s=2, .td_s=0.5f, .tf_s=0.1f,
        .sample_s=0.01f, .mode=REGELAAR_PID};
    for (int mode=REGELAAR_NONE; mode<=REGELAAR_PID; ++mode)
    {
        p.mode=(RegelaarMode)mode;
        assert(Regelaar_Init(&c,&p));
        assert(Regelaar_Step(&c,2,0,&o));
        near(o.value,mode==REGELAAR_NONE ? 0.0f : 4.0f);
        near(o.integral,0); near(o.derivative,0);
        assert(Regelaar_Step(&c,3,1,&o));
        near(o.integral,mode>=REGELAAR_PI ? 0.02f : 0);
        near(o.derivative,mode==REGELAAR_PID ? -2.0f/0.21f : 0);
        const float before=c.integral_before_step;
        if(mode>=REGELAAR_PI) {
            assert(Regelaar_RejectLatestIntegration(&c,&o));
            near(c.integral,before);
            near(o.value,o.proportional+o.integral+o.derivative);
            assert(!Regelaar_RejectLatestIntegration(&c,&o));
        }
        Regelaar snapshot=c;
        assert(!Regelaar_Step(&c,NAN,0,&o));
        assert(!Regelaar_Step(&c,0,INFINITY,&o));
        assert(memcmp(&snapshot,&c,sizeof(c))==0);
        Regelaar_Reset(&c);assert(!c.previous_valid);near(c.integral,0);
    }
    p.mode=REGELAAR_PI;p.ti_s=0;assert(!Regelaar_Init(&c,&p));
    p.mode=REGELAAR_P;assert(Regelaar_Init(&c,&p));
    p.mode=REGELAAR_PID;p.ti_s=2;p.tf_s=0;assert(!Regelaar_Init(&c,&p));
    p.tf_s=-1;assert(!Regelaar_Init(&c,&p));
    p.tf_s=0.1f;p.kp=INFINITY;assert(!Regelaar_Init(&c,&p));
    assert(Regelaar_FromLegacy(2,1,0,0.1f,0.01f,REGELAAR_PI,&p));
    near(p.ti_s,2);near(p.td_s,0);
    assert(Regelaar_Init(&c,&p));
    assert(Regelaar_Step(&c,0,0,&o));near(o.value,0);
    /* Frozen legacy torque PI vector with the original 5 ms test period. */
    p.sample_s=0.005f;assert(Regelaar_Init(&c,&p));
    assert(Regelaar_Step(&c,20,20,&o));near(o.value,0);
    assert(Regelaar_Step(&c,22,20,&o));near(o.value,4.005f);
    p.sample_s=0.01f;
    assert(Regelaar_Init(&c,&p));
    assert(Regelaar_Step(&c,2,0,&o));near(o.value,4);
    assert(Regelaar_Step(&c,4,0,&o));near(o.value,8.03f);
    assert(Regelaar_Step(&c,0,0,&o));near(o.value,0.05f);
    assert(Regelaar_SetParameters(&c,&p));assert(!c.previous_valid);
    assert(!Regelaar_FromLegacy(0,1,0,0,0.01f,REGELAAR_PI,&p));
    assert(Regelaar_FromLegacy(2,0,0,0,0.01f,REGELAAR_PI,&p));assert(p.mode==REGELAAR_P);
    /* No setpoint derivative kick, then the exact filtered decay. */
    p=(RegelaarParameters){.kp=2,.ti_s=2,.td_s=0.5f,.tf_s=0.1f,.sample_s=0.01f,.mode=REGELAAR_PID};
    assert(Regelaar_Init(&c,&p));assert(Regelaar_Step(&c,0,0,&o));
    assert(Regelaar_Step(&c,10,0,&o));near(o.derivative,0);
    assert(Regelaar_Step(&c,10,1,&o));near(o.derivative,-2/0.21f);
    assert(Regelaar_Step(&c,10,1,&o));near(o.derivative,-2/0.21f*0.19f/0.21f);
    puts("PASS ISA NONE/P/PI/PID, known Tustin vectors, first sample, legacy conversion, reset, invalid/NaN/Inf, derivative, single rejection");
    return 0;
}
