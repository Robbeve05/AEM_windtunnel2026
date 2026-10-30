/* Author: R.E. van Eekelen */
#pragma once
#include "TaskData.h"
bool SensorLib_Init(void);
bool SensorLib_PitotStep(Sample *sample);
bool SensorLib_TapsStep(Sample *sample);
void SensorLib_SetFilter(float hz);
float SensorLib_FilterHz(void);
void Simulation_SetOutput(float value);
