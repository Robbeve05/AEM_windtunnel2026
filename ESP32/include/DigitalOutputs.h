/* Author: R.E. van Eekelen. Operational outputs, not a safety function. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "StatusIndicators.h"
// Called once, before tasks, after the shared I2C bus is ready.
bool DigitalOutputs_Init(void);
// InputHandlerTask owns output updates. Success acknowledges the image over I2C, not physical
// lamp or relay state.
bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators);
typedef struct {
    bool initialized, vfd_requested, confirmed_valid;
    uint8_t requested_image, confirmed_image;
    uint32_t write_errors;
} DigitalOutputStatus;
void DigitalOutputs_GetStatus(DigitalOutputStatus *status);
