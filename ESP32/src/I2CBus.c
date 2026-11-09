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

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static bool transfer(uint8_t address, const uint8_t *tx, uint8_t *rx, size_t size, bool wait);

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
    return transfer(a, p, NULL, n, true);
}
///////////////////////////////////////////////////////////////////////////////
// I2CBus_Read()

bool I2CBus_Read(uint8_t a, uint8_t *p, size_t n)
{
    return transfer(a, NULL, p, n, true);
}
///////////////////////////////////////////////////////////////////////////////
// I2CBus_TryRead()

bool I2CBus_TryRead(uint8_t a, uint8_t *p, size_t n)
{
    return transfer(a, NULL, p, n, false);
}
///////////////////////////////////////////////////////////////////////////////
// I2CBus_TryWrite()

bool I2CBus_TryWrite(uint8_t a, const uint8_t *p, size_t n)
{
    return transfer(a, p, NULL, n, false);
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

static bool transfer(uint8_t address, const uint8_t *tx, uint8_t *rx, size_t size, bool wait)
{
    if (address >= 128) return false;
    const int64_t called = esp_timer_get_time();
    int64_t started = called;
    I2CBusResult result = I2C_RESULT_DRIVER;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    unsigned retries = 0;
    if (ready && mutex)
    {
        /* Output writer has bounded priority-inheriting acquisition; sensors
         * never wait. No retries on writes, timeouts, driver faults or busy. */
        if (xSemaphoreTake(mutex, wait ? pdMS_TO_TICKS(I2C_TIMEOUT_MS) : 0) != pdTRUE)
            result = I2C_RESULT_BUSY;
        else
        {
            started = esp_timer_get_time();
            error = rx ? i2c_master_read_from_device(0, address, rx, size, pdMS_TO_TICKS(I2C_TIMEOUT_MS)) :
                i2c_master_write_to_device(0, address, tx, size, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
            /* Only a quick NACK on a sensor read is retryable. One additional
             * attempt, within the original 10 ms transaction budget. */
            if (rx && !wait && error == ESP_FAIL && esp_timer_get_time() - started < 1000)
            {
                retries = 1;
                const uint32_t nack_ms = (uint32_t)(esp_timer_get_time() / 1000);
                portENTER_CRITICAL(&diagnostics_lock);
                diagnostics[address].results[I2C_RESULT_NACK]++;
                diagnostics[address].last_failure = I2C_RESULT_NACK;
                diagnostics[address].last_failure_driver_error = error;
                diagnostics[address].last_failure_ms = nack_ms;
                portEXIT_CRITICAL(&diagnostics_lock);
                TickType_t remaining = pdMS_TO_TICKS(I2C_TIMEOUT_MS - 1);
                if (!remaining) remaining = 1;
                error = i2c_master_read_from_device(0, address, rx, size, remaining);
            }
            result = error == ESP_OK ? I2C_RESULT_OK : error == ESP_FAIL ? I2C_RESULT_NACK :
                error == ESP_ERR_TIMEOUT ? I2C_RESULT_TIMEOUT : I2C_RESULT_DRIVER;
            xSemaphoreGive(mutex);
        }
    }
    const int64_t completed = esp_timer_get_time();
    const uint32_t duration = result == I2C_RESULT_BUSY ? 0 : (uint32_t)(completed - started);
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
    d->retries += retries;
    if (retries && result == I2C_RESULT_OK) d->recovered++;
    if (result != I2C_RESULT_OK && result != I2C_RESULT_BUSY) d->final_errors++;
    portEXIT_CRITICAL(&diagnostics_lock);
    return result == I2C_RESULT_OK;
}
