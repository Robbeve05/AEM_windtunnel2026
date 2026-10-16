#pragma once
#include <stdbool.h>
typedef struct {
    float kp, ki, kd, dt, minimum, maximum, filter_tau;
    float integral, previous_error, previous_measurement, derivative;
    bool initialized;
} Regelaar;
bool Regelaar_Init(Regelaar *pid, float kp, float ki, float kd, float dt,
                   float minimum, float maximum, float filter_tau);
void Regelaar_Reset(Regelaar *pid);
bool Regelaar_Step(Regelaar *pid, float setpoint, float measurement, float *output);

/* Theoretical VFD PI: m/s input, 0-100% output, 100% = 50 Hz. */
bool Regelaar_InitWindtunnel(Regelaar *pid, float dt);
