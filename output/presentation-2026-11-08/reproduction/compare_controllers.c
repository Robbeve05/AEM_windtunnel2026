/* Offline presentation experiment. Links real controller sources; no UART,
 * RTOS tasks, safety permissions, NVS writes or hardware actuation. */
#include "AppConfig.h"
#include "Regelaar.h"
#include "TorqueModel.h"
#include "FrequencyModel.h"
#include "SetpointCurve.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

bool FanCalibration_IsValid(void) { return false; }
float FanCalibration_GetBaseFrequency(float reference, float fallback)
{ (void)reference; return fallback; }

typedef struct {
    Regelaar controller;
    float omega, actual_hz, pressure_sum, filtered_pressure, measured;
    unsigned samples;
} Experiment;

int main(void)
{
    const float pi=3.14159265358979323846f, dt=CONTROL_CLOCK_PERIOD_S;
    const TorqueDesignInput input={J_MOTOR_KGM2,J_FAN_KGM2,DISTURBANCE_FREQUENCY_HZ,
        DISTURBANCE_AMPLITUDE_MS,ALLOWABLE_ERROR_MS,DESIGN_NOMINAL_RPM,LUCHTSNELHEID_NOM,
        CONTROL_CLOCK_PERIOD_S,DESIGN_MIN_PHASE_MARGIN_DEG};
    TorqueDesignOutput design; assert(TorqueModel_Design(&input,&design));
    const TorqueModel model={design.j,MOTOR_MODEL_SLIP_GAIN_NM_PER_RAD_S,
        MOTOR_MODEL_FAN_LOAD_NM_PER_RAD_S2,MOTOR_MODEL_POLE_PAIRS,design.kv,MANUAL_MAX_HZ};
    /* Model-only local identification at the common 20 m/s final point.
     * These derived K/tau values are NOT the zero-valued deployed model config. */
    const float w_design=20.0f/model.nominal_air_omega_gain;
    const float damping=model.slip_gain_nm_per_rad_s+2*model.load_coeff_nm_per_rad_s2*w_design;
    const FrequencyModel frequency={
        .gain_mps_per_hz=model.nominal_air_omega_gain*model.slip_gain_nm_per_rad_s*
                        2*pi/model.pole_pairs/damping,
        .tau_s=model.inertia_kgm2/damping,.delay_s=0,
        .lambda_s=FREQUENCY_MODEL_LAMBDA_S,.max_hz=50,.identified_valid=true,.commissioned=false};
    RegelaarParameters fp; assert(FrequencyModel_Design(&frequency,dt,&fp));
    const RegelaarParameters tp={.kp=design.kp,.ti_s=design.ti,.td_s=design.td,
        .tf_s=design.tf,.sample_s=dt,.mode=(RegelaarMode)CONTROLLER_SETTING};
    Experiment a={0},b={0}; assert(Regelaar_Init(&a.controller,&fp));
    assert(Regelaar_Init(&b.controller,&tp));
    SetpointCurve curve; assert(SetpointCurve_Init(&curve,0,SETPOINT_TRANSITION_TIME_S,
        SETPOINT_MAX_ACCELERATION_MPS2,SETPOINT_MAX_JERK_MPS3));
    fprintf(stderr,"{\"dt_s\":%.9g,\"J\":%.9g,\"Ks\":%.9g,\"b\":%.9g,\"Kv\":%.9g,"
        "\"frequency_K\":%.9g,\"frequency_tau\":%.9g,\"frequency_lambda\":%.9g,"
        "\"frequency_Kp\":%.9g,\"frequency_Ti\":%.9g,\"torque_Kp\":%.9g,"
        "\"torque_Ti\":%.9g,\"torque_Td\":%.9g,\"torque_Tf\":%.9g,"
        "\"torque_mode\":%d,\"filter_Hz\":%.9g,\"ramp_Hz_s\":2}\n",
        dt,model.inertia_kgm2,model.slip_gain_nm_per_rad_s,model.load_coeff_nm_per_rad_s2,
        model.nominal_air_omega_gain,frequency.gain_mps_per_hz,frequency.tau_s,
        frequency.lambda_s,fp.kp,fp.ti_s,tp.kp,tp.ti_s,tp.td_s,tp.tf_s,(int)tp.mode,PITOT_FILTER_HZ);
    puts("time_s,target_ms,reference_ms,disturbance_ms,frequency_speed_ms,torque_speed_ms,frequency_command_hz,torque_command_hz,frequency_actual_hz,torque_actual_hz");
    float previous_ref=0;
    for(unsigned n=0;n<=12000;n++) {
        float t=n*dt, ref=0, target=t>=5?20.0f:0;
        if(t>=5) assert(SetpointCurve_Step(&curve,target,dt,&ref));
        float accel=(ref-previous_ref)/dt;
        const float disturbance=t>=45?DISTURBANCE_AMPLITUDE_MS*sinf(2*pi*DISTURBANCE_FREQUENCY_HZ*(t-45)):0;
        const float wr=ref/model.nominal_air_omega_gain;
        /* Shared synthetic stationary characteristic derived from the same plant.
         * No measured/NVS calibration is claimed for either method. */
        const float base=model.pole_pairs/(2*pi)*(wr+model.load_coeff_nm_per_rad_s2*wr*wr/model.slip_gain_nm_per_rad_s);
        float commands[2]={0};
        Experiment *experiments[2]={&a,&b};
        for(unsigned method=0;method<2;method++) {
            Experiment *e=experiments[method]; RegelaarOutput correction;
            assert(Regelaar_Step(&e->controller,ref,e->measured,&correction));
            if(method==0) {
                FrequencyModelOutput result;
                assert(FrequencyModel_Command(&frequency,true,base,correction.value,&result));
                if(FrequencyModel_RejectIntegration(&result,50,correction.integral_increment,true,e->actual_hz)) {
                    assert(Regelaar_RejectLatestIntegration(&e->controller,&correction));
                    assert(FrequencyModel_Command(&frequency,true,base,correction.value,&result));
                }
                commands[method]=result.frequency_hz;
            } else {
                TorqueModelResult result;
                assert(TorqueModel_Command(&model,ref,e->measured,accel,correction.value,true,base,&result));
                if(TorqueModel_RejectIntegration(&result,50,correction.integral_increment,true,e->actual_hz)) {
                    assert(Regelaar_RejectLatestIntegration(&e->controller,&correction));
                    assert(TorqueModel_Command(&model,ref,e->measured,accel,correction.value,true,base,&result));
                }
                commands[method]=result.frequency_hz;
            }
            /* 2 Hz/s is the existing offline Python candidate ramp assumption,
             * NOT a readback or measurement of the physical V20 P1120/P1121. */
            e->actual_hz+=fminf(2*dt,fmaxf(-2*dt,commands[method]-e->actual_hz));
            const float torque=model.slip_gain_nm_per_rad_s*(2*pi*e->actual_hz/model.pole_pairs-e->omega);
            const float load=model.load_coeff_nm_per_rad_s2*e->omega*fabsf(e->omega);
            e->omega=fmaxf(0,e->omega+dt*(torque-load)/model.inertia_kgm2);
            const float air=fmaxf(0,model.nominal_air_omega_gain*e->omega+disturbance);
            e->pressure_sum+=0.5f*AIR_DENSITY*air*air; e->samples++;
            if(e->samples==PITOT_AVERAGE_SAMPLES) {
                const float pressure=e->pressure_sum/e->samples;
                e->filtered_pressure+=(1-expf(-2*pi*PITOT_FILTER_HZ*dt*e->samples))*(pressure-e->filtered_pressure);
                e->measured=sqrtf(2*fabsf(e->filtered_pressure)/AIR_DENSITY);
                e->samples=0; e->pressure_sum=0;
            }
            assert(isfinite(e->measured)&&isfinite(commands[method])&&commands[method]>=0&&commands[method]<=50);
        }
        if(n%5==0) printf("%.5f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
            t,target,ref,disturbance,a.measured,b.measured,commands[0],commands[1],a.actual_hz,b.actual_hz);
        previous_ref=ref;
    }
    return 0;
}
