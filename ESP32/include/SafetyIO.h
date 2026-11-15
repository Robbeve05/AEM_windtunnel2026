///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * SafetyIO.h
 *
 * Diagnostic inputs only.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "StateMachine.h"

///////////////////////////////////////////////////////////////////////////////
// Function declarations

void SafetyIO_Init(void);
SafetySnapshot SafetyIO_Read(void);
FaultReason_t SafetyIO_Fault(void);
bool SafetyIO_Stop(void);
void SafetyIO_Acknowledge(void);
bool SafetyIO_Inhibited(void);
bool SafetyIO_Simulate(const char *name, bool active);
/* Converts a sampled GPIO level using its configured active level; false means
 * invalid input. */
bool SafetyIO_InterpretLevel(int raw_level, int active_level, bool *active);

///////////////////////////////////////////////////////////////////////////////
// Task-context button notification interface

/* Called only by InputHandlerTask after debouncing and publishing its snapshot.
 */
void SafetyIO_PublishButtonEvents(const SafetySnapshot *current, const SafetySnapshot *previous);

#define SAFETY_GPIO_EVENT_CAPACITY 16u
typedef struct {
    bool valid;
    uint64_t timestamp_us;
    uint32_t generation, unsafe_generation;
    int gpio, active_level, raw_level;
    int estop_raw, relay_raw, stop_raw;
    int estop_polarity, relay_polarity, stop_polarity;
    bool emergency_latch, relay_latch, stop_latch;
} SafetyGPIOEvent;
/* Coherent copies only: no GPIO refresh, bus traffic or ISR printing.
 * ReadEvents drains the bounded overwrite ring in task context. */
void SafetyIO_GetLastEvent(SafetyGPIOEvent *event);
unsigned SafetyIO_ReadEvents(SafetyGPIOEvent *events, unsigned capacity, uint32_t *overwritten);
