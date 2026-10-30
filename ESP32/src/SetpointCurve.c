/* Vijfdegraads bewegingscurve voor het luchtsnelheid-setpoint. */
#include "SetpointCurve.h"
#include <math.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// Losse rekenfuncties

/* Kies de gewenste tijd, of verleng tot versnelling en jerk binnen de grenzen vallen.
 * Voor een snelheidsetpoint is dv/dt versnelling en d2v/dt2 jerk. */
static double BerekenOvergangstijd(const SetpointCurve *curve, double verschil)
{
    const double afstand = fabs(verschil);
    const double tijd_versnelling = 1.875 * afstand / curve->max_acceleration;
    const double tijd_jerk = sqrt((10.0 / sqrt(3.0)) * afstand / curve->max_jerk);
    const double third = curve->max_third_derivative > 0 ?
        cbrt(60.0 * afstand / curve->max_third_derivative) : 0;
    return fmax(third, fmax(curve->requested_time, fmax(tijd_versnelling, tijd_jerk)));
}

/* q loopt van 0 naar 1. Waarde, versnelling en jerk sluiten aan op de eindpunten. */
static double BerekenVijfdegraadsFactor(double q)
{
    return q * q * q * (10.0 + q * (-15.0 + 6.0 * q));
}

static void StartNieuweOvergang(SetpointCurve *curve, float doel)
{
    curve->start = curve->value;
    curve->target = doel;
    curve->duration = BerekenOvergangstijd(curve, curve->target - curve->start);
    curve->elapsed = 0;
}

static void BerekenVolgendeWaarde(SetpointCurve *curve, float Tsample)
{
    curve->elapsed = fmin(curve->duration, curve->elapsed + (double)Tsample);
    const double q = curve->elapsed / curve->duration;
    const double factor = BerekenVijfdegraadsFactor(q);
    curve->value = curve->start + (curve->target - curve->start) * factor;
    if (curve->elapsed >= curve->duration)
        curve->value = curve->target;
}

///////////////////////////////////////////////////////////////////////////////
// Initialiseren bij START, vanaf de dan gemeten luchtsnelheid.
bool SetpointCurve_Init(SetpointCurve *curve, float initial, float time,
                        float acceleration, float jerk)
{
    if (curve == NULL || !isfinite(initial) || !isfinite(time) ||
        !isfinite(acceleration) || !isfinite(jerk) || time <= 0 ||
        acceleration <= 0 || jerk <= 0)
        return false;
    *curve = (SetpointCurve){.start=initial, .target=initial, .value=initial,
        .requested_time=time, .max_acceleration=acceleration, .max_jerk=jerk};
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Een sample van de bewegingscurve. STOP en fouten gaan buiten deze functie om.
bool SetpointCurve_Step(SetpointCurve *curve, float target, float dt, float *output)
{
    if (curve == NULL || output == NULL || !isfinite(target) || !isfinite(dt) ||
        dt <= 0 || !isfinite(curve->max_third_derivative) || curve->max_third_derivative < 0 ||
        curve->requested_time <= 0 || curve->max_acceleration <= 0 ||
        curve->max_jerk <= 0)
        return false;

    // Lopende beweging afmaken; daarna alleen het nieuwste doel oppakken.
    if (curve->elapsed >= curve->duration && curve->target != (double)target)
        StartNieuweOvergang(curve, target);

    if (curve->elapsed < curve->duration)
        BerekenVolgendeWaarde(curve, dt);

    if (!isfinite(curve->value))
        return false;
    *output = (float)curve->value;
    return true;
}
