#include "TorqueModel.h"
#include "FrequencyModel.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    const TorqueModel m={.inertia_kgm2=0.3f,.slip_gain_nm_per_rad_s=1,
        .load_coeff_nm_per_rad_s2=0.001f,.pole_pairs=2,
        .nominal_air_omega_gain=0.25f,.max_command_hz=50};
    TorqueModelResult t;
    assert(TorqueModel_Command(&m,20,20,1,0,false,0,&t));
    assert(fabsf(t.total_torque_nm-7.6f)<0.0001f); /* legacy diagnostic vector */
    assert(TorqueModel_Command(&m,20,20,1,2,false,0,&t));
    assert(fabsf(t.feedforward_torque_nm-1.2f)<0.0001f);
    assert(fabsf(t.load_torque_nm-6.4f)<0.0001f);
    assert(fabsf(t.total_torque_nm-9.6f)<0.0001f);
    assert(fabsf(t.frequency_hz-(80+9.6f)/3.14159265359f)<0.0001f);
    assert(TorqueModel_Command(&m,20,20,1,2,true,20,&t));
    assert(fabsf(t.frequency_hz-(20+3.2f/3.14159265359f))<0.0001f);
    assert(TorqueModel_Command(&m,20,20,1,1000,true,20,&t));
    assert(t.frequency_hz==50 && t.saturated);
    assert(TorqueModel_RejectIntegration(&t,50,1,false,0));
    assert(!TorqueModel_RejectIntegration(&t,50,-1,false,0));
    FrequencyModel f={.gain_mps_per_hz=0.8f,.tau_s=2,.delay_s=0.2f,
        .lambda_s=0.8f,.max_hz=50,.identified_valid=true};
    RegelaarParameters p;
    assert(FrequencyModel_Design(&f,0.01f,&p));
    assert(fabsf(p.kp-2.5f)<0.0001f && p.ti_s==2 && p.mode==REGELAAR_PI);
    f.identified_valid=false;assert(!FrequencyModel_Design(&f,0.01f,&p));
    FrequencyModelOutput o;
    assert(!FrequencyModel_Command(&f,false,20,0,&o));
    assert(!FrequencyModel_Command(&f,true,NAN,0,&o));
    assert(!FrequencyModel_Command(&f,true,51,0,&o));
    p=(RegelaarParameters){.kp=2,.ti_s=1,.sample_s=0.01f,.mode=REGELAAR_PI};
    Regelaar c;RegelaarOutput r;
    assert(Regelaar_Init(&c,&p));
    for(int n=0;n<10000;n++) {
        assert(Regelaar_Step(&c,10,0,&r));
        assert(FrequencyModel_Command(&f,true,40,r.value,&o));
        if(FrequencyModel_RejectIntegration(&o,50,r.integral_increment,false,0)) {
            assert(Regelaar_RejectLatestIntegration(&c,&r));
            assert(FrequencyModel_Command(&f,true,40,r.value,&o));
        }
        /* Regression: subtracting from the clamped 50 would give 49.8. */
        assert(o.frequency_hz==50);assert(c.integral==0);
    }
    assert(Regelaar_Step(&c,0,1,&r));
    assert(FrequencyModel_Command(&f,true,40,r.value,&o));
    assert(o.frequency_hz<40); /* immediate recovery after prolonged saturation */
    assert(FrequencyModel_Command(&f,true,20,1,&o));
    assert(!FrequencyModel_RejectIntegration(&o,50,1,false,0));
    assert(FrequencyModel_RejectIntegration(&o,50,1,true,0));
    assert(!FrequencyModel_RejectIntegration(&o,50,-1,true,0));
    assert(FrequencyModel_Command(&f,true,0,-100,&o));assert(o.frequency_hz==0);
    puts("PASS single torque acceleration/load/correction, calibrated replacement, SIMC, invalid calibration, fresh tracking, saturation recomputation and 10000-cycle recovery");
    return 0;
}
