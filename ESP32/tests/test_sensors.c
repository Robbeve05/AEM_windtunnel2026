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
int main(void) {
    assert(SensorLib_Init()); Sample s={0};
    for(unsigned n=0;n<210;n++) {
        now+=2*SENSOR_PHASE_US;unsigned before=operations;
        bool published=SensorLib_PitotStep(&s);
        assert(operations==before+1);
        assert(published==((n+1)%PITOT_AVERAGE_SAMPLES==0));
        if(published) assert(s.valid==(n>=200+PITOT_AVERAGE_SAMPLES-1));
    }
    assert(s.valid && isfinite(s.value) &&
           s.sequence==210/PITOT_AVERAGE_SAMPLES && s.tick==2100);
    TickType_t valid_time=s.tick;
    fail=true;
    for(unsigned n=0;n<40;n++) {now+=2*SENSOR_PHASE_US;assert(!SensorLib_PitotStep(&s));}
    assert(s.tick==valid_time && xTaskGetTickCount()-s.tick>SENSOR_TIMEOUT_MS);
    PitotDiagnostics d;
    SensorLib_GetPitotDiagnostics(&d);
    assert(!d.pitot_read_ok && !d.pitot_valid && d.pitot_status_bits==255);
    fail=false;
    for(unsigned n=0;n<5;n++) { now+=10000; SensorLib_PitotStep(&s); }
    SensorLib_GetPitotDiagnostics(&d);
    assert(d.pitot_read_ok && d.pitot_valid && d.pitot_calibrated);
    assert(d.pitot_zero_samples==200 && d.pitot_raw_count==8192);
    assert(fabsf(d.pitot_corrected_pressure_pa)<0.01f && d.pitot_airspeed_ms==0);
    pitot_count=8292;
    for(unsigned n=0;n<5;n++) { now+=10000; SensorLib_PitotStep(&s); }
    SensorLib_GetPitotDiagnostics(&d);
    assert(d.pitot_corrected_pressure_pa>100 && d.pitot_filtered_pressure_pa>0);
    assert(d.pitot_filtered_pressure_pa<d.pitot_corrected_pressure_pa);
    assert(fabsf(d.pitot_filtered_pressure_pa - d.pitot_corrected_pressure_pa * (1.0f-expf(-6.283185307f*0.05f)))<0.01f);
    float start=d.pitot_filtered_pressure_pa;
    for(unsigned n=0;n<100;n++) { now+=10000; SensorLib_PitotStep(&s); }
    SensorLib_GetPitotDiagnostics(&d);
    assert(d.pitot_filtered_pressure_pa>start && fabsf(d.pitot_filtered_pressure_pa-d.pitot_corrected_pressure_pa)<1);
    pitot_count=8092;
    for(unsigned n=0;n<150;n++) { now+=10000; SensorLib_PitotStep(&s); }
    SensorLib_GetPitotDiagnostics(&d);
    assert(d.pitot_corrected_pressure_pa<0 && d.pitot_pressure_pa<0 && d.pitot_airspeed_ms>0);
    status_bits=2; uint32_t seq=d.pitot_sample_seq;
    now+=10000; assert(!SensorLib_PitotStep(&s));
    SensorLib_GetPitotDiagnostics(&d);
    assert(d.pitot_status_bits==2 && !d.pitot_valid && d.pitot_sample_seq==seq);
    status_bits=0;
    fail=false;Sample taps={0};
    for(unsigned n=0;n<112;n++) {
        now+=2*SENSOR_PHASE_US;unsigned before=operations;
        (void)SensorLib_TapsStep(&taps);
        assert(operations<=before+1);
    }
    assert(taps.taps_valid==0xffff && taps.sequence==16);
    assert(isfinite(taps.taps[15]));
    puts("PASS 10ms pitot slots, configured average, 200-sample calibration, timestamps, sensor timeout, incremental taps, one tap transaction per slot");
    return 0;
}
