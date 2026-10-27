/* Author: R.E. van Eekelen */
#pragma once
#include "TaskData.h"
/* Only ControlTask on Core 1 may call these methods. */
bool SensorLib_Init(void);
bool SensorLib_PitotStep(Sample *sample);
/* Only SensorTask on Core 0 may read the remaining pressure taps. */
bool SensorLib_TapsStep(Sample *sample);
void SensorLib_SetFilter(float hz);
float SensorLib_FilterHz(void);
void Simulation_SetOutput(float value);
