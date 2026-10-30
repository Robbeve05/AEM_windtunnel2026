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
int64_t esp_timer_get_time(void) { return now; }
TickType_t xTaskGetTickCount(void) { return (TickType_t)(now/1000); }
bool I2CBus_TryRead(uint8_t a,uint8_t *p,size_t n) {
    operations++; assert(n==4);
    if(fail) return false;
    if(a==PITOT_ADDRESS) { const uint8_t raw[]={0x20,0,0x80,0}; memcpy(p,raw,4); }
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
        now+=5000;unsigned before=operations;
        bool published=SensorLib_PitotStep(&s);
        assert(operations==before+1);
        assert(published==((n+1)%10==0));
        if(published) assert(s.valid==(n>=200));
    }
    assert(s.valid && isfinite(s.value) && s.sequence==21 && s.tick==1050);
    TickType_t valid_time=s.tick;
    fail=true;
    for(unsigned n=0;n<40;n++) {now+=5000;assert(!SensorLib_PitotStep(&s));}
    assert(s.tick==valid_time && xTaskGetTickCount()-s.tick>SENSOR_TIMEOUT_MS);
    fail=false;Sample taps={0};
    for(unsigned n=0;n<112;n++) {
        now+=5000;unsigned before=operations;
        (void)SensorLib_TapsStep(&taps);
        assert(operations<=before+1);
    }
    assert(taps.taps_valid==0xffff && taps.sequence==16);
    assert(isfinite(taps.taps[15]));
    puts("PASS 5ms pitot slots, 10-point average, 200-sample calibration, timestamps, sensor timeout, incremental taps, one tap transaction per slot");
    return 0;
}
