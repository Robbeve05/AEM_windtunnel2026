/* Author: R.E. van Eekelen. Operational outputs, not a safety function. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
// Called once, before tasks, after the shared I2C bus is ready.
bool DigitalOutputs_Init(void);
// InputHandlerTask owns K2 authorization and flushing. Success means the latest
// complete image was acknowledged by I2C, never proof of physical contact state.
bool DigitalOutputs_Set(bool enabled);
// Stages lamps only. Does not access I2C or change K2. True means accepted,
// not physically confirmed. InputHandlerTask flushes the current image.
bool DigitalOutputs_SetStatusLeds(bool green, bool yellow, bool red);
typedef struct {
    bool initialized, vfd_requested, confirmed_valid;
    uint8_t requested_image, confirmed_image;
    uint32_t write_errors;
} DigitalOutputStatus;
void DigitalOutputs_GetStatus(DigitalOutputStatus *status);
