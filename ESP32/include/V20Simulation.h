///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * V20Simulation.h
 *
 * Deterministic operational V20 simulation for software verification.
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
// Public data structures

/* Operational test plant, not a claim about installed wiring or safety. Inputs
 * are independent so tests can model either a relay-dependent or serial-only V20. */
typedef struct {
    bool relay_permission, relay_required, off1, off2, off3, fault, persistent_fault;
    bool enabled, on_previous, on_accepted, inhibit, ack_previous;
    float actual_hz;
    uint16_t control, setpoint;
} V20Simulation;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

void V20Simulation_Init(V20Simulation *model);
void V20Simulation_Command(V20Simulation *model, uint16_t control, uint16_t setpoint);
void V20Simulation_Step(V20Simulation *model, float seconds);
uint16_t V20Simulation_Status(const V20Simulation *model);
