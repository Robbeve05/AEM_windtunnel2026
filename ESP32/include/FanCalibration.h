///////////////////////////////////////////////////////////////////////////////
// File information

/* Public fan-calibration storage and control-gate interface. */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// Public definitions

#define FAN_CAL_VERSION 1
#define FAN_CAL_MAX_POINTS 32

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    bool valid;
    uint16_t points, version;
    uint32_t crc;
} FanCalibrationStatus;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool FanCalibration_Init(void);
/* Called once per control cycle before consuming START. */
bool FanCalibration_ControlGate(bool safe_stopped);
bool FanCalibration_IsValid(void);
float FanCalibration_GetBaseFrequency(float velocity, float default_hz);
bool FanCalibration_HandleCommand(const char *, bool ready, bool stopped, char *, size_t);
void FanCalibration_GetStatus(FanCalibrationStatus *);
