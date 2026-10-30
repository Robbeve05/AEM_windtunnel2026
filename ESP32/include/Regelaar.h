#pragma once
#include <stdbool.h>

/* MotorIOhistory_t adapted from the DeltaRobot for one fan.
 * PI needs only the previous error and integral state. */
typedef struct
{
    float e_1;              // previous error [m/s]
    float i_1;              // previous integral contribution [%]
    float meting_1;         // previous airspeed [m/s], for optional derivative term
    float d_1;              // previous filtered derivative contribution [%]
    bool gevuld;           // the first measurement has no derivative
} MotorIOhistory_t;

typedef struct
{
    float Kp, Ki, Kd;
    float Tsample, Tau_f;
    float minimum, maximum;
    float i_factor;         // Ki * Tsample / 2 (Tustin)
    float d_a, d_b;         // filtered derivative coefficients
    MotorIOhistory_t motorHist;
} Regelaar;

bool Regelaar_INIT(Regelaar *regelaar, float Tsample);
bool Regelaar_Instellen(Regelaar *regelaar, float Kp, float Ki, float Kd,
                        float Tsample, float minimum, float maximum, float Tau_f);
void removeRegelaarHistory(Regelaar *regelaar);
/* error = target - measured [m/s]. Output in %, 100% = 50 Hz.
 * meting supplies the optional derivative without setpoint kick.
 * false indicates invalid calculation; output is then zero. */
bool PIDregelaar(Regelaar *regelaar, float error, float meting, float *uitgang);

/* PID and reference feedforward share the same output saturation and anti-windup.
 * Units: both output and feedforward_pct in percent of V20 reference frequency. */
bool PIDregelaarMetFeedforward(Regelaar *regelaar, float error, float meting,
                               float feedforward_pct, float *uitgang);
