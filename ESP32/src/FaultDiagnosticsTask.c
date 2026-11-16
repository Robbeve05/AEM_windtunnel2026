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
#include "FaultCodeRegistry.h"
#include "FaultSnapshot.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static void PrintRecord(const FaultRecord *record);
static void PrintFaultEvidence(const FaultRecord *record);
static void PrintLiveStatus(bool *initialized, SystemState_t *last_state,
                            OperatingMode *last_mode, FaultReason_t *last_fault,
                            TickType_t *last_periodic_tick);
static const char *CodeDescription(uint16_t code);
static void Command(const char *command);

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// CodeDescription()

static const char *CodeDescription(uint16_t code)
{
    switch (code) {
#define FAULT(reason, number, description) case number: return description;
#define INTERLOCK(bit, number, description) case number: return description;
        FAULT_CODE_REGISTRY(FAULT, INTERLOCK)
#undef FAULT
#undef INTERLOCK
    default: return "Unknown fault code";
    }
}

///////////////////////////////////////////////////////////////////////////////
// PrintFaultEvidence()

static void PrintFaultEvidence(const FaultRecord *record)
{
    /* Telemetry is a large structure: keep the copy off the task stack. */
    static Telemetry snapshot;
    if (xQueuePeek(telemetry, &snapshot, 0) != pdTRUE)
        return;

    const FirstFaultSnapshot *f = &snapshot.first_fault;
    if (!f->valid || f->triggering_inputs.now_ms != record->now_ms ||
        f->code != (FaultReason_t)record->reason) {
        puts("FAULT_DETAIL automatic_full_snapshot_unavailable (use FAULT DETAIL)");
        return;
    }
    printf("FAULT_DETAIL reason=%s source=%s interlock_mask=0x%08lx"
           " safety_age_ms=%lu safety_valid=%u relay=%u stop_ok=%u outputs_ok=%u"
           " do1_req=%u do1_ack=%u do1_ack_valid=%u\n",
           FaultReason_Name(f->code), f->source ? f->source : "unknown",
           (unsigned long)f->interlock_reason, (unsigned long)f->safety_age_ms,
           (unsigned)f->safety.valid, (unsigned)f->safety.relay_ok,
           (unsigned)f->safety.stop_ok, (unsigned)f->safety.outputs_ok,
           (unsigned)f->do1_requested, (unsigned)f->do1_acknowledged,
           (unsigned)f->do1_valid);
    printf("FAULT_DRIVE v20_status=0x%04x actual_hz=%.2f"
           " modbus_errors=%lu timeouts=%lu last_operation=%u last_result=%u\n",
           (unsigned)f->motor.status, (double)f->motor.actual_hz,
           (unsigned long)f->motor.errors, (unsigned long)f->motor.timeouts,
           (unsigned)f->motor.transaction.type, (unsigned)f->motor.transaction.result);
}

///////////////////////////////////////////////////////////////////////////////
// PrintLiveStatus()

static void PrintLiveStatus(bool *initialized, SystemState_t *last_state,
                            OperatingMode *last_mode, FaultReason_t *last_fault,
                            TickType_t *last_periodic_tick)
{
    static Telemetry state;
    static MotorStatus motor;
    if (xQueuePeek(telemetry, &state, 0) != pdTRUE)
        return;
    const TickType_t now = xTaskGetTickCount();
    const bool changed = !*initialized || state.state != *last_state ||
                         state.mode != *last_mode || state.fault != *last_fault;
    if (changed) {
        printf("STATE state=%s mode=%s fault=%s last_fault=%s\n",
               StateMachine_Name(state.state), state.mode == MODE_MANUAL ? "MANUAL" : "AUTO",
               FaultReason_Name(state.fault), FaultReason_Name(state.last_fault));
        *initialized = true;
        *last_state = state.state;
        *last_mode = state.mode;
        *last_fault = state.fault;
    }
    if (!changed && (TickType_t)(now - *last_periodic_tick) < pdMS_TO_TICKS(5000))
        return;
    *last_periodic_tick = now;
    memset(&motor, 0, sizeof(motor));
    (void)xQueuePeek(motor_status, &motor, 0);
    printf("LIVE state=%s mode=%s pi=%u sensor=%u safety=%u relay=%u outputs=%u"
           " v20_status=0x%04x v20_comm=%u hz_cmd=%.2f hz_actual=%.2f"
           " ctrl_us=%lu jitter_us=%lu missed=%lu\n",
           StateMachine_Name(state.state), state.mode == MODE_MANUAL ? "MANUAL" : "AUTO",
           (unsigned)state.pi_ok, (unsigned)state.sensor_ok,
           (unsigned)state.safety.valid, (unsigned)state.safety.relay_ok,
           (unsigned)state.safety.outputs_ok, (unsigned)motor.status,
           (unsigned)motor.communication_ok, (double)state.applied_hz,
           (double)motor.actual_hz, (unsigned long)state.execution_us,
           (unsigned long)state.max_jitter_us, (unsigned long)state.missed_deadlines);
}

///////////////////////////////////////////////////////////////////////////////
// PrintRecord()

static void PrintRecord(const FaultRecord *r)
{
    static char age[32];
    if (r->flags & FAULT_RECORD_AGE_VALID)
        (void)snprintf(age, sizeof(age), "%lu", (unsigned long)r->age_ms);
    else
        (void)snprintf(age, sizeof(age), "INVALID(raw=%lu)", (unsigned long)r->age_ms);
    printf("F#%lu t=%lu code=%04x name=\"%s\" state=%s mode=%s detail=%04lx now=%lu tick=%lu age=%s timeout=%u safety_seq=%lu valid=%u\n",
           (unsigned long)r->sequence, (unsigned long)r->now_ms, r->code,
           CodeDescription(r->code), StateMachine_Name((SystemState_t)r->previous_state), r->manual_mode ? "MANUAL" : "AUTO",
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
    bool state_initialized = false;
    SystemState_t last_state = STATE_INIT;
    OperatingMode last_mode = MODE_MANUAL;
    FaultReason_t last_fault = FAULT_NONE;
    TickType_t last_periodic_tick = 0;
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
            PrintFaultEvidence(&record);
            cursor = record.sequence;
        }
        PrintLiveStatus(&state_initialized, &last_state, &last_mode, &last_fault, &last_periodic_tick);
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
