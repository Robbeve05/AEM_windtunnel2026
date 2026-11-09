///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * I2CBus.c
 *
 * Core 0 only; never called by ControlTask.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "driver/i2c.h"
#include "esp_timer.h"
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "I2CBus.h"
#include "SafetyIO.h"

///////////////////////////////////////////////////////////////////////////////
// File-local variables

static SemaphoreHandle_t mutex;
static bool ready;
static portMUX_TYPE diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static I2CBusDiagnostics diagnostics[128];
_Static_assert(pdMS_TO_TICKS(I2C_SENSOR_TIMEOUT_MS) > 0, "Sensor timeout requires finer RTOS ticks");

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static bool transfer(uint8_t address, const uint8_t *tx, uint8_t *rx, size_t size, bool wait, const uint8_t *reg);

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// I2CBus_Init()

bool I2CBus_Init(void)
{
#if SIMULATION_MODE

    return DigitalOutputs_Init();
#else
    if (!mutex)
        mutex = xSemaphoreCreateMutex();
    if (!mutex)
        return false;
    const i2c_config_t c = {.mode = I2C_MODE_MASTER,
                            .sda_io_num = SENSOR_I2C_SDA_PIN,
                            .scl_io_num = SENSOR_I2C_SCL_PIN,
                            .sda_pullup_en = true,
                            .scl_pullup_en = true,
                            .master.clk_speed = SENSOR_I2C_HZ};
    if (!ready)
        ready = i2c_param_config(0, &c) == ESP_OK &&
            i2c_driver_install(0, I2C_MODE_MASTER, 0, 0, 0) == ESP_OK;
    /* Keep an installed bus usable when only the inactive output write failed. */
    return ready && DigitalOutputs_Init();
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// I2CBus_Write()

bool I2CBus_Write(uint8_t a, const uint8_t *p, size_t n)
{
    return transfer(a, p, NULL, n, true, NULL);
}
///////////////////////////////////////////////////////////////////////////////
// I2CBus_Read()

bool I2CBus_Read(uint8_t a, uint8_t *p, size_t n)
{
    return transfer(a, NULL, p, n, true, NULL);
}
///////////////////////////////////////////////////////////////////////////////
// I2CBus_TryRead()

bool I2CBus_TryRead(uint8_t a, uint8_t *p, size_t n)
{
    return transfer(a, NULL, p, n, false, NULL);
}
///////////////////////////////////////////////////////////////////////////////
// I2CBus_TryWrite()

bool I2CBus_TryWrite(uint8_t a, const uint8_t *p, size_t n)
{
    return transfer(a, p, NULL, n, false, NULL);
}

///////////////////////////////////////////////////////////////////////////////
// I2CBus_GetDiagnostics()

bool I2CBus_GetDiagnostics(uint8_t address, I2CBusDiagnostics *out)
{
    if (address >= 128 || !out) return false;
    portENTER_CRITICAL(&diagnostics_lock);
    *out = diagnostics[address];
    portEXIT_CRITICAL(&diagnostics_lock);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// transfer()

static bool transfer(uint8_t address, const uint8_t *tx, uint8_t *rx, size_t size, bool wait, const uint8_t *reg)
{
    if (address >= 128 || !size || (!tx && !rx)) return false;
    const int64_t called = esp_timer_get_time();
    int64_t started = called, wire_finished = called;
    I2CBusResult result = I2C_RESULT_DRIVER;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    unsigned retries = 0, wire_calls = 0;
    uint32_t waited = 0;
    const uint32_t budget_ms = wait ? I2C_TIMEOUT_MS : I2C_SENSOR_TIMEOUT_MS;
    TickType_t budget = pdMS_TO_TICKS(budget_ms);
    if (!budget) budget = 1;
    if (ready && mutex)
    {
        /* Output writer has bounded priority-inheriting acquisition; sensors
         * never wait. No retries on writes, timeouts, driver faults or busy. */
        if (xSemaphoreTake(mutex, wait ? pdMS_TO_TICKS(I2C_TIMEOUT_MS) : 0) != pdTRUE)
        {
            result = I2C_RESULT_BUSY;
            waited = (uint32_t)(esp_timer_get_time() - called);
        }
        else
        {
            started = esp_timer_get_time();
            waited = (uint32_t)(started - called);
            wire_calls++;
            error = reg ? i2c_master_write_read_device(0, address, reg, 1, rx, size, budget) :
                rx ? i2c_master_read_from_device(0, address, rx, size, budget) :
                i2c_master_write_to_device(0, address, tx, size, budget);
            /* Only a quick NACK on a sensor read is retryable. One additional
             * attempt, within the original sensor budget. Never retry a DO register read. */
            if (rx && !wait && !reg && error == ESP_FAIL &&
                esp_timer_get_time() - started < 1000 &&
                budget_ms * 1000u - (uint32_t)(esp_timer_get_time() - started) >= portTICK_PERIOD_MS * 1000u)
            {
                retries = 1;
                const uint32_t nack_ms = (uint32_t)(esp_timer_get_time() / 1000);
                portENTER_CRITICAL(&diagnostics_lock);
                diagnostics[address].results[I2C_RESULT_NACK]++;
                diagnostics[address].last_failure = I2C_RESULT_NACK;
                diagnostics[address].last_failure_driver_error = error;
                diagnostics[address].last_failure_ms = nack_ms;
                portEXIT_CRITICAL(&diagnostics_lock);
                TickType_t remaining = pdMS_TO_TICKS((budget_ms * 1000u -
                    (uint32_t)(esp_timer_get_time() - started)) / 1000u);
                if (!remaining) remaining = 1;
                wire_calls++;
                error = i2c_master_read_from_device(0, address, rx, size, remaining);
            }
            result = error == ESP_OK ? I2C_RESULT_OK : error == ESP_FAIL ? I2C_RESULT_NACK :
                error == ESP_ERR_TIMEOUT ? I2C_RESULT_TIMEOUT : I2C_RESULT_DRIVER;
            wire_finished = esp_timer_get_time();
            xSemaphoreGive(mutex);
        }
    }
    const int64_t completed = esp_timer_get_time();
    const uint32_t duration = wire_calls ? (uint32_t)(wire_finished - started) : 0;
    portENTER_CRITICAL(&diagnostics_lock);
    I2CBusDiagnostics *d = &diagnostics[address];
    d->results[result]++;
    d->last_result = result;
    if (result != I2C_RESULT_OK)
    {
        d->last_failure = result;
        d->last_failure_driver_error = result == I2C_RESULT_BUSY ? 0 : error;
        d->last_failure_ms = (uint32_t)(completed / 1000);
    }
    d->last_driver_error = result == I2C_RESULT_BUSY ? 0 : error;
    d->last_us = duration;
    if (duration > d->max_us) d->max_us = duration;
    if (completed - called > d->max_call_us) d->max_call_us = (uint32_t)(completed - called);
    d->last_wait_us = waited;
    if (waited > d->max_wait_us) d->max_wait_us = waited;
    d->wire_calls += wire_calls;
    d->total_us += duration;
    d->retries += retries;
    if (retries && result == I2C_RESULT_OK) d->recovered++;
    if (result != I2C_RESULT_OK && result != I2C_RESULT_BUSY) d->final_errors++;
    portEXIT_CRITICAL(&diagnostics_lock);
    return result == I2C_RESULT_OK;
}

///////////////////////////////////////////////////////////////////////////////
// I2CBus_ReadRegister()

bool I2CBus_ReadRegister(uint8_t address, uint8_t reg, uint8_t *data, size_t size)
{
    return transfer(address, NULL, data, size, true, &reg);
}

///////////////////////////////////////////////////////////////////////////////
// I2CBus_FormatDiagnostics()

int I2CBus_FormatDiagnostics(char *buffer, size_t size)
{
    int used = snprintf(buffer, size, " i2c_hz=%u i2c_diag_v=1", (unsigned)SENSOR_I2C_HZ);
    const uint8_t addresses[] = {BOARD_DO_I2C_ADDRESS, PITOT_ADDRESS, BOARD_RTC_I2C_ADDRESS,
#if PRESSURE_TAPS_ENABLED
        MUX_0_ADDRESS, MUX_1_ADDRESS, ABP2_ADDRESS,
#endif
    };
    if (used < 0 || (size_t)used >= size) return -1;
    for (unsigned i = 0; i < sizeof(addresses); ++i)
    {
        I2CBusDiagnostics d;
        I2CBus_GetDiagnostics(addresses[i], &d);
        /* v1: ok,busy,nack,timeout,driver,retries,recovered,wire_calls,
         * avg_us,max_us,max_wait_us,max_call_us,last_result,last_error,
         * last_failure,last_failure_error,failure_ms,final_errors */
        int n = snprintf(buffer + used, size - (size_t)used,
            " i2c_%02x=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%u,%d,%u,%d,%lu,%lu",
            addresses[i], (unsigned long)d.results[I2C_RESULT_OK],
            (unsigned long)d.results[I2C_RESULT_BUSY], (unsigned long)d.results[I2C_RESULT_NACK],
            (unsigned long)d.results[I2C_RESULT_TIMEOUT], (unsigned long)d.results[I2C_RESULT_DRIVER],
            (unsigned long)d.retries, (unsigned long)d.recovered, (unsigned long)d.wire_calls,
            (unsigned long)(d.wire_calls > d.retries ? d.total_us / (d.wire_calls - d.retries) : 0),
            (unsigned long)d.max_us, (unsigned long)d.max_wait_us, (unsigned long)d.max_call_us,
            (unsigned)d.last_result, d.last_driver_error, (unsigned)d.last_failure,
            d.last_failure_driver_error, (unsigned long)d.last_failure_ms, (unsigned long)d.final_errors);
        if (n < 0 || (size_t)n >= size - (size_t)used) return -1;
        used += n;
    }
    return used;
}
