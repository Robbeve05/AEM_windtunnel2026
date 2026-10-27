#include "AppConfig.h"
#include "PressureSensors.h"
#include "I2CBus.h"
#if !SIMULATION_MODE
#include "driver/i2c.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

/* SensorTask owns mux selection; individual bus transactions share a mutex. */
static Sample latest;
static float zero, filtered;
static unsigned zero_count;
static double zero_sum;
static int64_t previous;
static bool read_bytes(uint8_t address, uint8_t *data, size_t count)
{
    return I2CBus_Read(address,data,count);
}
#if PRESSURE_TAPS_ENABLED
static bool select_tap(int index)
{
    /* Both multiplexers must be off before selecting exactly one channel. */
    uint8_t off=0;
    bool a=I2CBus_Write(MUX_0_ADDRESS,&off,1);
    bool b=I2CBus_Write(MUX_1_ADDRESS,&off,1);
    if (!a || !b || index<0) return a && b;
    uint8_t channel=1u << (index%8);
    return I2CBus_Write(index<8 ? MUX_0_ADDRESS : MUX_1_ADDRESS,&channel,1);
}
static void read_taps(void)
{
    static const uint8_t trigger[]={0xaa,0,0};
    uint16_t started=0;
    latest.taps_valid=0;
    for (int i=0;i<16;i++) {
        latest.taps[i]=NAN;
        if (select_tap(i) && I2CBus_Write(ABP2_ADDRESS,trigger,3)) started |= 1u<<i;
        else latest.errors++;
    }
    select_tap(-1);
    vTaskDelay(pdMS_TO_TICKS(5)+1);
    for (int i=0;i<16;i++) {
        uint8_t data[4];
        if (!(started & (1u<<i))) continue;
        if (!select_tap(i) || !read_bytes(ABP2_ADDRESS,data,4) ||
            (data[0]&0x25) || !(data[0]&0x40)) { latest.errors++; continue; }
        uint32_t raw=((uint32_t)data[1]<<16)|((uint32_t)data[2]<<8)|data[3];
        if (raw<ABP2_OUTPUT_MIN || raw>ABP2_OUTPUT_MAX) { latest.errors++; continue; }
        latest.taps[i]=(raw-ABP2_OUTPUT_MIN)*(ABP2_MAX_PA-ABP2_MIN_PA)/
            (ABP2_OUTPUT_MAX-ABP2_OUTPUT_MIN)+ABP2_MIN_PA;
        latest.taps_valid |= 1u<<i;
    }
    if (!select_tap(-1)) { latest.taps_valid=0; latest.errors++; }
}
#endif
bool SensorLib_Init(void)
{
    memset(&latest,0,sizeof(latest));
    return true;
}
static bool pitot(float *pressure, float *temperature)
{
    uint8_t data[4];
    if (!read_bytes(PITOT_ADDRESS,data,4) || data[0]>>6) return false;
    unsigned raw=((data[0]&0x3f)<<8)|data[1];
    if (raw<1638 || raw>14746) return false;
    *pressure=(raw-0.1f*16383)*(PITOT_MAX_PA-PITOT_MIN_PA)/(0.8f*16383)+PITOT_MIN_PA;
    *temperature=200.0f*((data[2]<<3)|(data[3]>>5))/2047.0f-50;
    return true;
}
bool SensorLib_Read(float *velocity)
{
    *velocity=0;
#if PRESSURE_TAPS_ENABLED
    if (!select_tap(-1)) { latest.errors++; return false; }
#endif
    /* Preserve the Uno's 10-reading average and pressure low-pass filter. */
    float sum=0, temps=0; unsigned count=0;
    for (unsigned i=0;i<10;i++) {
        float p,t;
        if (pitot(&p,&t)) { sum+=p; temps+=t; count++; }
        else latest.errors++;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (count!=10) { previous=0; return false; }
    float pressure=sum/count;
    latest.temperature_c=temps/count;
    /* Start inhibited until 200 valid readings; keep pitot in still air at boot. */
    if (zero_count<PITOT_ZERO_SAMPLES) {
        zero_sum+=sum; zero_count+=count;
        zero=(float)(zero_sum/zero_count);
        return false;
    }
    pressure-=zero;
    int64_t now=esp_timer_get_time();
    if (!previous) filtered=pressure;
    else filtered+=(1-expf(-6.283185307f*SensorLib_FilterHz()*(now-previous)/1000000.0f))*(pressure-filtered);
    previous=now;
    latest.pressure_pa=fabsf(filtered)<PITOT_DEADBAND_PA ? 0 : filtered;
    *velocity=sqrtf(2*fabsf(latest.pressure_pa)/AIR_DENSITY);
#if PRESSURE_TAPS_ENABLED
    read_taps();
#endif
    return isfinite(*velocity);
}
void SensorLib_Details(Sample *s)
{
    s->pressure_pa=latest.pressure_pa; s->temperature_c=latest.temperature_c;
    s->errors=latest.errors; s->taps_valid=latest.taps_valid;
    memcpy(s->taps,latest.taps,sizeof(s->taps));
}
#endif

#include "freertos/portmacro.h"
#include <math.h>
static portMUX_TYPE filter_lock=portMUX_INITIALIZER_UNLOCKED;
static float filter_hz=PITOT_FILTER_HZ;
void SensorLib_SetFilter(float hz) {
    if(!isfinite(hz)||hz<0.1f||hz>10) return;
    portENTER_CRITICAL(&filter_lock); filter_hz=hz; portEXIT_CRITICAL(&filter_lock);
}
float SensorLib_FilterHz(void) {
    portENTER_CRITICAL(&filter_lock); float hz=filter_hz; portEXIT_CRITICAL(&filter_lock); return hz;
}
#if SIMULATION_MODE
static float drive,velocity;
void Simulation_SetOutput(float value) {
    portENTER_CRITICAL(&filter_lock); drive=value; portEXIT_CRITICAL(&filter_lock);
}
bool SensorLib_Init(void) { velocity=0; return true; }
bool SensorLib_Read(float *value) {
    portENTER_CRITICAL(&filter_lock); float target=drive*0.30f; portEXIT_CRITICAL(&filter_lock);
    velocity+=(target-velocity)*(SENSOR_PERIOD_MS/1000.0f)/0.5f; *value=velocity; return true;
}
void SensorLib_Details(Sample *s) { s->pressure_pa=0.5f*AIR_DENSITY*s->value*s->value; s->temperature_c=20; }
#else
void Simulation_SetOutput(float value) { (void)value; }
#endif
