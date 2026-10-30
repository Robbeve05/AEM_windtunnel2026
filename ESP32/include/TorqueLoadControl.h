/* Model-based load-side controller, output in Nm. Author R.E. van Eekelen.
 * NOT a direct Siemens V20 command. */
#pragma once
#include <stdbool.h>
#include "RegelaarDesign.h"
typedef struct {
  RegelaarDesignOutput design;
  float sample_s, integral_nm, previous_error_ms;
  float previous_measurement_ms, derivative_nm, previous_reference_ms;
  bool previous_valid;
} TorqueLoadControl;
typedef struct {
  float pid_nm, acceleration_nm, fan_load_nm, total_nm;
  float omega_est_rad_s;
} TorqueLoadOutput;
bool TorqueLoadControl_Init(TorqueLoadControl *c,
                             const RegelaarDesignOutput *design, float sample_s);
bool TorqueLoadControl_Step(TorqueLoadControl *c, float reference_ms,
                            float measured_ms, float reference_accel_ms2,
                            float fan_load_coefficient, TorqueLoadOutput *out);
void TorqueLoadControl_Reset(TorqueLoadControl *c);
