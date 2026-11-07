#include "FrequencyPI.h"
#include "AppConfig.h"
#include <math.h>
#include <string.h>

bool FrequencyPI_Init(FrequencyPI *controller,
                      float kp_hz_per_mps,
                      float ki_hz_per_mps_s,
                      float sample_s)
{
    if (!controller || !isfinite(kp_hz_per_mps) || !isfinite(ki_hz_per_mps_s) ||
        !isfinite(sample_s) || sample_s <= 0.0f ||
        kp_hz_per_mps < 0.0f || ki_hz_per_mps_s < 0.0f)
        return false;

    memset(controller, 0, sizeof(*controller));
    controller->kp_hz_per_mps = kp_hz_per_mps;
    controller->ki_hz_per_mps_s = ki_hz_per_mps_s;
    controller->sample_s = sample_s;
    controller->ready = true;
    controller->valid = true;
    return true;
}

void FrequencyPI_Reset(FrequencyPI *controller)
{
    if (!controller)
        return;
    controller->integral_hz = 0.0f;
    controller->integral_before_step_hz = 0.0f;
    controller->previous_error_mps = 0.0f;
    controller->previous_valid = false;
    controller->valid = controller->ready;
}

bool FrequencyPI_SetGains(FrequencyPI *controller,
                          float kp_hz_per_mps,
                          float ki_hz_per_mps_s)
{
    if (!controller || !isfinite(kp_hz_per_mps) || !isfinite(ki_hz_per_mps_s) ||
        kp_hz_per_mps < 0.0f || ki_hz_per_mps_s < 0.0f ||
        controller->sample_s <= 0.0f)
        return false;

    controller->kp_hz_per_mps = kp_hz_per_mps;
    controller->ki_hz_per_mps_s = ki_hz_per_mps_s;
    FrequencyPI_Reset(controller);
    return true;
}

bool FrequencyPI_Step(FrequencyPI *controller,
                      float setpoint_mps,
                      float measured_mps,
                      float feedforward_hz,
                      FrequencyPIOutput *output)
{
    if (!controller || !output || !controller->ready || !isfinite(setpoint_mps) ||
        !isfinite(measured_mps) || !isfinite(feedforward_hz) ||
        controller->sample_s <= 0.0f || measured_mps < 0.0f)
        return false;

    output->feedforward_hz = feedforward_hz;
    output->output_hz = 0.0f;
    output->integral_hz = controller->integral_hz;
    output->valid = false;

    const float error = setpoint_mps - measured_mps;
    if (!isfinite(error))
        return false;

    controller->integral_before_step_hz = controller->integral_hz;
    const float integral_term = controller->previous_valid ?
        controller->ki_hz_per_mps_s * controller->sample_s * 0.5f *
            (error + controller->previous_error_mps) : 0.0f;
    const float next_integral = controller->integral_hz + integral_term;

    if (!isfinite(next_integral))
        return false;

    float raw_hz = feedforward_hz + controller->kp_hz_per_mps * error + next_integral;
    if (!isfinite(raw_hz))
        return false;

    const float max_hz = MANUAL_MAX_HZ;
    if (raw_hz < 0.0f)
        raw_hz = 0.0f;
    else if (raw_hz > max_hz)
        raw_hz = max_hz;

    controller->integral_hz = next_integral;
    controller->previous_error_mps = error;
    controller->previous_valid = true;
    controller->valid = true;
    output->feedforward_hz = feedforward_hz;
    output->integral_hz = controller->integral_hz;
    output->output_hz = raw_hz;
    output->valid = true;
    return true;
}

void FrequencyPI_RejectLatestIntegration(FrequencyPI *controller,
                                        FrequencyPIOutput *output)
{
    if (!controller || !output || !controller->previous_valid)
        return;

    const float rejected = controller->integral_hz - controller->integral_before_step_hz;
    controller->integral_hz = controller->integral_before_step_hz;
    output->integral_hz = controller->integral_hz;
    output->output_hz = output->output_hz - rejected;
    if (output->output_hz < 0.0f)
        output->output_hz = 0.0f;
    else if (output->output_hz > MANUAL_MAX_HZ)
        output->output_hz = MANUAL_MAX_HZ;
}
