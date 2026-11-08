///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * StateMachine.c
 *
 * Operational state transitions and fault recovery.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "StateMachine.h"

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static void transition(StateMachine *m, SystemState_t s, uint32_t now);
static bool safety_healthy(const MachineInputs *i);

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// StateMachine_Init()

/* Starts disabled in INIT with the original initialization deadline. */
void StateMachine_Init(StateMachine *m, uint32_t now)
{
    *m = (StateMachine){.state = STATE_INIT, .entered_ms = now, .boot_ms = now};
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// StateMachine_Name()

/*
 * Returns the existing state name used by telemetry.
 */

const char *StateMachine_Name(SystemState_t s)
{
    static const char *names[] = {"INIT", "WAIT", "READY", "RUNNING", "STOP", "FAULT", "STOPPED_PREPARE", "STARTING"};
    return (unsigned)s < 8 ? names[s] : "UNKNOWN";
}

/*
 * Returns the existing fault name used by telemetry.
 */

///////////////////////////////////////////////////////////////////////////////
// FaultReason_Name()

const char *FaultReason_Name(FaultReason_t f)
{
    static const char *names[] = {
        "NONE",          "EMERGENCY_STOP",  "SAFETY_FEEDBACK", "VFD",       "MODBUS",  "SENSOR",
        "PI_CONNECTION", "CONTROL_OVERRUN", "CLOCK_TIMEOUT",   "INTERLOCK", "INTERNAL",
        "DRIVE_INHIBIT", "CONFIGURATION", "START_TIMEOUT", "INITIALIZATION"};
    return (unsigned)f < sizeof(names) / sizeof(names[0]) ? names[f] : "INTERNAL";
}

///////////////////////////////////////////////////////////////////////////////
// Internal transition helper

/*
 * Records entry time and increments counters only when the state changes.
 */

///////////////////////////////////////////////////////////////////////////////
// transition()

static void transition(StateMachine *m, SystemState_t s, uint32_t now)
{
    if (m->state != s)
    {
        m->state = s;
        m->entered_ms = now;
        m->transition_count++;
        m->epoch++;
    }
}

///////////////////////////////////////////////////////////////////////////////
// StateMachine_GetBlockingReasons()

uint32_t StateMachine_GetBlockingReasons(const StateMachine *m, const MachineInputs *i)
{
    if (m == NULL || i == NULL || (m->state != STATE_WAIT && m->state != STATE_STOPPED_PREPARE))
        return 0;

    uint32_t reasons = 0;
    const bool safety_fresh = i->safety.valid && i->now_ms - i->safety.tick_ms < INPUT_TIMEOUT_MS;
    if (i->safety.estop_pressed)
        reasons |= WAIT_BLOCK_ESTOP_PRESSED;
    if (!i->safety.stop_ok)
        reasons |= WAIT_BLOCK_STOP_INPUT;
    if (!i->safety.valid)
        reasons |= WAIT_BLOCK_SAFETY_INPUT_INVALID;
    else if (!safety_fresh)
        reasons |= WAIT_BLOCK_SAFETY_INPUT_STALE;
    if (!i->safety.commissioned)
        reasons |= WAIT_BLOCK_SAFETY_NOT_COMMISSIONED;
    if (!i->safety.relay_ok)
        reasons |= WAIT_BLOCK_SAFETY_RELAY;
    if (!i->safety.outputs_ok)
        reasons |= WAIT_BLOCK_SAFETY_OUTPUTS;
    if (i->safety.vfd_fault_available && i->safety.vfd_fault)
        reasons |= WAIT_BLOCK_VFD_FAULT;
    if (i->safety.contactor_available && !i->safety.contactor_ok)
        reasons |= WAIT_BLOCK_CONTACTOR;
    if (!i->sensor_ok)
        reasons |= WAIT_BLOCK_SENSOR;
    if (!i->v20_modbus_initialized)
        reasons |= WAIT_BLOCK_MODBUS_NOT_INITIALIZED;
    if (!i->v20_response_received)
        reasons |= WAIT_BLOCK_MODBUS_NO_RESPONSE;
    if (i->v20_response_received && !i->drive_fault_free)
        reasons |= WAIT_BLOCK_DRIVE_FAULT;
    if (!i->drive_communication_ok)
        reasons |= WAIT_BLOCK_DRIVE_COMMUNICATION;
    if (!i->v20_configuration_valid)
        reasons |= WAIT_BLOCK_V20_CONFIGURATION;
    if (i->drive_fault_free && !i->drive_ready)
        reasons |= WAIT_BLOCK_V20_OPERATION_INHIBITED;
#if !SIMULATION_MODE
    if (!i->v20_run_commissioned)
        reasons |= WAIT_BLOCK_V20_RUN_LOCK;
#endif
    if (!i->pi_ok)
        reasons |= WAIT_BLOCK_PI;
    if (!m->reset_accepted && !m->reset_pending)
        reasons |= WAIT_BLOCK_RESET_SEQUENCE;
    if (m->reset_pending)
        reasons |= WAIT_BLOCK_RESET_FEEDBACK_PENDING;
    return reasons;
}

///////////////////////////////////////////////////////////////////////////////
// Fault handling

/*
 * Latches the first fault, allowing emergency stop to take priority.
 */

///////////////////////////////////////////////////////////////////////////////
// StateMachine_Fault()

void StateMachine_Fault(StateMachine *m, FaultReason_t f, uint32_t now)
{
    if (f == FAULT_NONE)
        return;
    const bool new_fault = m->state == STATE_FAULT && m->fault != f;
    if (m->fault == FAULT_NONE || f == FAULT_EMERGENCY_STOP)
        m->last_fault = f;
    if (m->fault == FAULT_NONE || f == FAULT_EMERGENCY_STOP)
        m->fault = f;
    if (m->state != STATE_FAULT || new_fault)
    {
        m->reset_pending = false;
        m->reset_accepted = false;
        if (m->state != STATE_FAULT)
            m->fault_from = m->state;
    }
    transition(m, STATE_FAULT, now);
    m->permissive = false;
}

///////////////////////////////////////////////////////////////////////////////
// Safety condition evaluation

/*
 * Checks physical feedback, commissioning and input freshness.
 */

///////////////////////////////////////////////////////////////////////////////
// safety_healthy()

static bool safety_healthy(const MachineInputs *i)
{
    return i->safety.valid && i->safety.commissioned && i->safety.outputs_ok &&
           !i->safety.estop_pressed && i->safety.relay_ok &&
           (!i->safety.vfd_fault_available || !i->safety.vfd_fault) &&
           (!i->safety.contactor_available || i->safety.contactor_ok) &&
           i->now_ms - i->safety.tick_ms < INPUT_TIMEOUT_MS;
}

///////////////////////////////////////////////////////////////////////////////
// StateMachine execution

/*
 * Evaluates faults and physical actions before applying the existing
 * transitions.
 */

///////////////////////////////////////////////////////////////////////////////
// StateMachine_Step()

void StateMachine_Step(StateMachine *m, const MachineInputs *i)
{
    m->recovery_requested = false;
    m->permissive = false;
    bool fresh = i->safety.valid && i->now_ms - i->safety.tick_ms < INPUT_TIMEOUT_MS;
    bool healthy = safety_healthy(i);
    bool stopped_process = i->sensor_ok && i->v20_diagnostics_complete &&
                   i->drive_communication_ok && !i->drive_run_locked &&
                   i->drive_fault_free && i->pi_ok &&
                   i->v20_configuration_valid && (SIMULATION_MODE || i->v20_run_commissioned);
    bool process = stopped_process && i->drive_ready && i->drive_ok;
    /* Emergency has priority over the first latched non-emergency cause. */
    if (i->safety.estop_pressed)
    {
        StateMachine_Fault(m, FAULT_EMERGENCY_STOP, i->now_ms);
        m->reset_pending = false;
        return;
    }
    if (i->urgent_fault != FAULT_NONE)
        StateMachine_Fault(m, i->urgent_fault, i->now_ms);
    if (fresh && !i->safety.relay_ok)
        StateMachine_Fault(m, FAULT_SAFETY_FEEDBACK, i->now_ms);
    if (m->state != STATE_INIT && (!fresh || !i->safety.outputs_ok))
        StateMachine_Fault(m, FAULT_INTERLOCK, i->now_ms);
    if (i->safety.vfd_fault_available && i->safety.vfd_fault)
        StateMachine_Fault(m, FAULT_VFD, i->now_ms);
    if (i->safety.contactor_available && !i->safety.contactor_ok)
        StateMachine_Fault(m, FAULT_SAFETY_FEEDBACK, i->now_ms);
    if (i->timing_fault != FAULT_NONE)
        StateMachine_Fault(m, i->timing_fault, i->now_ms);
    if (m->state == STATE_READY || m->state == STATE_STARTING ||
        m->state == STATE_RUNNING || m->state == STATE_STOP || m->state == STATE_STOPPED_PREPARE)
    {
        if (!i->sensor_ok)
            StateMachine_Fault(m, FAULT_SENSOR, i->now_ms);

        else if (i->drive_fault)
            StateMachine_Fault(m, FAULT_VFD, i->now_ms);
        else if (!i->drive_communication_ok)
            StateMachine_Fault(m, FAULT_MODBUS, i->now_ms);
        else if (!i->v20_configuration_valid)
            StateMachine_Fault(m, FAULT_CONFIGURATION, i->now_ms);
        else if (((!i->drive_ready || !i->drive_ok) && m->state != STATE_STOPPED_PREPARE &&
                  !i->drive_run_locked) ||
                 (m->state == STATE_RUNNING && !i->drive_operation_enabled) ||
                 (m->state == STATE_READY && i->drive_operation_enabled))
            StateMachine_Fault(m, FAULT_DRIVE_INHIBIT, i->now_ms);
        else if (i->drive_run_locked)
            StateMachine_Fault(m, FAULT_MODBUS, i->now_ms);
        else if (!i->pi_ok)
            StateMachine_Fault(m, FAULT_PI_CONNECTION, i->now_ms);
        else if (!healthy)
            StateMachine_Fault(m, FAULT_INTERLOCK, i->now_ms);
    }
    if (i->reset && (m->state == STATE_WAIT || m->state == STATE_FAULT) && fresh &&
        !i->safety.estop_pressed && !i->safety.reset_pressed && i->safety.stop_ok &&
        i->urgent_fault == FAULT_NONE && i->timing_fault == FAULT_NONE &&
        (!i->safety.contactor_available || i->safety.contactor_ok))
    {
        m->reset_pending = true;
        m->reset_until_ms = i->now_ms + RESET_FEEDBACK_WINDOW_MS;
        m->recovery_requested = true;
        process = false;
    }
    if (m->reset_pending && (int32_t)(i->now_ms - m->reset_until_ms) > 0)
        m->reset_pending = false;
    if (m->state == STATE_FAULT && m->fault == FAULT_EMERGENCY_STOP && m->reset_pending && fresh &&
        i->safety.outputs_ok && i->safety.relay_ok && i->safety.stop_ok &&
        !i->safety.estop_pressed && !i->safety.reset_pressed && i->urgent_fault == FAULT_NONE &&
        i->timing_fault == FAULT_NONE)
    {
        m->fault = FAULT_NONE;
        m->reset_pending = false;
        m->reset_accepted = true;
        transition(m, STATE_WAIT, i->now_ms);
    }
    switch (m->state)
    {
    case STATE_INIT:
        if (fresh && i->sensor_ok && i->v20_diagnostics_complete && i->v20_response_received &&
            i->drive_communication_ok && i->safety.outputs_ok)
            transition(m, STATE_WAIT, i->now_ms);
        break;
    case STATE_FAULT:
        if (m->reset_pending && healthy && stopped_process && i->safety.stop_ok &&
            i->timing_fault == FAULT_NONE && i->urgent_fault == FAULT_NONE)
        {
            m->fault = FAULT_NONE;
            m->reset_accepted = true;
            m->reset_pending = false;
            transition(m, STATE_WAIT, i->now_ms);
        }
        break;
    case STATE_WAIT:
        if (m->reset_pending && healthy && stopped_process && !i->reset && i->safety.stop_ok)
        {
            m->reset_accepted = true;
            m->reset_pending = false;
        }
        if (m->reset_accepted && healthy && stopped_process && !i->reset && i->safety.stop_ok)
            transition(m, STATE_STOPPED_PREPARE, i->now_ms);
        break;
    case STATE_STOPPED_PREPARE:
        /* Serial OFF1 throughout. Relay release of intended In1/OFF2 requires
         * separate as-built verification; no software safety override. */
        if (i->stop || !i->safety.stop_ok)
        {
            m->reset_accepted = false;
            transition(m, STATE_WAIT, i->now_ms);
        }
        else if (healthy && process && i->stopped_prepared)
            transition(m, STATE_READY, i->now_ms);
        else if (i->now_ms - m->entered_ms >= V20_PREPARE_TIMEOUT_MS)
            StateMachine_Fault(m, FAULT_DRIVE_INHIBIT, i->now_ms);
        break;
    case STATE_READY:
        if (i->start && !i->reset && !i->stop && i->safety.stop_ok && healthy && process)
            transition(m, STATE_STARTING, i->now_ms);
        break;
    case STATE_STARTING:
        if (i->stop || !i->safety.stop_ok)
            transition(m, STATE_STOP, i->now_ms);
        else if (i->start_confirmed)
            transition(m, STATE_RUNNING, i->now_ms);
        else if (i->now_ms - m->entered_ms >= V20_START_TIMEOUT_MS)
            StateMachine_Fault(m, FAULT_START_TIMEOUT, i->now_ms);
        break;
    case STATE_RUNNING:
        if (i->stop || !i->safety.stop_ok)
            transition(m, STATE_STOP, i->now_ms);
        break;
    case STATE_STOP:
        if (i->stop_confirmed && i->safety.stop_ok && !i->stop)
            transition(m, STATE_READY, i->now_ms);
        else if (i->now_ms - m->entered_ms >= NORMAL_STOP_TIMEOUT_MS)
            StateMachine_Fault(m, FAULT_VFD, i->now_ms);
        break;
    default:
        StateMachine_Fault(m, FAULT_INTERNAL, i->now_ms);
        break;
    }
    /* Keep the non-safety OFF2 permissive through a healthy normal OFF1 ramp. */
    m->permissive = healthy && process && m->fault == FAULT_NONE &&
                    (m->state == STATE_STARTING || m->state == STATE_RUNNING || m->state == STATE_STOP);
    if (m->reset_accepted &&
        m->fault == FAULT_NONE && healthy && stopped_process && i->safety.stop_ok &&
        i->stop_command_acknowledged &&
        (m->state == STATE_STOPPED_PREPARE || m->state == STATE_READY))
        m->permissive = true;
}
