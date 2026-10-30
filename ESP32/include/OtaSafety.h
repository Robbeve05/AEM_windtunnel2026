/* Author: R.E. van Eekelen. Physical maintenance authorization, no speed estimate. */
#pragma once
#include "StateMachine.h"
static inline bool OtaSafety_Authorized(bool enabled, bool maintenance, bool isolated,
                                         const SafetySnapshot *s) {
    return enabled && maintenance && isolated && s && s->valid && s->commissioned &&
        s->estop_pressed && !s->relay_ok && s->contactor_available && !s->contactor_ok &&
        s->stop_ok && !s->start_pressed;
}
