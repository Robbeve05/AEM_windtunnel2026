/* Author: R.E. van Eekelen.
 * Pitot ASPD-4525 is read only by ControlTask (Core 1).
 * One bounded I2C transaction every 5 ms, 10-point rolling average.
 * Pressure tap acquisition is separately owned by SensorTask (Core 0).
 * Hardware I2C latency is NOT hard real-time: verify overruns on target.
 */
#include "AppConfig.h"
#include "PressureSensors.h"
#include "I2CBus.h"
#include "esp_timer.h"
#include "freertos/portmacro.h"
#include <math.h>
#include <string.h>

static portMUX_TYPE config_lock = portMUX_INITIALIZER_UNLOCKED;
static float filter_hz = PITOT_FILTER_HZ;
static uint32_t error_count, sequence_count;
static float zero_pressure, filtered_pressure, pressure_pa, temperature_c;
static double zero_sum;
static uint32_t zero_samples, samples_in_window;
static float window_pressure, window_temperature;
static int64_t previous_us;
static bool sample_valid;
#if SIMULATION_MODE
static float sim_drive, sim_velocity;
#endif

void SensorLib_SetFilter(float hz)
{
    if (!isfinite(hz) || hz < 0.1f || hz > 10.0f) return;
    portENTER_CRITICAL(&config_lock);
    filter_hz = hz;
    portEXIT_CRITICAL(&config_lock);
}
float SensorLib_FilterHz(void)
{
    portENTER_CRITICAL(&config_lock);
    const float value = filter_hz;
    portEXIT_CRITICAL(&config_lock);
    return value;
}
bool SensorLib_Init(void)
{
    error_count = sequence_count = zero_samples = samples_in_window = 0;
    window_pressure = window_temperature = 0.0f;
    zero_sum = 0;
    zero_pressure = filtered_pressure = pressure_pa = temperature_c = 0.0f;
    previous_us = 0;
    sample_valid = false;
#if SIMULATION_MODE
    sim_velocity = 0.0f;
#endif
    return true;
}

void Simulation_SetOutput(float value)
{
#if SIMULATION_MODE
    portENTER_CRITICAL(&config_lock);
    sim_drive = isfinite(value) ? value : 0.0f;
    portEXIT_CRITICAL(&config_lock);
#else
    (void)value;
#endif
}

/* Core 1 only. Return true whenever a new valid 10-sample average exists.
 * Even during calibration, publish a fresh invalid sample to the queue. */
bool SensorLib_PitotStep(Sample *out)
{
    if (out == NULL) return false;
    float p = 0.0f, temp = 0.0f;
    bool valid_read = false;
#if SIMULATION_MODE
    portENTER_CRITICAL(&config_lock);
    const float drive = sim_drive;
    portEXIT_CRITICAL(&config_lock);
    sim_velocity += (drive * 0.30f - sim_velocity) * CONTROL_CLOCK_PERIOD_S / 0.5f;
    p = 0.5f * AIR_DENSITY * sim_velocity * sim_velocity;
    temp = 20.0f;
    valid_read = true;
#else
    uint8_t raw[4];
    if (I2CBus_TryRead(PITOT_ADDRESS, raw, sizeof(raw)) && !(raw[0] >> 6)) {
        const uint32_t count = (((uint32_t)raw[0] & 0x3fu) << 8) | raw[1];
        if (count >= 1638u && count <= 14746u) {
            p = ((float)count - 0.1f * 16383.0f) *
                (PITOT_MAX_PA - PITOT_MIN_PA) / (0.8f * 16383.0f) + PITOT_MIN_PA;
            temp = 200.0f * ((raw[2] << 3) | (raw[3] >> 5)) / 2047.0f - 50.0f;
            valid_read = isfinite(p) && isfinite(temp);
        }
    }
#endif
    if (!valid_read) {
        error_count++;
        samples_in_window = 0;
        window_pressure = window_temperature = 0.0f;
        previous_us = 0;
        sample_valid = false;
    } else {
        window_pressure += p;
        window_temperature += temp;
        samples_in_window++;
    }
    if (valid_read && samples_in_window < PITOT_AVERAGE_SAMPLES)
        return false;

    /* One published sample per 10 control ticks (20 Hz for a 200 Hz loop). */
    if (valid_read) {
        p = window_pressure / PITOT_AVERAGE_SAMPLES;
        temperature_c = window_temperature / PITOT_AVERAGE_SAMPLES;
        window_pressure = window_temperature = 0.0f;
        samples_in_window = 0;
#if !SIMULATION_MODE
        if (zero_samples < PITOT_ZERO_SAMPLES) {
            zero_sum += p * PITOT_AVERAGE_SAMPLES;
            zero_samples += PITOT_AVERAGE_SAMPLES;
            zero_pressure = (float)(zero_sum / zero_samples);
            sample_valid = false;
        } else {
            p -= zero_pressure;
            int64_t now_us = esp_timer_get_time();
            if (!previous_us) filtered_pressure = p;
            else {
                const float dt = (float)(now_us - previous_us) / 1000000.0f;
                const float coefficient = 1.0f - expf(-6.283185307f * SensorLib_FilterHz() * dt);
                filtered_pressure += coefficient * (p - filtered_pressure);
            }
            previous_us = now_us;
            pressure_pa = fabsf(filtered_pressure) < PITOT_DEADBAND_PA ? 0.0f : filtered_pressure;
            sample_valid = true;
        }
#else
        pressure_pa = p;
        sample_valid = true;
#endif
    }
    *out = (Sample){0};
    out->pressure_pa = pressure_pa;
    out->temperature_c = temperature_c;
#if SIMULATION_MODE
    out->value = sim_velocity;
#else
    out->value = sqrtf(2.0f * fabsf(pressure_pa) / AIR_DENSITY);
#endif
    out->errors = error_count;
    out->sequence = ++sequence_count;
    out->tick = xTaskGetTickCount();
    out->valid = sample_valid && isfinite(out->value);
    return true;
}

/* Core 0 only: pressure-tap I2C never executes in ControlTask. */
void SensorLib_ReadTaps(Sample *out)
{
    if (!out) return;
    out->taps_valid = 0;
    for (int i = 0; i < 16; ++i) out->taps[i] = NAN;
#if PRESSURE_TAPS_ENABLED && !SIMULATION_MODE
    static const uint8_t trigger[] = {0xaa, 0, 0};
    uint16_t started = 0;
    const uint8_t off = 0;
    for (int i = 0; i < 16; ++i) {
        const uint8_t channel = (uint8_t)(1u << (i % 8));
        if (!I2CBus_Write(MUX_0_ADDRESS, &off, 1) ||
            !I2CBus_Write(MUX_1_ADDRESS, &off, 1) ||
            !I2CBus_Write(i < 8 ? MUX_0_ADDRESS : MUX_1_ADDRESS, &channel, 1) ||
            !I2CBus_Write(ABP2_ADDRESS, trigger, sizeof(trigger))) {
            out->errors++;
            continue;
        }
        started |= (uint16_t)(1u << i);
    }
    (void)I2CBus_Write(MUX_0_ADDRESS, &off, 1);
    (void)I2CBus_Write(MUX_1_ADDRESS, &off, 1);
    vTaskDelay(pdMS_TO_TICKS(5) + 1);
    for (int i = 0; i < 16; ++i) {
        if (!(started & (1u << i))) continue;
        const uint8_t channel = (uint8_t)(1u << (i % 8));
        uint8_t bytes[4];
        if (!I2CBus_Write(MUX_0_ADDRESS, &off, 1) ||
            !I2CBus_Write(MUX_1_ADDRESS, &off, 1) ||
            !I2CBus_Write(i < 8 ? MUX_0_ADDRESS : MUX_1_ADDRESS, &channel, 1) ||
            !I2CBus_Read(ABP2_ADDRESS, bytes, sizeof(bytes)) ||
            (bytes[0] & 0x25u) || !(bytes[0] & 0x40u)) {
            out->errors++; continue;
        }
        const uint32_t raw = ((uint32_t)bytes[1] << 16) |
            ((uint32_t)bytes[2] << 8) | bytes[3];
        if (raw < ABP2_OUTPUT_MIN || raw > ABP2_OUTPUT_MAX) {
            out->errors++; continue;
        }
        out->taps[i] = ((float)raw - ABP2_OUTPUT_MIN) *
            (ABP2_MAX_PA - ABP2_MIN_PA) /
            (ABP2_OUTPUT_MAX - ABP2_OUTPUT_MIN) + ABP2_MIN_PA;
        out->taps_valid |= (uint16_t)(1u << i);
    }
    (void)I2CBus_Write(MUX_0_ADDRESS, &off, 1);
    (void)I2CBus_Write(MUX_1_ADDRESS, &off, 1);
#endif
    out->tick = xTaskGetTickCount();
}
