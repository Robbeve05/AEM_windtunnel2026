/* Author: R.E. van Eekelen */
#pragma once
#include "StateMachine.h"
typedef struct { bool red, amber, green; const char *text; } StatusIndicator;
StatusIndicator StatusIndicators_Get(SystemState_t state,FaultReason_t fault,
    bool estop_pressed,bool commissioned,uint32_t now_ms);
