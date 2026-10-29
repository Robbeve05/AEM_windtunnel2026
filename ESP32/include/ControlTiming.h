/* Author: R.E. van Eekelen */
#pragma once
#include "StateMachine.h"
typedef struct { uint64_t previous_us; uint32_t max_jitter_us,missed_deadlines,overruns; } ControlTiming;
FaultReason_t ControlTiming_Tick(ControlTiming *t,uint64_t now,unsigned notifications);
bool ControlTiming_Execution(ControlTiming *t,uint32_t execution_us,uint32_t service_us);
