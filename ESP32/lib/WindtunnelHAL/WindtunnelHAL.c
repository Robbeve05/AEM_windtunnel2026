#include "WindtunnelHAL.h"
#include "AppConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <math.h>

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
bool MotorLib_Write(float value)
{
    if (!isfinite(value) || value < 0 || value > MAX_OUTPUT) return false;
    Simulation_SetOutput(value);
    return true;
}
#else
/* TODO: I2C-sensortype, adres, kalibratie en conversie naar m/s invullen.
 * Geen fictieve geldige sensorwaarden bij ontbrekende hardware. */
bool SensorLib_Init(void) { return false; }
bool SensorLib_Read(float *value) { (void)value; return false; }
/* TODO: Modbus RTU RS485-driver met CRC, response-validatie, timeout,
 * drive-enable/stop en registermapping van de gekozen frequentieregelaar.
 * Configureer ook de communicatiewatchdog in de drive zelf. */
bool MotorLib_Init(void) { return false; }
bool MotorLib_Write(float value) { (void)value; return false; }
void Simulation_SetOutput(float value) { (void)value; }
#endif
