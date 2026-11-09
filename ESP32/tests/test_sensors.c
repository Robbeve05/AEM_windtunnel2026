/* Executes production phased sensor code with deterministic bus replies. */
#include "AppConfig.h"
#include "PressureSensors.h"
#include "I2CBus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static int64_t now;
static unsigned operations;
static bool fail;
static unsigned pitot_count = 8192;
static unsigned status_bits;
int64_t esp_timer_get_time(void) { return now; }
TickType_t xTaskGetTickCount(void) { return (TickType_t)(now/1000); }
bool I2CBus_TryRead(uint8_t a,uint8_t *p,size_t n) {
    operations++; assert(n==4);
    if(fail) return false;
    if(a==PITOT_ADDRESS) { const uint8_t raw[]={(uint8_t)((pitot_count >> 8) | (status_bits << 6)),(uint8_t)pitot_count,0x80,0}; memcpy(p,raw,4); }
    else { assert(a==ABP2_ADDRESS); const uint8_t raw[]={0x40,0x80,0,0};memcpy(p,raw,4); }
    return true;
}
bool I2CBus_TryWrite(uint8_t a,const uint8_t *p,size_t n) {
    (void)p;operations++;
    assert((a==MUX_0_ADDRESS || a==MUX_1_ADDRESS) ? n==1 : a==ABP2_ADDRESS && n==3);
    return !fail;
}
bool I2CBus_TryReadOnce(uint8_t a,uint8_t *p,size_t n) {
    assert(a==PITOT_ADDRESS);int64_t start=now;
    bool ok=I2CBus_TryRead(a,p,n);now=start+120;return ok;
}
int main(void) {
    assert(SensorLib_Init()); PitotSample s={0}; PitotDiagnostics d;
    for(unsigned n=0;n<200;n++) {
        now+=10000;unsigned before=operations;
        assert(SensorLib_PitotStep(&s) && operations==before+1 && !s.valid);
        SensorLib_GetPitotDiagnostics(&d);assert(d.pitot_zero_samples==n+1);
    }
    assert(d.pitot_calibrated && s.sequence==200);
    now+=10000;assert(SensorLib_PitotStep(&s) && s.valid && s.value==0 && s.acquisition_us==120);
    pitot_count=8292;
    for(unsigned n=0;n<5;n++) {now+=10000;assert(SensorLib_PitotStep(&s) && s.valid);}
    SensorLib_GetPitotDiagnostics(&d);
    assert(d.pitot_corrected_pressure_pa>100 && d.pitot_filtered_pressure_pa>0 && d.pitot_filtered_pressure_pa<d.pitot_corrected_pressure_pa);
    for(unsigned n=0;n<100;n++) {now+=10000;SensorLib_PitotStep(&s);}
    SensorLib_GetPitotDiagnostics(&d);assert(fabsf(d.pitot_filtered_pressure_pa-d.pitot_corrected_pressure_pa)<1);
    float previous=d.pitot_filtered_pressure_pa;
    now+=10000;SensorLib_PitotStep(&s);SensorLib_GetPitotDiagnostics(&d);
    const float expected=previous+(1-expf(-6.283185307f*0.01012f))*(d.pitot_corrected_pressure_pa-previous);
    assert(fabsf(expected-d.pitot_filtered_pressure_pa)<0.01f);
    pitot_count=8092;
    for(unsigned n=0;n<150;n++) {now+=10000;SensorLib_PitotStep(&s);}
    SensorLib_GetPitotDiagnostics(&d);assert(d.pitot_pressure_pa<0 && s.value>0);
    for(unsigned status=1;status<=3;status++) {
        status_bits=status;now+=10000;uint32_t seq=s.sequence;
        assert(SensorLib_PitotStep(&s) && !s.valid && s.sequence==seq+1);
        SensorLib_GetPitotDiagnostics(&d);assert(d.pitot_status_bits==status && !d.pitot_valid);
    }
    status_bits=0;fail=true;now+=10000;assert(SensorLib_PitotStep(&s) && !s.valid);
    SensorLib_GetPitotDiagnostics(&d);assert(d.pitot_status_bits==255 && d.pitot_zero_samples==200);
    fail=false;now+=10000;assert(SensorLib_PitotStep(&s) && s.valid);
    PressureTapSample taps={0};
    for(unsigned n=0;n<112;n++) {
        now+=10000;unsigned before=operations; (void)SensorLib_TapsStep(&taps); assert(operations<=before+1);
    }
    assert(taps.taps_valid==0xffff && taps.sequence==16 && isfinite(taps.taps[15]));
    assert(taps.updated_mask==(1u<<15));
    assert(SensorLib_TapsValidMask(&taps,xTaskGetTickCount())==0xffff);
    assert(SensorLib_TapsValidMask(&taps,xTaskGetTickCount()+TAP_TIMEOUT_MS)==0);
    taps.taps_valid=1;taps.tap_ticks[0]=UINT32_MAX-5;
    assert(SensorLib_TapsValidMask(&taps,10)==1);
    assert(SensorLib_TapsValidMask(NULL,10)==0);
    puts("PASS 100Hz immediate Pitot, 200 valid zero reads, rolling five, 1Hz, both signs, statuses 1/2/3, failed read and recovery, independent taps");
    return 0;
}
