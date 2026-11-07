///////////////////////////////////////////////////////////////////////////////
// File information

/* Author: R.E. van Eekelen. Shared telemetry interpretation, no hardware I/O.
 */
///////////////////////////////////////////////////////////////////////////////
// Include guard
#pragma once

///////////////////////////////////////////////////////////////////////////////
// Application includes
#include "StatusIndicators.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

static inline StatusIndicator StatusOutput_Get(const Telemetry *t, bool available, uint32_t now_ms)
{
    if (!available || !t)
        return (StatusIndicator){.red = true, .text = "STATUS UNKNOWN - FAN MAY STILL BE ROTATING"};
    ///////////////////////////////////////////////////////////////////////////////
    // Function declarations

    return StatusIndicators_Get(t->state, t->fault, t->safety.estop_pressed, t->safety.commissioned,
                                now_ms);
}
