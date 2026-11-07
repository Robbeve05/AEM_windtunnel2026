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
static bool write_image(uint8_t image);

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
    bool ok = write_image(inactive);
#if !SIMULATION_MODE
    const uint8_t direction[] = {3, 0};
    if (ok)
        ok = I2CBus_Write(BOARD_DO_I2C_ADDRESS, direction, sizeof(direction));
#endif
    portENTER_CRITICAL(&state_lock);
    state.initialized = state.confirmed_valid = ok;
    state.confirmed_image = inactive;
    state.vfd_acknowledged = false;
    if (!ok)
        state.write_errors++;
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

static bool write_image(uint8_t image)
{
#if SIMULATION_MODE
    (void)image;
    return true;
#else
    const uint8_t data[] = {1, image};

    return I2CBus_Write(BOARD_DO_I2C_ADDRESS, data, sizeof(data));
#endif
}

///////////////////////////////////////////////////////////////////////////////
// DigitalOutputs_Set()

bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators)
{
    // Only VFD-enable relay writers take this mutex. FreeRTOS priority
    // inheritance applies.
    if (!writer_lock || xSemaphoreTake(writer_lock, portMAX_DELAY) != pdTRUE)
        return false;
    portENTER_CRITICAL(&state_lock);
    const bool ready = state.initialized;
    portEXIT_CRITICAL(&state_lock);
    if (!ready)
    {
        xSemaphoreGive(writer_lock);
        return false;
    }
    const bool authorized = enabled && SAFETY_IO_COMMISSIONED && !SafetyIO_Inhibited();
    portENTER_CRITICAL(&state_lock);
    state.vfd_requested = authorized;
    status_indicators = indicators;
    const uint8_t image = compose();
    state.requested_image = image;
    portEXIT_CRITICAL(&state_lock);
    const bool ok = write_image(image);
    portENTER_CRITICAL(&state_lock);
    state.confirmed_valid = ok;
    if (ok)
    {
        state.confirmed_image = image;
        state.vfd_acknowledged = authorized;
    }
    else
        state.write_errors++;
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
