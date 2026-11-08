///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * StateMachine.h
 *
 * Operational software, not a hardware safety
 * function.
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
// Public enumerations

typedef enum
{
    STATE_INIT,
    STATE_WAIT,
    STATE_READY,
    STATE_RUNNING,
    STATE_STOP,
    STATE_FAULT,
    STATE_STOPPED_PREPARE,
    STATE_STARTING
} SystemState_t;
typedef enum
{
    FAULT_NONE,
    FAULT_EMERGENCY_STOP,
    FAULT_SAFETY_FEEDBACK,
    FAULT_VFD,
    FAULT_MODBUS,
    FAULT_SENSOR,
    FAULT_PI_CONNECTION,
    FAULT_CONTROL_OVERRUN,
    FAULT_CLOCK_TIMEOUT,
    FAULT_INTERLOCK,
    FAULT_INTERNAL,
    FAULT_DRIVE_INHIBIT,
    FAULT_CONFIGURATION,
    FAULT_START_TIMEOUT,
    FAULT_INITIALIZATION
} FaultReason_t;
typedef enum
{
    WAIT_BLOCK_ESTOP_PRESSED = 1u << 0,
    WAIT_BLOCK_STOP_INPUT = 1u << 1,
    WAIT_BLOCK_SAFETY_INPUT_INVALID = 1u << 2,
    WAIT_BLOCK_SAFETY_INPUT_STALE = 1u << 3,
    WAIT_BLOCK_SAFETY_NOT_COMMISSIONED = 1u << 4,
    WAIT_BLOCK_SAFETY_RELAY = 1u << 5,
    WAIT_BLOCK_SAFETY_OUTPUTS = 1u << 6,
    WAIT_BLOCK_VFD_FAULT = 1u << 7,
    WAIT_BLOCK_CONTACTOR = 1u << 8,
    WAIT_BLOCK_SENSOR = 1u << 9,
    WAIT_BLOCK_MODBUS_NOT_INITIALIZED = 1u << 10,
    WAIT_BLOCK_MODBUS_NO_RESPONSE = 1u << 11,
    WAIT_BLOCK_V20_STATUS = 1u << 12,
    WAIT_BLOCK_DRIVE_COMMUNICATION = 1u << 13,
    WAIT_BLOCK_V20_NOT_READY = 1u << 14,
    WAIT_BLOCK_V20_RUN_LOCK = 1u << 15,
    WAIT_BLOCK_PI = 1u << 16,
    WAIT_BLOCK_RESET_SEQUENCE = 1u << 17,
    WAIT_BLOCK_RESET_FEEDBACK_PENDING = 1u << 18,
    WAIT_BLOCK_DRIVE_FAULT = 1u << 19,
    WAIT_BLOCK_V20_CONFIGURATION = 1u << 20,
    WAIT_BLOCK_V20_OPERATION_INHIBITED = 1u << 21
} WaitBlockingReason;

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    bool valid, commissioned, estop_pressed, relay_ok, stop_ok, outputs_ok;
    bool vfd_fault_available, vfd_fault, contactor_available, contactor_ok;
    bool start_pressed, reset_pressed;
    uint32_t tick_ms, raw_levels, start_sequence, reset_sequence, start_ms, reset_ms;
} SafetySnapshot;
typedef struct
{
    uint32_t now_ms;
    SafetySnapshot safety;
    bool sensor_ok, drive_ready, drive_ok, drive_fault_free, drive_fault, pi_ok, stop, start, reset;
    bool drive_communication_ok, v20_response_received, v20_status_healthy;
    bool v20_modbus_initialized, v20_run_commissioned;
    bool v20_diagnostics_complete, v20_configuration_valid;
    bool stop_confirmed, stopped_prepared, start_confirmed, drive_run_locked;
    bool drive_operation_enabled, stop_command_acknowledged;
    FaultReason_t urgent_fault, timing_fault;
} MachineInputs;

typedef struct
{
    SystemState_t state;
    SystemState_t fault_from;
    FaultReason_t fault, last_fault;
    uint32_t entered_ms, boot_ms, reset_until_ms, epoch, transition_count;
    bool reset_pending, reset_accepted, recovery_requested, permissive;
} StateMachine;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

void StateMachine_Init(StateMachine *m, uint32_t now_ms);
void StateMachine_Fault(StateMachine *m, FaultReason_t fault, uint32_t now_ms);
void StateMachine_Step(StateMachine *m, const MachineInputs *in);
uint32_t StateMachine_GetBlockingReasons(const StateMachine *m, const MachineInputs *in);
const char *StateMachine_Name(SystemState_t state);
const char *FaultReason_Name(FaultReason_t fault);
