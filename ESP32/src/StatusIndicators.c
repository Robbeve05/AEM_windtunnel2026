/* Author: R.E. van Eekelen. Indication never establishes mechanical standstill. */
#include "StatusIndicators.h"
StatusIndicator StatusIndicators_Get(SystemState_t s,FaultReason_t f,bool pressed,bool commissioned,uint32_t ms) {
    bool blink=(ms%1000)<500;
    if (s==STATE_FAULT && f==FAULT_EMERGENCY_STOP)
        return (StatusIndicator){.red=pressed || blink,.text=pressed ?
            (commissioned ? "EMERGENCY STOP ACTIVE - MOTOR TORQUE REMOVED; FAN MAY STILL BE ROTATING; KEEP CLEAR" :
            "SAFETY CIRCUIT NOT COMMISSIONED - FAN MAY STILL BE ROTATING; KEEP CLEAR") :
            "EMERGENCY STOP RELEASED - RESET REQUIRED; FAN MAY STILL BE ROTATING"};
    switch(s) {
    case STATE_INIT: return (StatusIndicator){.amber=true,.red=true,.text="INITIALIZING"};
    case STATE_WAIT: return (StatusIndicator){.amber=blink,.text="WAITING FOR RESET / CHECKS"};
    case STATE_READY: return (StatusIndicator){.green=blink,.text="READY - PRESS START"};
    case STATE_RUNNING: return (StatusIndicator){.green=true,.text="RUNNING"};
    case STATE_STOP: return (StatusIndicator){.amber=true,.text="STOPPING - WAIT FOR VFD"};
    default: return (StatusIndicator){.red=true,.text=FaultReason_Name(f)};
    }
}
