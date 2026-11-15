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

typedef enum {
    OUTPUT_EVENT_NONE, OUTPUT_I2C_FAILURE, OUTPUT_READBACK_MISMATCH,
    OUTPUT_ACK_MISSING, OUTPUT_SOFTWARE_SHUTDOWN
} OutputEventReason;
typedef enum { OUTPUT_OP_NONE, OUTPUT_OP_WRITE, OUTPUT_OP_READ_LATCH, OUTPUT_OP_DIRECTION } OutputOperation;
typedef struct {
    bool valid, readback_valid, last_written_valid;
    uint64_t timestamp_us;
    uint32_t sequence, duration_us;
    uint8_t address, requested_image, last_written_image, readback_image;
    int driver_error;
    unsigned bus_result;
    OutputEventReason reason;
    OutputOperation operation;
    bool do1_requested, do1_acknowledged;
} OutputDiagnosticEvent;

typedef struct
{
    bool initialized, vfd_requested, vfd_acknowledged, confirmed_valid;
    uint8_t requested_image, confirmed_image;
    uint32_t write_errors;
    uint64_t shutdown_requested_us, last_ack_us;
    bool last_written_valid;
    uint8_t last_written_image;
    OutputDiagnosticEvent last_event, last_unhealthy_event;
    bool write_ack_valid, latch_valid;
    uint8_t latch_image;
    uint32_t read_errors, latch_mismatches;
} DigitalOutputStatus;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

/* Called once, before tasks, after the shared I2C bus is ready. */
bool DigitalOutputs_Init(void);
// InputHandlerTask owns output updates. Success acknowledges the image over
// I2C write ACK or a matching latch read, not physical lamp or relay state.
bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators);


void DigitalOutputs_GetStatus(DigitalOutputStatus *status);
