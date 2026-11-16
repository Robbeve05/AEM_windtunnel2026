///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * FaultDiagnosticsTask.c
 *
 * Low-priority compact fault output and read-only serial commands.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "FaultRecorder.h"
#include "FaultSnapshot.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static void PrintRecord(const FaultRecord *record);
static void Command(const char *command);

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// PrintRecord()

static void PrintRecord(const FaultRecord *r)
{
    static char age[32];
    if (r->flags & FAULT_RECORD_AGE_VALID)
        (void)snprintf(age, sizeof(age), "%lu", (unsigned long)r->age_ms);
    else
        (void)snprintf(age, sizeof(age), "INVALID(raw=%lu)", (unsigned long)r->age_ms);
    printf("F#%lu t=%lu code=%04x state=%s mode=%s detail=%04lx now=%lu tick=%lu age=%s timeout=%u safety_seq=%lu valid=%u\n",
           (unsigned long)r->sequence, (unsigned long)r->now_ms, r->code,
           StateMachine_Name((SystemState_t)r->previous_state), r->manual_mode ? "MANUAL" : "AUTO",
           (unsigned long)r->detail, (unsigned long)r->now_ms, (unsigned long)r->safety_tick_ms, age, r->timeout_ms,
           (unsigned long)r->safety_sequence, !!(r->flags & FAULT_RECORD_SAFETY_VALID));
}

///////////////////////////////////////////////////////////////////////////////
// Command()

static void Command(const char *command)
{
    FaultRecord record;
    bool active;
    if (!strcmp(command, "FAULT?")) {
        if (FaultRecorder_First(&record, &active)) {
            printf("FAULT episode=%s\n", active ? "active" : "archived");
            PrintRecord(&record);
        } else puts("FAULT none");
    } else if (!strcmp(command, "FAULT HISTORY")) {
        uint32_t cursor = 0, lost;
        for (unsigned i = 0; i < FAULT_RECORD_CAPACITY && FaultRecorder_ReadAfter(cursor, &record, &lost); ++i) {
            if (lost) printf("FAULT history_overwritten=%lu\n", (unsigned long)lost);
            PrintRecord(&record);
            cursor = record.sequence;
        }
    } else if (!strcmp(command, "FAULT DETAIL")) {
        static Telemetry state;
        static char detail[6144];
        if (!FaultRecorder_First(&record, &active)) { puts("FAULT none"); return; }
        PrintRecord(&record);
        if (xQueuePeek(telemetry, &state, 0) == pdTRUE && state.first_fault.valid &&
            state.first_fault.triggering_inputs.now_ms == record.now_ms &&
            state.first_fault.code == (FaultReason_t)record.reason) {
            if (FaultSnapshot_Format(&state.first_fault, detail, sizeof(detail))) fputs(detail, stdout);
        } else puts("FAULT full detail unavailable; compact decision retained in history");
    }
}

///////////////////////////////////////////////////////////////////////////////
// FaultDiagnosticsTask()

void FaultDiagnosticsTask(void *argument)
{
    (void)argument;
    uint32_t cursor = 0;
    static char line[32];
    unsigned used = 0;
    bool overflow = false;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    bool serial_available = flags >= 0 && fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == 0;
    if (!serial_available) puts("FAULT console input unavailable; compact output remains active");
    for (;;) {
        FaultRecord record;
        uint32_t lost;
        for (unsigned i = 0; i < FAULT_RECORD_CAPACITY && FaultRecorder_ReadAfter(cursor, &record, &lost); ++i) {
            if (lost) printf("FAULT output_lost=%lu\n", (unsigned long)lost);
            PrintRecord(&record);
            cursor = record.sequence;
        }
        for (unsigned i = 0; serial_available && i < 64; ++i) {
            char c;
            if (read(STDIN_FILENO, &c, 1) != 1) break;
            if (c == '\n' || c == '\r') {
                if (!overflow && used) { line[used] = 0; Command(line); }
                used = 0; overflow = false;
            } else if (!overflow && used < sizeof(line) - 1) line[used++] = c;
            else overflow = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
