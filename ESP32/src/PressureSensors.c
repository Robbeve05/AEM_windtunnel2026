///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * PressureSensors.c
 *
 * Pitot is owned by ControlTask (Core 1); taps by SensorTask (Core 0).
 * One bounded Pitot transaction every 10 ms, 5-point rolling average.
 * Pressure tap acquisition is separately owned by SensorTask (Core 0).
 * Hardware I2C latency is NOT hard real-time: verify overruns on target.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_timer.h"
#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "I2CBus.h"
#include "PressureSensors.h"

///////////////////////////////////////////////////////////////////////////////
// File-local variables

static portMUX_TYPE config_lock = portMUX_INITIALIZER_UNLOCKED;
static PitotDiagnostics diagnostics;
static float filter_hz = PITOT_FILTER_HZ;
static uint32_t error_count, sequence_count;
static bool pitot_communication_ok;
static float zero_pressure, filtered_pressure, pressure_pa, temperature_c;
static double zero_sum;
static uint32_t zero_samples, samples_in_window;
static float window_pressure, window_temperature;
static float pressure_window[PITOT_AVERAGE_SAMPLES], temperature_window[PITOT_AVERAGE_SAMPLES];
static unsigned window_index;
static int64_t previous_us;
static bool sample_valid;
#if SIMULATION_MODE
static float sim_drive, sim_velocity;
#endif

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// SensorLib_Init()

bool SensorLib_Init(void)
{
    portENTER_CRITICAL(&config_lock);
    memset(&diagnostics, 0, sizeof(diagnostics));
    diagnostics.pitot_status_bits = 255;
    pitot_communication_ok = false;
    portEXIT_CRITICAL(&config_lock);
    error_count = sequence_count = zero_samples = samples_in_window = 0;
    window_pressure = window_temperature = 0.0f;
    memset(pressure_window, 0, sizeof(pressure_window));
    memset(temperature_window, 0, sizeof(temperature_window));
    window_index = 0;
    zero_sum = 0;
    zero_pressure = filtered_pressure = pressure_pa = temperature_c = 0.0f;
    previous_us = 0;
    sample_valid = false;
#if SIMULATION_MODE
    sim_velocity = 0.0f;
#endif
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// SensorLib_SetFilter()

void SensorLib_SetFilter(float hz)
{
    if (!isfinite(hz) || hz < 0.1f || hz > 10.0f)
        return;
    portENTER_CRITICAL(&config_lock);
    filter_hz = hz;
    portEXIT_CRITICAL(&config_lock);
}

///////////////////////////////////////////////////////////////////////////////
// SensorLib_FilterHz()

float SensorLib_FilterHz(void)
{
    portENTER_CRITICAL(&config_lock);
    const float value = filter_hz;
    portEXIT_CRITICAL(&config_lock);
    return value;
}

///////////////////////////////////////////////////////////////////////////////
// Simulation_SetOutput()

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

/* ControlTask only: every attempt publishes a distinct sample, including invalid
 * attempts. No queue is read here; filter/calibration state has one owner. */

///////////////////////////////////////////////////////////////////////////////
// SensorLib_PitotStep()

bool SensorLib_PitotStep(PitotSample *out)
{
    if (!out) return false;
    *out = (PitotSample){.sequence = ++sequence_count};
    float p = 0.0f, temp = 0.0f;
    bool valid_read = false;
    uint16_t count = 0;
    uint8_t status = 255;
    uint32_t acquisition_us = 0;
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
    const int64_t started = esp_timer_get_time();
    const bool received = I2CBus_TryReadOnce(PITOT_ADDRESS, raw, sizeof(raw));
    acquisition_us = (uint32_t)(esp_timer_get_time() - started);
    if (received)
    {
        status = raw[0] >> 6;
        count = (uint16_t)(((raw[0] & 0x3fu) << 8) | raw[1]);
        if (status == 0 && count >= 1638u && count <= 14746u)
        {
            p = ((float)count - 0.1f * 16383.0f) * (PITOT_MAX_PA - PITOT_MIN_PA) /
                (0.8f * 16383.0f) + PITOT_MIN_PA;
            temp = 200.0f * ((raw[2] << 3) | (raw[3] >> 5)) / 2047.0f - 50.0f;
            valid_read = isfinite(p) && isfinite(temp);
        }
    }
#endif
    float raw_average = 0.0f;
    sample_valid = false;
    if (valid_read)
    {
#if !SIMULATION_MODE
        /* Zero uses exactly 200 valid raw reads, independent of rolling averaging. */
        const bool calibrating = zero_samples < PITOT_ZERO_SAMPLES;
        if (calibrating)
        {
            zero_sum += p;
            zero_samples++;
            zero_pressure = (float)(zero_sum / zero_samples);
        }
#endif
        window_pressure += p - pressure_window[window_index];
        window_temperature += temp - temperature_window[window_index];
        pressure_window[window_index] = p;
        temperature_window[window_index] = temp;
        window_index = (window_index + 1u) % PITOT_AVERAGE_SAMPLES;
        if (samples_in_window < PITOT_AVERAGE_SAMPLES) samples_in_window++;
        raw_average = window_pressure / samples_in_window;
        temperature_c = window_temperature / samples_in_window;
#if !SIMULATION_MODE
        if (!calibrating)
        {
            const float corrected = raw_average - zero_pressure;
            const int64_t now = esp_timer_get_time();
            if (!previous_us) filtered_pressure = corrected;
            else
            {
                const float dt = (float)(now - previous_us) / 1000000.0f;
                const float alpha = 1.0f - expf(-6.283185307f * SensorLib_FilterHz() * dt);
                filtered_pressure += alpha * (corrected - filtered_pressure);
            }
            previous_us = now;
            pressure_pa = fabsf(filtered_pressure) < PITOT_DEADBAND_PA ? 0.0f : filtered_pressure;
            sample_valid = true;
        }
#else
        pressure_pa = p;
        sample_valid = true;
#endif
    }
    else error_count++;
    out->pressure_pa = pressure_pa;
    out->temperature_c = temperature_c;
#if SIMULATION_MODE
    out->value = sim_velocity;
#else
    out->value = sqrtf(2.0f * fabsf(pressure_pa) / AIR_DENSITY);
#endif
    out->acquisition_us = acquisition_us;
    out->errors = error_count;
    out->tick = xTaskGetTickCount();
    out->valid = valid_read && sample_valid && isfinite(out->value);
    portENTER_CRITICAL(&config_lock);
    pitot_communication_ok = valid_read;
    diagnostics.pitot_status_bits = status;
    diagnostics.pitot_raw_count = count;
    diagnostics.pitot_read_ok = valid_read;
    if (valid_read)
    {
        diagnostics.pitot_raw_pressure_pa = raw_average;
        diagnostics.pitot_corrected_pressure_pa = raw_average - zero_pressure;
    }
    diagnostics.pitot_zero_offset_pa = zero_pressure;
    diagnostics.pitot_filtered_pressure_pa = filtered_pressure;
    diagnostics.pitot_pressure_pa = pressure_pa;
    diagnostics.pitot_airspeed_ms = out->value;
    diagnostics.pitot_zero_samples = zero_samples;
    diagnostics.pitot_calibrated = zero_samples >= PITOT_ZERO_SAMPLES;
    diagnostics.pitot_sample_seq = out->sequence;
    diagnostics.pitot_tick = out->tick;
    diagnostics.pitot_valid = out->valid;
    portEXIT_CRITICAL(&config_lock);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// SensorLib_PitotCommunicationOk()

bool SensorLib_PitotCommunicationOk(void)
{
    portENTER_CRITICAL(&config_lock);
    const bool ok = pitot_communication_ok;
    portEXIT_CRITICAL(&config_lock);
    return ok;
}

/* Author: R.E. van Eekelen.
 * Incremental pressure-tap scan on Core 0, one I2C transaction per 10 ms
 * phase. A conversion is triggered and read in later phases; the entire
 * sixteen-channel sweep has no conversion waits; shared-bus contention is
 * reported to the independent Core 1 Pitot acquisition.
 *
 * The two TCA9548A multiplexers are selected in distinct transactions.
 * The 16 tap pressures are separate from the pitot/speed sensor.
 */

///////////////////////////////////////////////////////////////////////////////
// SensorLib_TapsStep()

bool SensorLib_TapsStep(PressureTapSample *out)
{
    if (out == NULL)
        return false;
#if PRESSURE_TAPS_ENABLED && !SIMULATION_MODE
    PressureTapSample *const aggregate = out; /* SensorTask owns the sole working image. */
    static unsigned channel;
    static uint8_t stage;
    static int64_t conversion_started_us;
    static bool initialized;
    if (!initialized)
    {
        for (unsigned i = 0; i < 16; ++i)
            aggregate->taps[i] = NAN;
        initialized = true;
    }
    const uint8_t off = 0;
    const uint8_t mask = (uint8_t)(1u << (channel % 8u));
    const uint8_t trigger[] = {0xAA, 0, 0};
    uint8_t data[4];
    bool success = false;
    switch (stage)
    {
    case 0: /* Disarm first multiplexer. */
        if (!I2CBus_TryWrite(MUX_0_ADDRESS, &off, 1))
            return false;
        stage = 1;
        return false;
    case 1: /* Disarm second multiplexer. */
        if (!I2CBus_TryWrite(MUX_1_ADDRESS, &off, 1))
            return false;
        stage = 2;
        return false;
    case 2: /* Select exactly one channel. */
        if (!I2CBus_TryWrite(channel < 8 ? MUX_0_ADDRESS : MUX_1_ADDRESS, &mask, 1))
        {
            aggregate->errors++;
            aggregate->taps[channel] = NAN;
            aggregate->taps_valid &= (uint16_t)~(1u << channel);
            stage = 5;
            return false;
        }
        stage = 3;
        return false;
    case 3: /* Start conversion. */
        if (!I2CBus_TryWrite(ABP2_ADDRESS, trigger, sizeof(trigger)))
        {
            aggregate->errors++;
            aggregate->taps[channel] = NAN;
            aggregate->taps_valid &= (uint16_t)~(1u << channel);
            stage = 5;
            return false;
        }
        conversion_started_us = esp_timer_get_time();
        stage = 4;
        return false;
    case 4: /* >= 5 ms conversion; never delay a task to wait. */
        if (esp_timer_get_time() - conversion_started_us < 5000)
            return false;
        success = I2CBus_TryRead(ABP2_ADDRESS, data, sizeof(data));
        if (success)
            success = !(data[0] & 0x25u) && (data[0] & 0x40u);
        if (success)
        {
            const uint32_t raw = ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
            success = raw >= ABP2_OUTPUT_MIN && raw <= ABP2_OUTPUT_MAX;
            if (success)
            {
                aggregate->taps[channel] = ((float)raw - ABP2_OUTPUT_MIN) *
                                              (ABP2_MAX_PA - ABP2_MIN_PA) /
                                              (ABP2_OUTPUT_MAX - ABP2_OUTPUT_MIN) +
                                          ABP2_MIN_PA;
                aggregate->tap_ticks[channel] = xTaskGetTickCount();
                aggregate->taps_valid |= (uint16_t)(1u << channel);
            }
        }
        if (!success)
        {
            aggregate->errors++;
            aggregate->taps[channel] = NAN;
            aggregate->taps_valid &= (uint16_t)~(1u << channel);
        }
        stage = 5;
        return false;
    case 5: /* Reset first mux before publishing anything. */
        if (!I2CBus_TryWrite(MUX_0_ADDRESS, &off, 1))
            return false;
        stage = 6;
        return false;
    case 6: /* Reset second mux before advancing. */
        if (!I2CBus_TryWrite(MUX_1_ADDRESS, &off, 1))
            return false;
        aggregate->updated_mask = (uint16_t)(1u << channel);
        channel = (channel + 1u) % 16u;
        stage = 0;
        aggregate->tick = xTaskGetTickCount();
        aggregate->sequence++;
        return true;
    default:
        stage = 0;
        return false;
    }
#else
    (void)out;
    return false;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// SensorLib_GetPitotDiagnostics()

void SensorLib_GetPitotDiagnostics(PitotDiagnostics *out)
{
    if (!out) return;
    portENTER_CRITICAL(&config_lock);
    *out = diagnostics;
    portEXIT_CRITICAL(&config_lock);
}

///////////////////////////////////////////////////////////////////////////////
// SensorLib_TapsValidMask()

uint16_t SensorLib_TapsValidMask(const PressureTapSample *sample, TickType_t now)
{
    if (!sample) return 0;
    uint16_t mask = sample->taps_valid;
    for (unsigned i = 0; i < 16; ++i)
        if ((TickType_t)(now - sample->tap_ticks[i]) >= pdMS_TO_TICKS(TAP_TIMEOUT_MS))
            mask &= (uint16_t)~(1u << i);
    return mask;
}
