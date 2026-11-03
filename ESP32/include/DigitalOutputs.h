/* Author: R.E. van Eekelen. Operational outputs, not a safety function. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
// Called once, before tasks, after the shared I2C bus is ready.
bool DigitalOutputs_Init(void);
// InputHandlerTask owns the VFD-enable relay authorization and flushing. Success means the latest
// complete image was acknowledged by I2C, never proof of physical contact state.
bool DigitalOutputs_Set(bool enabled);
typedef struct {
    bool initialized, vfd_requested, confirmed_valid;
    uint8_t requested_image, confirmed_image;
    uint32_t write_errors;
} DigitalOutputStatus;
void DigitalOutputs_GetStatus(DigitalOutputStatus *status);
