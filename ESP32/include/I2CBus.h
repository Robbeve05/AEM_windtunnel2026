///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * I2CBus.h
 *
 * Shared I2C bus initialization and serialized sensor/output communication.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "DigitalOutputs.h" // Existing users retain DigitalOutputs_Set compatibility.

///////////////////////////////////////////////////////////////////////////////
// Public data structures

/* Per-address counters distinguish contention from actual wire failures.
 * Durations exclude mutex waiting; call_us includes it. Snapshot is coherent. */
typedef enum {
    I2C_RESULT_OK, I2C_RESULT_BUSY, I2C_RESULT_NACK,
    I2C_RESULT_TIMEOUT, I2C_RESULT_DRIVER, I2C_RESULT_COUNT
} I2CBusResult;
typedef struct {
    uint32_t results[I2C_RESULT_COUNT];
    uint32_t last_us, max_us, max_call_us;
    uint32_t retries, recovered, final_errors;
    int last_driver_error;
    I2CBusResult last_result, last_failure;
    int last_failure_driver_error;
    uint32_t last_failure_ms;
} I2CBusDiagnostics;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool I2CBus_Init(void);
bool I2CBus_TryRead(uint8_t address, uint8_t *data, size_t size);
bool I2CBus_TryWrite(uint8_t address, const uint8_t *data, size_t size);
bool I2CBus_Write(uint8_t address, const uint8_t *data, size_t size);
bool I2CBus_Read(uint8_t address, uint8_t *data, size_t size);

bool I2CBus_GetDiagnostics(uint8_t address, I2CBusDiagnostics *out);
