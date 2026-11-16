///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * FaultRecorder.h
 *
 * Bounded first-fault records and archive, independent of logging and bus I/O.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "StateMachine.h"

///////////////////////////////////////////////////////////////////////////////
// Public definitions

#define FAULT_RECORD_CAPACITY 32u
#define FAULT_RECORD_SAFETY_VALID 1u
#define FAULT_RECORD_AGE_VALID 2u

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct {
    uint32_t sequence, now_ms, safety_tick_ms, safety_sequence, age_ms, detail;
    uint16_t code, timeout_ms;
    uint8_t previous_state, manual_mode, flags, reason;
} FaultRecord;
_Static_assert(sizeof(FaultRecord) == 32, "Fault records must remain 32 bytes");

///////////////////////////////////////////////////////////////////////////////
// Function declarations

void FaultRecorder_Reset(void); /* startup/tests only, before tasks run */
void FaultRecorder_Capture(FaultReason_t fault, SystemState_t previous,
                           const MachineInputs *decision, uint32_t interlock_mask);
void FaultRecorder_EndEpisode(void);
bool FaultRecorder_First(FaultRecord *record, bool *active);
bool FaultRecorder_ReadAfter(uint32_t cursor, FaultRecord *record, uint32_t *lost);
unsigned FaultRecorder_Count(void);
/* Formatting is ONLY for diagnostic/network consumers, never capture. */
int FaultRecorder_Format(const FaultRecord *record, char *buffer, size_t size);
void FaultDiagnosticsTask(void *argument);
