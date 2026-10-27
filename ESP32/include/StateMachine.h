/* Author: R.E. van Eekelen. Operational software, not a hardware safety function. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { STATE_INIT, STATE_WAIT, STATE_READY, STATE_RUNNING, STATE_STOP, STATE_FAULT } SystemState_t;
typedef enum { FAULT_NONE, FAULT_EMERGENCY_STOP, FAULT_SAFETY_FEEDBACK, FAULT_VFD,
    FAULT_MODBUS, FAULT_SENSOR, FAULT_PI_CONNECTION, FAULT_CONTROL_OVERRUN,
    FAULT_CLOCK_TIMEOUT, FAULT_INTERLOCK, FAULT_INTERNAL } FaultReason_t;
typedef struct {
    bool valid, commissioned, estop_pressed, relay_ok, stop_ok, outputs_ok;
    bool vfd_fault_available, vfd_fault, contactor_available, contactor_ok;
    bool start_pressed, reset_pressed;
    uint32_t tick_ms, raw_levels, start_sequence, reset_sequence, start_ms, reset_ms;
} SafetySnapshot;
typedef struct {
    uint32_t now_ms;
    SafetySnapshot safety;
    bool sensor_ok, drive_ready, drive_ok, drive_fault, pi_ok, stop, start, reset;
    bool stop_confirmed;
    FaultReason_t urgent_fault, timing_fault;
} MachineInputs;
typedef struct {
    SystemState_t state;
    FaultReason_t fault, last_fault;
    uint32_t entered_ms, boot_ms, reset_until_ms, epoch, transition_count;
    bool reset_pending, reset_accepted, recovery_requested, permissive;
} StateMachine;
void StateMachine_Init(StateMachine *m, uint32_t now_ms);
void StateMachine_Fault(StateMachine *m, FaultReason_t fault, uint32_t now_ms);
void StateMachine_Step(StateMachine *m, const MachineInputs *in);
const char *StateMachine_Name(SystemState_t state);
const char *FaultReason_Name(FaultReason_t fault);
