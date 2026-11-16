///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * FaultCodes.c
 *
 * Single registry expansion; numeric codes do not depend on enum ordering.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "FaultCodes.h"
#include "FaultCodeRegistry.h"

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// FaultCodes_ForReason()

uint16_t FaultCodes_ForReason(FaultReason_t reason)
{
    switch (reason) {
#define FAULT(reason, code, description) case reason: return code;
#define INTERLOCK(bit, code, description)
FAULT_CODE_REGISTRY(FAULT, INTERLOCK)
#undef FAULT
#undef INTERLOCK
    default: return UINT16_MAX;
    }
}

///////////////////////////////////////////////////////////////////////////////
// FaultCodes_ForInterlock()

uint16_t FaultCodes_ForInterlock(uint32_t mask)
{
    /* Select the lowest defined bit; preserve ALL bits in the record detail. */
#define FAULT(reason, code, description)
#define INTERLOCK(bit, code, description) if (mask & (bit)) return code;
FAULT_CODE_REGISTRY(FAULT, INTERLOCK)
#undef FAULT
#undef INTERLOCK
    return FaultCodes_ForReason(FAULT_INTERLOCK);
}

///////////////////////////////////////////////////////////////////////////////
// FaultCodes_KnownInterlocks()

uint32_t FaultCodes_KnownInterlocks(void)
{
    uint32_t mask = 0;
#define FAULT(reason, code, description)
#define INTERLOCK(bit, code, description) mask |= bit;
FAULT_CODE_REGISTRY(FAULT, INTERLOCK)
#undef FAULT
#undef INTERLOCK
    return mask;
}
