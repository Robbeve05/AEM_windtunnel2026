///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * StatusIndicators.c
 *
 * Indication never establishes mechanical standstill.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "StatusIndicators.h"
#include "AppConfig.h"

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// StatusIndicators_Get()

StatusIndicator StatusIndicators_Get(SystemState_t s, FaultReason_t f, bool pressed,
                                     bool commissioned, uint32_t ms)
{
    bool blink = (ms % STATUS_LED_BLINK_PERIOD_MS) < STATUS_LED_BLINK_PERIOD_MS / 2;
    if (pressed || (s == STATE_FAULT && f == FAULT_EMERGENCY_STOP))
        return (StatusIndicator){
            .red = pressed || blink,
            .text = pressed ? (commissioned ? "EMERGENCY STOP ACTIVE - MOTOR TORQUE REMOVED; "
                                              "FAN MAY STILL BE ROTATING; KEEP CLEAR"
                                            : "SAFETY CIRCUIT NOT COMMISSIONED - FAN MAY STILL "
                                              "BE ROTATING; KEEP CLEAR")
                            : "EMERGENCY STOP RELEASED - RESET REQUIRED; FAN MAY STILL "
                              "BE ROTATING"};
    switch (s)
    {
    case STATE_INIT:
        return (StatusIndicator){.amber = true, .red = true, .text = "INITIALIZING"};
    case STATE_WAIT:
        return (StatusIndicator){.amber = blink, .text = "WAITING FOR RESET / CHECKS"};
    case STATE_STOPPED_PREPARE:
        return (StatusIndicator){.amber = true, .text = "VERIFYING STOPPED DRIVE"};
    case STATE_STARTING:
        return (StatusIndicator){.green = blink, .text = "STARTING - WAIT FOR VFD"};
    case STATE_READY:
        return (StatusIndicator){.green = blink, .text = "READY - PRESS START"};
    case STATE_RUNNING:
        return (StatusIndicator){.green = true, .text = "RUNNING"};
    case STATE_STOP:
        return (StatusIndicator){.amber = true, .text = "STOPPING - WAIT FOR VFD"};
    default:
        return (StatusIndicator){.red = blink, .text = FaultReason_Name(f)};
    }
}
