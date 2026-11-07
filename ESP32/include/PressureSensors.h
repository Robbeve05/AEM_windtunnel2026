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
bool SensorLib_PitotStep(Sample *sample);
bool SensorLib_PitotCommunicationOk(void);
bool SensorLib_TapsStep(Sample *sample);
void SensorLib_SetFilter(float hz);
float SensorLib_FilterHz(void);
void Simulation_SetOutput(float value);
