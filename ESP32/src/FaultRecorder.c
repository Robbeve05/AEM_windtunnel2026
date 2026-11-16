///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * FaultRecorder.c
 *
 * Static ring and immutable first cause; capture never formats or allocates.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "FaultRecorder.h"
#include "FaultCodes.h"
#include "SafetyTime.h"

///////////////////////////////////////////////////////////////////////////////
// Local definitions

_Static_assert(INPUT_TIMEOUT_MS <= UINT16_MAX, "Fault timeout field too small");

///////////////////////////////////////////////////////////////////////////////
// File-local variables

static portMUX_TYPE recorder_lock = portMUX_INITIALIZER_UNLOCKED;
static FaultRecord records[FAULT_RECORD_CAPACITY], first;
static unsigned next, count;
static uint32_t sequence;
static bool episode_active, first_valid;
static FaultReason_t last_reason;

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// FaultRecorder_Reset()

void FaultRecorder_Reset(void)
{
    portENTER_CRITICAL(&recorder_lock);
    memset(records, 0, sizeof(records));
    first = (FaultRecord){0};
    next = count = sequence = 0;
    episode_active = first_valid = false;
    last_reason = FAULT_NONE;
    portEXIT_CRITICAL(&recorder_lock);
}

///////////////////////////////////////////////////////////////////////////////
// FaultRecorder_Capture()

void FaultRecorder_Capture(FaultReason_t fault, SystemState_t previous,
                           const MachineInputs *decision, uint32_t interlock_mask)
{
    if (fault == FAULT_NONE || !decision) return;
    portENTER_CRITICAL(&recorder_lock);
    if (!episode_active || fault != last_reason) {
        FaultRecord *r = &records[next];
        r->sequence = ++sequence;
        r->now_ms = decision->now_ms;
        r->safety_tick_ms = decision->safety.tick_ms;
        r->safety_sequence = decision->safety.snapshot_sequence;
        r->age_ms = SafetyTime_Age(decision->now_ms, decision->safety.tick_ms);
        r->detail = fault == FAULT_INTERLOCK ? interlock_mask : 0;
        r->code = fault == FAULT_INTERLOCK ? FaultCodes_ForInterlock(interlock_mask) :
                                           FaultCodes_ForReason(fault);
        r->timeout_ms = INPUT_TIMEOUT_MS;
        r->previous_state = (uint8_t)previous;
        r->manual_mode = decision->manual_mode;
        r->flags = (decision->safety.valid ? FAULT_RECORD_SAFETY_VALID : 0) |
            (SafetyTime_AgeValid(decision->now_ms, decision->safety.tick_ms) ? FAULT_RECORD_AGE_VALID : 0);
        r->reason = (uint8_t)fault;
        if (!episode_active) { first = *r; first_valid = true; }
        episode_active = true;
        last_reason = fault;
        next = (next + 1u) % FAULT_RECORD_CAPACITY;
        if (count < FAULT_RECORD_CAPACITY) count++;
    }
    portEXIT_CRITICAL(&recorder_lock);
}

///////////////////////////////////////////////////////////////////////////////
// FaultRecorder_EndEpisode()

void FaultRecorder_EndEpisode(void)
{
    portENTER_CRITICAL(&recorder_lock);
    episode_active = false; /* first remains available; its record is already archived */
    portEXIT_CRITICAL(&recorder_lock);
}

///////////////////////////////////////////////////////////////////////////////
// FaultRecorder_First()

bool FaultRecorder_First(FaultRecord *record, bool *active)
{
    if (!record) return false;
    portENTER_CRITICAL(&recorder_lock);
    bool available = first_valid;
    if (available) *record = first;
    if (active) *active = episode_active;
    portEXIT_CRITICAL(&recorder_lock);
    return available;
}

///////////////////////////////////////////////////////////////////////////////
// FaultRecorder_ReadAfter()

bool FaultRecorder_ReadAfter(uint32_t cursor, FaultRecord *record, uint32_t *lost)
{
    if (!record) return false;
    bool found = false;
    portENTER_CRITICAL(&recorder_lock);
    for (unsigned i = 0; i < count; ++i) {
        const FaultRecord *r = &records[(next + FAULT_RECORD_CAPACITY - count + i) % FAULT_RECORD_CAPACITY];
        if ((int32_t)(r->sequence - cursor) > 0) {
            *record = *r;
            if (lost) *lost = r->sequence - cursor - 1u;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&recorder_lock);
    return found;
}

///////////////////////////////////////////////////////////////////////////////
// FaultRecorder_Count()

unsigned FaultRecorder_Count(void)
{
    portENTER_CRITICAL(&recorder_lock);
    unsigned result = count;
    portEXIT_CRITICAL(&recorder_lock);
    return result;
}

///////////////////////////////////////////////////////////////////////////////
// FaultRecorder_Format()

int FaultRecorder_Format(const FaultRecord *r, char *buffer, size_t size)
{
    if (!r || !buffer || !size) return -1;
    int n = snprintf(buffer, size,
        "FAULT seq=%lu t=%lu code=%04x detail=%04lx prev=%u mode=%u tick=%lu age=%lu timeout=%u safety_seq=%lu flags=%u reason=%u\n",
        (unsigned long)r->sequence, (unsigned long)r->now_ms, r->code, (unsigned long)r->detail,
        r->previous_state, r->manual_mode, (unsigned long)r->safety_tick_ms,
        (unsigned long)r->age_ms, r->timeout_ms, (unsigned long)r->safety_sequence, r->flags, r->reason);
    return n < 0 || (size_t)n >= size ? -1 : n;
}
