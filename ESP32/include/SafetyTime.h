///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * SafetyTime.h
 *
 * Wrap-safe, fail-closed safety snapshot age calculations.
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
// Application includes

#include "AppConfig.h"
#include "StateMachine.h"

///////////////////////////////////////////////////////////////////////////////
// Function declarations

///////////////////////////////////////////////////////////////////////////////
// SafetyTime_Age()

static inline uint32_t SafetyTime_Age(uint32_t now, uint32_t tick)
{
    return now - tick;
}

///////////////////////////////////////////////////////////////////////////////
// SafetyTime_AgeValid()

static inline bool SafetyTime_AgeValid(uint32_t now, uint32_t tick)
{
    /* Ordering is meaningful within half the 32-bit monotonic period.
     * Future timestamps fail closed; never clamp them to age zero. */
    return SafetyTime_Age(now, tick) < UINT32_C(0x80000000);
}

///////////////////////////////////////////////////////////////////////////////
// SafetyTime_Fresh()

static inline bool SafetyTime_Fresh(const SafetySnapshot *s, uint32_t now)
{
    return s->valid && SafetyTime_AgeValid(now, s->tick_ms) &&
           SafetyTime_Age(now, s->tick_ms) < INPUT_TIMEOUT_MS;
}
