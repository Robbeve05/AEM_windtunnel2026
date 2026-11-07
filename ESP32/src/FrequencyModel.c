///////////////////////////////////////////////////////////////////////////////
// File information

/* FOPDT/SIMC ISA PI design, frequency feedforward and actuator decisions. */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include <math.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "FrequencyModel.h"
#include "FanCalibration.h"

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// FrequencyModel_Design()

bool FrequencyModel_Design(const FrequencyModel *m, float sample_s, RegelaarParameters *out)
{
    if (!m || !out || !m->identified_valid || !isfinite(m->gain_mps_per_hz) ||
        m->gain_mps_per_hz <= 0 || !isfinite(m->tau_s) || m->tau_s <= 0 || !isfinite(m->delay_s) ||
        m->delay_s < 0 || !isfinite(m->lambda_s) || m->lambda_s <= 0 || !isfinite(sample_s) ||
        sample_s <= 0)
        return false;
    const float horizon = m->lambda_s + m->delay_s;
    const RegelaarParameters p = {.kp = m->tau_s / (m->gain_mps_per_hz * horizon),
                                  .ti_s = fminf(m->tau_s, 4 * horizon),
                                  .sample_s = sample_s,
                                  .mode = REGELAAR_PI};
    if (!isfinite(horizon) || !isfinite(p.kp) || p.kp <= 0 || !isfinite(p.ti_s) || p.ti_s <= 0)
        return false;
    *out = p;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// FrequencyModel_Command()

bool FrequencyModel_Command(const FrequencyModel *m, bool calibrated, float base, float correction,
                            FrequencyModelOutput *out)
{
    if (!m || !out || !calibrated || !isfinite(base) || base < 0 || !isfinite(m->max_hz) ||
        m->max_hz <= 0 || base > m->max_hz || !isfinite(correction))
        return false;
    const float raw = base + correction;
    if (!isfinite(raw))
        return false;
    const float limited = fminf(m->max_hz, fmaxf(0, raw));
    *out = (FrequencyModelOutput){.feedforward_hz = base,
                                  .requested_frequency_hz = raw,
                                  .frequency_hz = limited,
                                  .saturated = raw != limited};
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// FrequencyModel_RejectIntegration()

bool FrequencyModel_RejectIntegration(const FrequencyModelOutput *out, float max_hz,
                                      float increment, bool fresh, float actual_hz)
{
    if (!out || !isfinite(increment))
        return false;
    return (increment > 0 &&
            (out->requested_frequency_hz > max_hz ||
             (fresh && isfinite(actual_hz) && out->frequency_hz > actual_hz + 0.2f))) ||
           (increment < 0 &&
            (out->requested_frequency_hz < 0 ||
             (fresh && isfinite(actual_hz) && out->frequency_hz < actual_hz - 0.2f)));
}

///////////////////////////////////////////////////////////////////////////////
// FrequencyModel_Apply()

bool FrequencyModel_Apply(const FrequencyModel *m, float reference, float correction,
                          FrequencyModelOutput *out)
{
    if (!isfinite(reference) || reference < 0)
        return false;
    /* Below the first measured point blend from zero; above the last clamp
     * to the measured endpoint, as specified by FanCalibration. */
    return FrequencyModel_Command(m, FanCalibration_IsValid(),
                                  FanCalibration_GetBaseFrequency(reference, 0), correction, out);
}
