///////////////////////////////////////////////////////////////////////////////
// File information

/* Frequency-domain PI regulator for the V20 frequency path. */
///////////////////////////////////////////////////////////////////////////////
// Include guard
#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    float kp_hz_per_mps;
    float ki_hz_per_mps_s;
    float sample_s;
    float integral_hz;
    float integral_before_step_hz;
    float previous_error_mps;
    bool previous_valid;
    bool ready;
    bool valid;
} FrequencyPI;

typedef struct
{
    float feedforward_hz;
    float output_hz;
    float integral_hz;
    bool valid;
} FrequencyPIOutput;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool FrequencyPI_Init(FrequencyPI *controller, float kp_hz_per_mps, float ki_hz_per_mps_s,
                      float sample_s);
void FrequencyPI_Reset(FrequencyPI *controller);
bool FrequencyPI_SetGains(FrequencyPI *controller, float kp_hz_per_mps, float ki_hz_per_mps_s);
bool FrequencyPI_Step(FrequencyPI *controller, float setpoint_mps, float measured_mps,
                      float feedforward_hz, FrequencyPIOutput *output);
void FrequencyPI_RejectLatestIntegration(FrequencyPI *controller, FrequencyPIOutput *output);
