/*
 * StateMachine.c
 * Operational state transitions and fault recovery.
 *
 * Author: R.E. van Eekelen
 */
#include "StateMachine.h"
#include "AppConfig.h"

///////////////////////////////////////////////////////////////////////////////
// State and fault names

/*
 * Returns the existing state name used by telemetry.
 */
const char *StateMachine_Name(SystemState_t s)
{
    static const char *names[] = {"INIT", "WAIT", "READY", "RUNNING", "STOP", "FAULT"};
    return (unsigned)s < 6 ? names[s] : "UNKNOWN";
}

/*
 * Returns the existing fault name used by telemetry.
 */
const char *FaultReason_Name(FaultReason_t f)
{
    static const char *names[] = {
        "NONE",          "EMERGENCY_STOP",  "SAFETY_FEEDBACK", "VFD",       "MODBUS",  "SENSOR",
        "PI_CONNECTION", "CONTROL_OVERRUN", "CLOCK_TIMEOUT",   "INTERLOCK", "INTERNAL"};
    return (unsigned)f < sizeof(names) / sizeof(names[0]) ? names[f] : "INTERNAL";
}

///////////////////////////////////////////////////////////////////////////////
// Internal transition helper

/*
 * Records entry time and increments counters only when the state changes.
 */
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
// Initialization

/*
 * Starts disabled in INIT with the original initialization deadline.
 */
void StateMachine_Init(StateMachine *m, uint32_t now)
{
    *m = (StateMachine){.state = STATE_INIT, .entered_ms = now, .boot_ms = now};
}

///////////////////////////////////////////////////////////////////////////////
// Fault handling

/*
 * Latches the first fault, allowing emergency stop to take priority.
 */
void StateMachine_Fault(StateMachine *m, FaultReason_t f, uint32_t now)
{
    if (f == FAULT_NONE)
        return;
    m->last_fault = f;
    if (m->fault == FAULT_NONE || f == FAULT_EMERGENCY_STOP)
        m->fault = f;
    if (m->state != STATE_FAULT)
    {
        m->reset_pending = false;
        m->reset_accepted = false;
    }
    transition(m, STATE_FAULT, now);
    m->permissive = false;
}

///////////////////////////////////////////////////////////////////////////////
// Safety condition evaluation

/*
 * Checks physical feedback, commissioning and input freshness.
 */
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
 * Evaluates faults and physical actions before applying the existing transitions.
 */
void StateMachine_Step(StateMachine *m, const MachineInputs *i)
{
    m->recovery_requested = false;
    m->permissive = false;
    bool fresh = i->safety.valid && i->now_ms - i->safety.tick_ms < INPUT_TIMEOUT_MS;
    bool healthy = safety_healthy(i);
    bool process = i->sensor_ok && i->drive_ready && i->drive_ok && i->pi_ok;
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
    if (m->state == STATE_READY || m->state == STATE_RUNNING || m->state == STATE_STOP)
    {
        if (!i->sensor_ok)
            StateMachine_Fault(m, FAULT_SENSOR, i->now_ms);
        else if (i->drive_fault)
            StateMachine_Fault(m, FAULT_VFD, i->now_ms);
        else if (!i->drive_ok || !i->drive_ready)
            StateMachine_Fault(m, FAULT_MODBUS, i->now_ms);
        else if (!i->pi_ok)
            StateMachine_Fault(m, FAULT_PI_CONNECTION, i->now_ms);
        else if (!healthy)
            StateMachine_Fault(m, FAULT_INTERLOCK, i->now_ms);
    }
    if (i->reset && (m->state == STATE_WAIT || m->state == STATE_FAULT) && fresh &&
        !i->safety.estop_pressed && !i->safety.reset_pressed && i->safety.stop_ok)
    {
        m->reset_pending = true;
        m->reset_until_ms = i->now_ms + RESET_FEEDBACK_WINDOW_MS;
        m->recovery_requested = true;
        process = false;
    }
    if (m->reset_pending && (int32_t)(i->now_ms - m->reset_until_ms) > 0)
        m->reset_pending = false;
    switch (m->state)
    {
    case STATE_INIT:
        if (fresh && i->sensor_ok && i->drive_ready && i->drive_ok && i->safety.outputs_ok)
            transition(m, STATE_WAIT, i->now_ms);
        else if (i->now_ms - m->boot_ms >= INIT_TIMEOUT_MS)
            StateMachine_Fault(m, FAULT_INTERNAL, i->now_ms);
        break;
    case STATE_FAULT:
        if (m->reset_pending && healthy && process && i->safety.stop_ok &&
            i->timing_fault == FAULT_NONE && i->urgent_fault == FAULT_NONE)
        {
            m->fault = FAULT_NONE;
            m->reset_accepted = true;
            m->reset_pending = false;
            transition(m, STATE_WAIT, i->now_ms);
        }
        break;
    case STATE_WAIT:
        if (m->reset_pending && healthy && process && i->safety.stop_ok)
        {
            m->reset_accepted = true;
            m->reset_pending = false;
        }
        if (m->reset_accepted && healthy && process && i->safety.stop_ok)
            transition(m, STATE_READY, i->now_ms);
        break;
    case STATE_READY:
        if (i->start && !i->reset && !i->stop && i->safety.stop_ok && healthy && process)
            transition(m, STATE_RUNNING, i->now_ms);
        break;
    case STATE_RUNNING:
        if (i->stop || !i->safety.stop_ok)
            transition(m, STATE_STOP, i->now_ms);
        break;
    case STATE_STOP:
        if (i->stop_confirmed)
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
                    (m->state == STATE_RUNNING || m->state == STATE_STOP);
}
