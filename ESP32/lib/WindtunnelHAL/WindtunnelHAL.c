#include "WindtunnelHAL.h"
#include "AppConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <math.h>

static portMUX_TYPE filter_lock = portMUX_INITIALIZER_UNLOCKED;
static float filter_hz=PITOT_FILTER_HZ;
void SensorLib_SetFilter(float hz) {
    if (!isfinite(hz) || hz<0.1f || hz>10.0f) return;
    portENTER_CRITICAL(&filter_lock); filter_hz=hz; portEXIT_CRITICAL(&filter_lock);
}
float SensorLib_FilterHz(void) {
    portENTER_CRITICAL(&filter_lock); float hz=filter_hz; portEXIT_CRITICAL(&filter_lock);
    return hz;
}
#if SIMULATION_MODE
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static float drive, velocity;
void Simulation_SetOutput(float value)
{
    portENTER_CRITICAL(&lock);
    drive = value;
    portEXIT_CRITICAL(&lock);
}
bool SensorLib_Init(void) { velocity = 0; return true; }
bool SensorLib_Read(float *value)
{
    portENTER_CRITICAL(&lock);
    float target = drive * 0.30f;
    portEXIT_CRITICAL(&lock);
    velocity += (target-velocity) * ((float)SENSOR_PERIOD_MS/1000.0f)/0.5f;
    *value = velocity;
    return true;
}
bool MotorLib_Init(void) { Simulation_SetOutput(0); return true; }
bool MotorLib_Write(bool run, float value)
{
    if (!isfinite(value) || value < 0 || value > MAX_OUTPUT) return false;
    Simulation_SetOutput(run ? value : 0.0f);
    return true;
}
void SensorLib_Details(Sample *s) { s->pressure_pa=0.5f*AIR_DENSITY*s->value*s->value; s->temperature_c=20; }
void MotorLib_Details(MotorStatus *s) { s->actual_hz=drive*0.5f; s->status=0x0201; }
#else
/* Motor driver: V20Transport.c. */
void Simulation_SetOutput(float value) { (void)value; }
#endif
