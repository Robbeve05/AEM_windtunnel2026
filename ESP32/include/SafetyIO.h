/* Author: R.E. van Eekelen. Diagnostic inputs only. */
#pragma once
#include "StateMachine.h"
void SafetyIO_Init(void);
SafetySnapshot SafetyIO_Read(void);
FaultReason_t SafetyIO_Fault(void);
bool SafetyIO_Stop(void);
void SafetyIO_Acknowledge(void);
bool SafetyIO_Inhibited(void);
bool SafetyIO_Simulate(const char *name, bool active);

///////////////////////////////////////////////////////////////////////////////
// Task-context button notification interface

/* Called only by InputHandlerTask after debouncing and publishing its snapshot. */
void SafetyIO_PublishButtonEvents(const SafetySnapshot *current, const SafetySnapshot *previous);
