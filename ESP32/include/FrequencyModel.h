///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * FrequencyModel.h
 *
 * Frequency plant and calibrated feedforward; corrections are in Hz.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "Regelaar.h"

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    float gain_mps_per_hz, tau_s, delay_s, lambda_s, max_hz;
    bool identified_valid, commissioned;
} FrequencyModel;
typedef struct
{
    float feedforward_hz, requested_frequency_hz, frequency_hz;
    bool saturated;
} FrequencyModelOutput;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool FrequencyModel_Design(const FrequencyModel *, float sample_s, RegelaarParameters *);
bool FrequencyModel_Command(const FrequencyModel *, bool calibration_valid, float base_hz,
                            float correction_hz, FrequencyModelOutput *);
bool FrequencyModel_RejectIntegration(const FrequencyModelOutput *, float max_hz, float increment,
                                      bool feedback_fresh, float actual_hz);
bool FrequencyModel_Apply(const FrequencyModel *, float reference, float correction,
                          FrequencyModelOutput *);
