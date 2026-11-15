///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * DigitalOutputs.c
 *
 * One logical image, one serialized output writer.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_log.h"
#include "esp_timer.h"
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "DigitalOutputs.h"
#include "I2CBus.h"
#include "SafetyIO.h"

///////////////////////////////////////////////////////////////////////////////
// Local definitions

#define OUTPUT_BIT(n) (1u << (n))
#define CHECK_BIT(n) _Static_assert((n) >= 0 && (n) <= 7, "Output index must be 0..7")
CHECK_BIT(VFD_ENABLE_DO_BIT);
CHECK_BIT(RED_INDICATOR_DO_BIT);
CHECK_BIT(GREEN_INDICATOR_DO_BIT);
CHECK_BIT(YELLOW_INDICATOR_DO_BIT);
CHECK_BIT(SPARE_DO5_BIT);
CHECK_BIT(SPARE_DO6_BIT);
CHECK_BIT(SPARE_DO7_BIT);
CHECK_BIT(SPARE_DO8_BIT);
_Static_assert((OUTPUT_BIT(VFD_ENABLE_DO_BIT) | OUTPUT_BIT(RED_INDICATOR_DO_BIT) |
                OUTPUT_BIT(GREEN_INDICATOR_DO_BIT) | OUTPUT_BIT(YELLOW_INDICATOR_DO_BIT) |
                OUTPUT_BIT(SPARE_DO5_BIT) | OUTPUT_BIT(SPARE_DO6_BIT) | OUTPUT_BIT(SPARE_DO7_BIT) |
                OUTPUT_BIT(SPARE_DO8_BIT)) == 0xffu,
               "All eight output assignments must be unique");
_Static_assert(BOARD_DO_ACTIVE_LEVEL == 0 || BOARD_DO_ACTIVE_LEVEL == 1,
               "Invalid board output polarity");
_Static_assert(VFD_ENABLE_ACTIVE_LEVEL == 0 || VFD_ENABLE_ACTIVE_LEVEL == 1,
               "Invalid VFD relay polarity");

///////////////////////////////////////////////////////////////////////////////
// File-local variables

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t writer_lock;
static DigitalOutputStatus state;
static StatusIndicator status_indicators;

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static void set_output(uint8_t *image, unsigned bit, bool active, int active_level);
static uint8_t compose(void);
static bool write_image(uint8_t image, I2CTransactionEvent *event);

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// DigitalOutputs_Init()

bool DigitalOutputs_Init(void)
{
    // Initialization is single-threaded before any producer exists.
    if (!writer_lock)
        writer_lock = xSemaphoreCreateMutex();
    if (!writer_lock)
    {
        ESP_LOGE("DigitalOutputs", "Output mutex allocation failed");
        return false;
    }
    portENTER_CRITICAL(&state_lock);
    state = (DigitalOutputStatus){0};
    const uint8_t inactive = compose();
    state.requested_image = inactive;
    portEXIT_CRITICAL(&state_lock);
    // Load inactive latches before enabling any output direction, including relay
    // 11K3.
    I2CTransactionEvent transaction = {0};
    bool ok = write_image(inactive, &transaction);
    const bool image_written = ok;
#if !SIMULATION_MODE
    const uint8_t direction[] = {3, 0};
    if (ok)
        ok = I2CBus_WriteObserved(BOARD_DO_I2C_ADDRESS, direction, sizeof(direction), &transaction);
#endif
    portENTER_CRITICAL(&state_lock);
    state.initialized = state.confirmed_valid = state.write_ack_valid = ok;
    state.confirmed_image = inactive;
    state.last_written_valid = image_written;
    state.last_written_image = inactive;
    state.last_ack_us = ok ? transaction.timestamp_us : 0;
    state.vfd_acknowledged = false;
    if (!ok) {
        state.write_errors++;
        state.last_event = state.last_unhealthy_event = (OutputDiagnosticEvent){
            .valid = true, .timestamp_us = transaction.timestamp_us, .sequence = 1,
            .duration_us = transaction.duration_us, .address = BOARD_DO_I2C_ADDRESS,
            .operation = image_written ? OUTPUT_OP_DIRECTION : OUTPUT_OP_WRITE,
            .reason = OUTPUT_I2C_FAILURE, .driver_error = transaction.driver_error,
            .bus_result = transaction.result, .requested_image = inactive,
            .last_written_image = inactive, .last_written_valid = image_written};
    }
    portEXIT_CRITICAL(&state_lock);
    if (!ok)
        ESP_LOGE("DigitalOutputs",
                 "Inactive image/direction initialization failed; motor inhibited");
    return ok;
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// set_output()

static void set_output(uint8_t *image, unsigned bit, bool active, int active_level)
{
    const uint8_t mask = OUTPUT_BIT(bit);
    const bool high = active ? active_level != 0 : active_level == 0;
    if (high)
        *image |= mask;
    else
        *image &= (uint8_t)~mask;
}

///////////////////////////////////////////////////////////////////////////////
// compose()

static uint8_t compose(void)
{
    uint8_t image = BOARD_DO_ACTIVE_LEVEL ? 0x00u : 0xffu;
    set_output(&image, RED_INDICATOR_DO_BIT, status_indicators.red, BOARD_DO_ACTIVE_LEVEL);
    set_output(&image, GREEN_INDICATOR_DO_BIT, status_indicators.green, BOARD_DO_ACTIVE_LEVEL);
    set_output(&image, YELLOW_INDICATOR_DO_BIT, status_indicators.amber, BOARD_DO_ACTIVE_LEVEL);
    set_output(&image, VFD_ENABLE_DO_BIT, state.vfd_requested, VFD_ENABLE_ACTIVE_LEVEL);
    return image;
}

///////////////////////////////////////////////////////////////////////////////
// write_image()

static bool write_image(uint8_t image, I2CTransactionEvent *event)
{
#if SIMULATION_MODE
    (void)image;
    *event = (I2CTransactionEvent){.timestamp_us = (uint64_t)esp_timer_get_time()};
    return true;
#else
    const uint8_t data[] = {1, image};

    return I2CBus_WriteObserved(BOARD_DO_I2C_ADDRESS, data, sizeof(data), event);
#endif
}

///////////////////////////////////////////////////////////////////////////////
// DigitalOutputs_Set()

/* Missing software acknowledgement is not a physical relay observation. */
static void missing_ack_event(void)
{
    portENTER_CRITICAL(&state_lock);
    if (!state.last_event.valid || (state.initialized && state.last_event.reason != OUTPUT_ACK_MISSING)) {
        OutputDiagnosticEvent e = {.valid = true, .timestamp_us = (uint64_t)esp_timer_get_time(),
            .sequence = state.last_event.sequence + 1u, .address = BOARD_DO_I2C_ADDRESS,
            .reason = OUTPUT_ACK_MISSING, .operation = OUTPUT_OP_NONE,
            .requested_image = state.requested_image, .last_written_image = state.last_written_image,
            .last_written_valid = state.last_written_valid,
            .do1_requested = state.vfd_requested, .do1_acknowledged = state.vfd_acknowledged};
        state.last_event = state.last_unhealthy_event = e;
    }
    portEXIT_CRITICAL(&state_lock);
}

bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators)
{
    // Only VFD-enable relay writers take this mutex. FreeRTOS priority
    // inheritance applies.
    if (!writer_lock || xSemaphoreTake(writer_lock, portMAX_DELAY) != pdTRUE) {
        missing_ack_event();
        return false;
    }
    portENTER_CRITICAL(&state_lock);
    const bool ready = state.initialized;
    portEXIT_CRITICAL(&state_lock);
    if (!ready)
    {
        missing_ack_event();
        xSemaphoreGive(writer_lock);
        return false;
    }
    const bool authorized = enabled && SAFETY_IO_COMMISSIONED && !SafetyIO_Inhibited();
    portENTER_CRITICAL(&state_lock);
    const bool shutdown = state.vfd_requested && !authorized;
    const bool was_healthy = state.confirmed_valid;
    if (shutdown)
        state.shutdown_requested_us = (uint64_t)esp_timer_get_time();
    state.vfd_requested = authorized;
    status_indicators = indicators;
    const uint8_t image = compose();
    const bool changed = image != state.requested_image;
    state.requested_image = image;
    portEXIT_CRITICAL(&state_lock);
    /* Unchanged images are verified, never rewritten. Keep the 10 ms probe,
     * including DO1, and no retry/grace for unknown outputs. */
    uint8_t latch = 0;
    bool read_ok = false;
    bool ok;
    I2CTransactionEvent transaction = {0};
    if (changed)
        ok = write_image(image, &transaction);
    else
    {
#if SIMULATION_MODE
        latch = image;
        read_ok = true;
        transaction.timestamp_us = (uint64_t)esp_timer_get_time();
#else
        read_ok = I2CBus_ReadRegisterObserved(BOARD_DO_I2C_ADDRESS, 1, &latch, 1, &transaction);
#endif
        ok = read_ok && latch == image;
    }
    portENTER_CRITICAL(&state_lock);
    state.confirmed_valid = ok;
    if (changed)
    {
        state.write_ack_valid = ok;
        state.latch_valid = false;
    }
    else
    {
        state.latch_image = latch;
        state.latch_valid = read_ok;
        if (!read_ok) state.read_errors++;
        else if (latch != image) state.latch_mismatches++;
    }
    if (ok)
    {
        state.confirmed_image = image;
        state.vfd_acknowledged = authorized;
    }
    else
    {
        state.vfd_acknowledged = false;
        if (changed) state.write_errors++;
    }
    if (ok)
    {
        state.last_ack_us = transaction.timestamp_us;
        if (changed) {
            state.last_written_image = image;
            state.last_written_valid = true;
        }
    }
    if ((was_healthy && !ok) || shutdown)
    {
        OutputDiagnosticEvent e = {.valid = true,
            .timestamp_us = transaction.timestamp_us,
            .sequence = state.last_event.sequence + 1u,
            .duration_us = transaction.duration_us, .address = BOARD_DO_I2C_ADDRESS,
            .operation = changed ? OUTPUT_OP_WRITE : OUTPUT_OP_READ_LATCH,
            .driver_error = transaction.driver_error, .bus_result = transaction.result,
            .requested_image = image, .last_written_image = state.last_written_image,
            .last_written_valid = state.last_written_valid,
            .readback_image = latch, .readback_valid = read_ok,
            .do1_requested = authorized, .do1_acknowledged = state.vfd_acknowledged,
            .reason = !ok ? (!changed && read_ok ? OUTPUT_READBACK_MISMATCH : OUTPUT_I2C_FAILURE)
                          : OUTPUT_SOFTWARE_SHUTDOWN};
        state.last_event = e;
        if (!ok) state.last_unhealthy_event = e;
    }
    portEXIT_CRITICAL(&state_lock);
    xSemaphoreGive(writer_lock);
    return ok;
}

///////////////////////////////////////////////////////////////////////////////
// DigitalOutputs_GetStatus()

void DigitalOutputs_GetStatus(DigitalOutputStatus *out)
{
    if (!out)
        return;
    portENTER_CRITICAL(&state_lock);
    *out = state;
    portEXIT_CRITICAL(&state_lock);
}
