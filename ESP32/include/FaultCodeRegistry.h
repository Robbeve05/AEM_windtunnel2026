///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * FaultCodeRegistry.h
 *
 * Single source for stable wire codes and generated fault dictionaries.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// Public definitions

#define FAULT_CODE_REGISTRY(FAULT, INTERLOCK) \
    FAULT(FAULT_NONE, 0x0000, "No fault") \
    FAULT(FAULT_EMERGENCY_STOP, 0x0101, "Emergency stop active") \
    FAULT(FAULT_SAFETY_FEEDBACK, 0x0102, "Safety feedback unhealthy") \
    FAULT(FAULT_VFD, 0x0201, "Drive feedback fault") \
    FAULT(FAULT_MODBUS, 0x0202, "Drive communication failure") \
    FAULT(FAULT_SENSOR, 0x0401, "Required sensor invalid or stale") \
    FAULT(FAULT_PI_CONNECTION, 0x0501, "HMI connection or heartbeat lost") \
    FAULT(FAULT_CONTROL_OVERRUN, 0x0601, "Control execution deadline exceeded") \
    FAULT(FAULT_CLOCK_TIMEOUT, 0x0602, "Control clock timeout") \
    FAULT(FAULT_INTERLOCK, 0x0300, "Safety interlock rejected") \
    FAULT(FAULT_INTERNAL, 0x0701, "Internal state invalid") \
    FAULT(FAULT_DRIVE_INHIBIT, 0x0203, "Drive preparation inhibited") \
    FAULT(FAULT_CONFIGURATION, 0x0702, "Required configuration invalid") \
    FAULT(FAULT_START_TIMEOUT, 0x0204, "Drive start confirmation timeout") \
    FAULT(FAULT_INITIALIZATION, 0x0703, "Required task initialization incomplete") \
    FAULT(FAULT_STARTUP_RESET_REQUIRED, 0x0704, "Startup complete - physical reset required") \
    INTERLOCK(SAFETY_SNAPSHOT_INVALID, 0x0301, "Safety snapshot invalid") \
    INTERLOCK(SAFETY_SNAPSHOT_STALE, 0x0302, "Safety snapshot stale or timestamp ordering invalid") \
    INTERLOCK(DIGITAL_OUTPUT_UNHEALTHY, 0x0303, "Digital output acknowledgement unhealthy") \
    INTERLOCK(SAFETY_NOT_COMMISSIONED, 0x0304, "Safety I/O not commissioned") \
    INTERLOCK(SAFETY_RELAY_UNHEALTHY, 0x0305, "Safety relay feedback unhealthy") \
    INTERLOCK(VFD_FEEDBACK_UNHEALTHY, 0x0306, "VFD feedback unhealthy") \
    INTERLOCK(CONTACTOR_FEEDBACK_UNHEALTHY, 0x0307, "Contactor feedback unhealthy")
