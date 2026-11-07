///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * DigitalOutputs.h
 *
 * Operational outputs, not a safety function.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "StatusIndicators.h"

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    bool initialized, vfd_requested, vfd_acknowledged, confirmed_valid;
    uint8_t requested_image, confirmed_image;
    uint32_t write_errors;
} DigitalOutputStatus;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

/* Called once, before tasks, after the shared I2C bus is ready. */
bool DigitalOutputs_Init(void);
// InputHandlerTask owns output updates. Success acknowledges the image over
// I2C, not physical lamp or relay state.
bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators);


void DigitalOutputs_GetStatus(DigitalOutputStatus *status);
