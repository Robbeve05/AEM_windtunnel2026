#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Alleen SensorTask gebruikt de sensor; alleen ActuatorTask de motorinterface.
 * Echte drivers moeten begrensde time-outs gebruiken. */
bool SensorLib_Init(void);
bool SensorLib_Read(float *velocity_mps);
bool MotorLib_Init(void);
bool MotorLib_Write(float percentage);
void Simulation_SetOutput(float percentage);
