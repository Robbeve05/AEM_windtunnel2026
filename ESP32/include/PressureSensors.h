///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * PressureSensors.h
 *
 * Pitot and pressure-tap acquisition, calibration and filtered measurements.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool SensorLib_Init(void);
/* ControlTask only: one bounded read, including invalid attempt publications. */
bool SensorLib_PitotStep(PitotSample *sample);
bool SensorLib_PitotCommunicationOk(void);
bool SensorLib_TapsStep(PressureTapSample *sample);
void SensorLib_SetFilter(float hz);
float SensorLib_FilterHz(void);
void Simulation_SetOutput(float value);

typedef struct {
    uint16_t pitot_raw_count;
    uint8_t pitot_status_bits;
    float pitot_raw_pressure_pa, pitot_zero_offset_pa;
    float pitot_corrected_pressure_pa, pitot_filtered_pressure_pa;
    float pitot_pressure_pa, pitot_airspeed_ms;
    uint32_t pitot_zero_samples, pitot_sample_seq;
    bool pitot_calibrated, pitot_read_ok, pitot_valid;
    TickType_t pitot_tick;
} PitotDiagnostics;
/* Atomic snapshot: count/status are latest attempt, pressures last average. */
void SensorLib_GetPitotDiagnostics(PitotDiagnostics *out);


uint16_t SensorLib_TapsValidMask(const PressureTapSample *sample, TickType_t now);
