///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * Regelaar.h
 *
 * Generic ISA controller. No plant units, hardware or I/O.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// Include guard

#pragma once

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////
// Public enumerations

typedef enum
{
    REGELAAR_NONE,
    REGELAAR_P,
    REGELAAR_PI,
    REGELAAR_PID
} RegelaarMode;

///////////////////////////////////////////////////////////////////////////////
// Public data structures

typedef struct
{
    float kp, ti_s, td_s, tf_s, sample_s;
    RegelaarMode mode;
} RegelaarParameters;
typedef struct
{
    RegelaarParameters parameters;
    float integral, integral_before_step, previous_error, previous_measurement, derivative;
    bool previous_valid, valid, integration_pending;
} Regelaar;
typedef struct
{
    float proportional, integral, derivative, value, error, integral_increment;
} RegelaarOutput;

///////////////////////////////////////////////////////////////////////////////
// Function declarations

bool Regelaar_Init(Regelaar *, const RegelaarParameters *);
bool Regelaar_SetParameters(Regelaar *, const RegelaarParameters *);
void Regelaar_Reset(Regelaar *);
bool Regelaar_Step(Regelaar *, float reference, float measurement, RegelaarOutput *);
/* Reject once, retaining error/measurement/derivative history. Re-run the model. */
bool Regelaar_RejectLatestIntegration(Regelaar *, RegelaarOutput *);
/* Compatibility boundary only: legacy wire fields remain Kp/Ki/Kd. */
bool Regelaar_FromLegacy(float kp, float ki, float kd, float tf_s, float sample_s,
                         RegelaarMode mode, RegelaarParameters *);
