///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * OtaSafety.h
 *
 * Physical maintenance authorization, no speed
 * estimate.
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

///////////////////////////////////////////////////////////////////////////////
// OtaSafety_Authorized()

static inline bool OtaSafety_Authorized(bool enabled, bool maintenance, bool isolated,
                                        const SafetySnapshot *s)
{
    return enabled && maintenance && isolated && s && s->valid && s->commissioned &&
           s->estop_pressed && !s->relay_ok && s->contactor_available && !s->contactor_ok &&
           s->stop_ok && !s->start_pressed;
}
