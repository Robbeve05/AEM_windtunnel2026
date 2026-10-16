/* Windtunnel PI(D), compatible with the ESP32 Regelaar API.
 * Design source: windtunnel_pi_design.py, PLANT_MODE = 'vfd_proxy'.
 * Input: airspeed [m/s]. Output: percent, 100% = 50 Hz (assumption).
 * The HAL must convert percent to the configured V20 Modbus setpoint scale;
 * percent/Hz are NOT raw Modbus register values.
 * This approximate model is not an identification of the real fan/drive.
 */
#include "Regelaar.h"
#include <math.h>
#include <stddef.h>

#define WT_PI 3.14159265358979323846f
#define WT_AIRSPEED_MS 40.0f
#define WT_REFERENCE_HZ 50.0f
#define WT_VFD_TAU_S 0.6f
#define WT_CROSSOVER_HZ 0.20f
#define WT_ZERO_DIVISOR 10.0f
#define WT_PERCENT_PER_HZ (100.0f / WT_REFERENCE_HZ)

/* Python: G(s) = (V_air/w0)*VFD_SPEED_GAIN/(tau*s+1).
 * VFD_SPEED_GAIN = w0/50, so G(s) = 0.8/(0.6*s+1) [(m/s)/Hz].
 * Kp = 1/abs((1+wi/s)*G(s)) at s=j*wc; Ki = Kp*wi.
 * No torque-to-voltage Ka*Kt, resonance model, or robot feedforward.
 * Python's command delay is zero. Drive ramps/delays require revalidation.
 */
bool Regelaar_InitWindtunnel(Regelaar *pid, float dt)
{
    const float wc = 2.0f * WT_PI * WT_CROSSOVER_HZ;
    const float wi = wc / WT_ZERO_DIVISOR;
    const float gain = WT_AIRSPEED_MS / WT_REFERENCE_HZ;
    const float x = wc * WT_VFD_TAU_S;
    const float r = wi / wc;
    const float kp_hz = sqrtf(1.0f + x*x) / (gain * sqrtf(1.0f + r*r));
    if (!isfinite(dt) || dt <= 0.0f || WT_CROSSOVER_HZ >= 0.1f / dt)
        return false;
    return Regelaar_Init(pid, kp_hz * WT_PERCENT_PER_HZ,
                         kp_hz * wi * WT_PERCENT_PER_HZ,
                         0.0f, dt, 0.0f, 100.0f, 0.0f);
}

bool Regelaar_Init(Regelaar *pid, float kp, float ki, float kd, float dt,
                   float minimum, float maximum, float filter_tau)
{
    if (pid == NULL || !isfinite(kp) || !isfinite(ki) || !isfinite(kd) ||
        !isfinite(dt) || !isfinite(minimum) || !isfinite(maximum) ||
        !isfinite(filter_tau) || kp < 0.0f || ki < 0.0f || kd < 0.0f ||
        dt <= 0.0f || minimum >= maximum || filter_tau < 0.0f)
        return false;
    *pid = (Regelaar){.kp=kp, .ki=ki, .kd=kd, .dt=dt,
        .minimum=minimum, .maximum=maximum, .filter_tau=filter_tau};
    return true;
}

void Regelaar_Reset(Regelaar *pid)
{
    if (pid == NULL) return;
    pid->integral = 0.0f;
    pid->previous_error = 0.0f;
    pid->previous_measurement = 0.0f;
    pid->derivative = 0.0f;
    pid->initialized = false;
}

bool Regelaar_Step(Regelaar *pid, float setpoint, float measurement, float *output)
{
    if (output == NULL) return false;
    *output = 0.0f;
    if (pid == NULL) return false;
    if (!isfinite(setpoint) || !isfinite(measurement)) {
        Regelaar_Reset(pid);
        return false;
    }
    const float error = setpoint - measurement;
    /* Tustin integral: I[n] = I[n-1] + Ki*dt/2*(e[n]+e[n-1]).
     * Without saturation this equals Python's sampled PI transfer function.
     * Use the caller's real sample time (currently 1 ms, not Python's 50 ms).
     */
    const float delta_i = pid->ki * (0.5f * pid->dt) *
                          (error + pid->previous_error);
    float integral = pid->integral + delta_i;
    float derivative = 0.0f;
    if (pid->initialized && pid->kd > 0.0f) {
        const float denominator = 2.0f * pid->filter_tau + pid->dt;
        derivative = (2.0f * pid->filter_tau - pid->dt) / denominator *
                     pid->derivative - (2.0f * pid->kd / denominator) *
                     (measurement - pid->previous_measurement);
    }
    const float proportional = pid->kp * error;
    float command = proportional + integral + derivative;
    if (!isfinite(error) || !isfinite(integral) || !isfinite(derivative) ||
        !isfinite(command)) {
        Regelaar_Reset(pid);
        return false;
    }
    /* Conditional integration: reject increments driving further into saturation. */
    if ((command > pid->maximum && delta_i > 0.0f) ||
        (command < pid->minimum && delta_i < 0.0f)) {
        integral = pid->integral;
        command = proportional + integral + derivative;
    }
    if (!isfinite(command)) {
        Regelaar_Reset(pid);
        return false;
    }
    *output = fminf(pid->maximum, fmaxf(pid->minimum, command));
    pid->integral = integral;
    pid->previous_error = error;
    pid->previous_measurement = measurement;
    pid->derivative = derivative;
    pid->initialized = true;
    return true;
}
