///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * V20Simulation.c
 *
 * Deterministic operational V20 simulation for software verification.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <math.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "V20Simulation.h"

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// V20Simulation_Init()

void V20Simulation_Init(V20Simulation *m)
{
    *m = (V20Simulation){.relay_permission = true, .control = 0x047e};
}

///////////////////////////////////////////////////////////////////////////////
// V20Simulation_Command()

void V20Simulation_Command(V20Simulation *m, uint16_t control, uint16_t setpoint)
{
    const bool on = (control & 1u) != 0;
    const bool ack = (control & 0x80u) != 0;
    if (ack && !m->ack_previous && !on && !m->persistent_fault)
        m->fault = false;
    const bool off2 = m->off2 || !(control & 2u) ||
                      (m->relay_required && !m->relay_permission);
    const bool off3 = m->off3 || !(control & 4u);
    if (!on && !off2 && !off3)
        m->inhibit = false;
    if (on && !m->on_previous && !m->off1 && !off2 && !off3 && !m->fault && !m->inhibit)
        m->on_accepted = true;
    if (!on || m->off1 || off2 || off3 || m->fault)
        m->on_accepted = false;
    m->control = control;
    m->setpoint = setpoint;
    m->on_previous = on;
    m->ack_previous = ack;
}

///////////////////////////////////////////////////////////////////////////////
// V20Simulation_Step()

void V20Simulation_Step(V20Simulation *m, float seconds)
{
    if (!isfinite(seconds) || seconds <= 0)
        return;
    const bool off2 = m->off2 || !(m->control & 2u) ||
                      (m->relay_required && !m->relay_permission);
    const bool off3 = m->off3 || !(m->control & 4u);
    if (off2 || off3)
    {
        m->inhibit = true;
        m->on_accepted = false;
    }
    if (m->off1 || m->fault)
        m->on_accepted = false;
    const bool permits = (m->control & 0x0478u) == 0x0478u;
    m->enabled = m->on_accepted && permits && !off2 && !off3 && !m->fault;
    float target = m->enabled ? fminf(50.0f, m->setpoint * 50.0f / 16384.0f) : 0;
    /* Illustrative rates only: OFF1 5 Hz/s, OFF3 20 Hz/s, coast 1 Hz/s. */
    float rate = off2 || m->fault ? 1.0f : off3 ? 20.0f : 5.0f;
    float change = target - m->actual_hz;
    float limit = rate * seconds;
    m->actual_hz += fminf(limit, fmaxf(-limit, change));
}

///////////////////////////////////////////////////////////////////////////////
// V20Simulation_Status()

uint16_t V20Simulation_Status(const V20Simulation *m)
{
    const bool off2 = m->off2 || !(m->control & 2u) ||
                      (m->relay_required && !m->relay_permission);
    const bool off3 = m->off3 || !(m->control & 4u);
    uint16_t z = 0xeb00u;
    if (!off2) z |= 0x10u;
    if (!off3) z |= 0x20u;
    if (m->fault) z |= 8u;
    if (m->inhibit || off2 || off3) z |= 0x40u;
    if (!m->fault && !m->inhibit && !off2 && !off3) z |= 3u;
    if (m->enabled) z |= 4u;
    return z;
}
