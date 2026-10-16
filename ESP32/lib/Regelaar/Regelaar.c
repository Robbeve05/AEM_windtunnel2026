/*
 * Regelaar.c
 *
 * Oorspronkelijke opzet: R.E. van Eekelen, deltarobot (10/06/2026).
 * Aangepast voor de windtunnel: luchtsnelheid [m/s] -> motoropdracht [%].
 * Berekening uit windtunnel_pi_design.py, stand 'vfd_proxy'.
 */
#include "Regelaar.h"

///////////////////////////////////////////////////////////////////////////////
// system includes
#include <math.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// REGELAAR constants - dezelfde aannames als het Python VFD-model
#define PI                  (3.14159265358979323846f)
#define LUCHTSNELHEID_NOM    (40.0f)  // m/s bij de referentiefrequentie
#define FREQUENTIE_NOM      (50.0f)  // Hz, komt overeen met 100% uitgang
#define Tau_motor           (0.6f)   // s, aangenomen VFD/ventilator-tijdconstante
#define FbM                 (0.20f)  // Hz, gewenste bandbreedte
#define PI_ZERO_DIVISOR     (10.0f)  // PI-nulpunt een decade onder de bandbreedte

///////////////////////////////////////////////////////////////////////////////
// Losse rekenfuncties; uitsluitend gebruikt binnen dit bestand (static)

/* Vervangt de robotberekening met Je1M, Je2M, K12M en Ka*Kt.
 * G(s) = Kplant / (Tau_motor*s + 1), met ingang Hz en uitgang m/s.
 * Kies Kp zodat |C(j*wbM)*G(j*wbM)| = 1 bij de gewenste bandbreedte.
 */
static void BerekenWindtunnelParameters(float *Kp, float *Ki)
{
    const float wbM = 2.0f * PI * FbM;
    const float wi = wbM / PI_ZERO_DIVISOR;
    const float Tau_i = 1.0f / wi;
    const float Kplant = LUCHTSNELHEID_NOM / FREQUENTIE_NOM;
    const float motor_term = wbM * Tau_motor;
    const float pi_term = wi / wbM;
    const float Kp_Hz = sqrtf(1.0f + motor_term * motor_term) /
                       (Kplant * sqrtf(1.0f + pi_term * pi_term));

    // Eerst gains in Hz, daarna omrekenen naar de bestaande procentuitgang.
    *Kp = Kp_Hz * (100.0f / FREQUENTIE_NOM);
    *Ki = *Kp / Tau_i;
}

/* Eenmalig berekenen, net als de a- en b-coefficienten in het oude ontwerp. */
static bool BerekenTustinCoefficienten(Regelaar *regelaar)
{
    regelaar->i_factor = regelaar->Ki * (0.5f * regelaar->Tsample);
    const float noemer = 2.0f * regelaar->Tau_f + regelaar->Tsample;
    regelaar->d_a = (2.0f * regelaar->Tau_f - regelaar->Tsample) / noemer;
    regelaar->d_b = 2.0f * regelaar->Kd / noemer;
    return isfinite(regelaar->i_factor) && isfinite(regelaar->d_a) &&
           isfinite(regelaar->d_b);
}

/* Tustin: delta_I = Ki*Tsample/2 * (e[n] + e[n-1]). */
static float BerekenIntegratieStap(const Regelaar *regelaar, float error)
{
    return regelaar->i_factor * (error + regelaar->motorHist.e_1);
}

/* Afgeleide van de meting, zodat een setpointsprong geen D-piek veroorzaakt.
 * De windtunnel gebruikt standaard Kd=0; dan is deze bijdrage altijd nul. */
static float BerekenDTerm(const Regelaar *regelaar, float meting)
{
    const MotorIOhistory_t *hist = &regelaar->motorHist;
    if (!hist->gevuld || regelaar->Kd == 0.0f)
        return 0.0f;
    return regelaar->d_a * hist->d_1 - regelaar->d_b * (meting - hist->meting_1);
}

/* Geen extra integratie als die de uitgang verder buiten zijn grens duwt. */
static bool IntegratieToegestaan(const Regelaar *regelaar, float u_0, float delta_i)
{
    if (u_0 > regelaar->maximum && delta_i > 0.0f)
        return false;
    if (u_0 < regelaar->minimum && delta_i < 0.0f)
        return false;
    return true;
}

static float BegrensUitgang(const Regelaar *regelaar, float u_0)
{
    if (u_0 > regelaar->maximum)
        return regelaar->maximum;
    if (u_0 < regelaar->minimum)
        return regelaar->minimum;
    return u_0;
}

static void BewaarRegelaarHistory(Regelaar *regelaar, float error, float meting,
                                 float i_0, float d_0)
{
    MotorIOhistory_t *hist = &regelaar->motorHist;
    hist->e_1 = error;
    hist->i_1 = i_0;
    hist->meting_1 = meting;
    hist->d_1 = d_0;
    hist->gevuld = true;
}

///////////////////////////////////////////////////////////////////////////////
// bool Regelaar_INIT(Regelaar *regelaar, float Tsample)
/* Berekent eenmalig de windtunnel-PI en wist de historie.
 * Tsample komt uit ControlTask: 0,001 s. Python gebruikt standaard 0,05 s.
 * De modelparameters zijn aannames, geen gemeten afstelling van de ventilator.
 */
bool Regelaar_INIT(Regelaar *regelaar, float Tsample)
{
    if (!isfinite(Tsample) || Tsample <= 0.0f || FbM >= 0.1f / Tsample)
        return false;
    float Kp, Ki;
    BerekenWindtunnelParameters(&Kp, &Ki);
    return Regelaar_Instellen(regelaar, Kp, Ki, 0.0f, Tsample, 0.0f, 100.0f, 0.0f);
}

///////////////////////////////////////////////////////////////////////////////
// bool Regelaar_Instellen(...)
/* Voor handmatige PID-waarden vanuit de Pi en voor de tests.
 * Pas de bestaande regelaar alleen aan als alle nieuwe waarden geldig zijn. */
bool Regelaar_Instellen(Regelaar *regelaar, float Kp, float Ki, float Kd,
                        float Tsample, float minimum, float maximum, float Tau_f)
{
    if (regelaar == NULL || !isfinite(Kp) || !isfinite(Ki) || !isfinite(Kd) ||
        !isfinite(Tsample) || !isfinite(minimum) || !isfinite(maximum) ||
        !isfinite(Tau_f) || Kp < 0.0f || Ki < 0.0f || Kd < 0.0f ||
        Tsample <= 0.0f || minimum >= maximum || Tau_f < 0.0f)
        return false;

    Regelaar nieuw = {.Kp=Kp, .Ki=Ki, .Kd=Kd, .Tsample=Tsample,
        .minimum=minimum, .maximum=maximum, .Tau_f=Tau_f};
    if (!BerekenTustinCoefficienten(&nieuw))
        return false;
    removeRegelaarHistory(&nieuw);
    *regelaar = nieuw;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// bool PIDregelaar(Regelaar *regelaar, float error, float meting, float *uitgang)
/* Een regelstap: P/I/D berekenen, anti-windup, begrenzen, historie bewaren.
 * Zonder begrenzing is deze PI gelijk aan de oude soort verschilvergelijking:
 * u[n] = u[n-1] + (Kp+Ki*Tsample/2)*e[n] + (-Kp+Ki*Tsample/2)*e[n-1].
 * P en I apart bewaren maakt anti-windup mogelijk. e_2/u_2 zijn voor PI niet nodig.
 */
bool PIDregelaar(Regelaar *regelaar, float error, float meting, float *uitgang)
{
    if (uitgang == NULL)
        return false;
    *uitgang = 0.0f;
    if (regelaar == NULL)
        return false;
    if (!isfinite(error) || !isfinite(meting))
    {
        removeRegelaarHistory(regelaar);
        return false;
    }

    // 1. Regelbijdragen bepalen.
    const float p_0 = regelaar->Kp * error;
    const float delta_i = BerekenIntegratieStap(regelaar, error);
    float i_0 = regelaar->motorHist.i_1 + delta_i;
    const float d_0 = BerekenDTerm(regelaar, meting);
    float u_0 = p_0 + i_0 + d_0;
    if (!isfinite(i_0) || !isfinite(d_0) || !isfinite(u_0))
    {
        removeRegelaarHistory(regelaar);
        return false;
    }

    // 2. Bij verzadiging de extra integratie zo nodig terugnemen.
    if (!IntegratieToegestaan(regelaar, u_0, delta_i))
    {
        i_0 = regelaar->motorHist.i_1;
        u_0 = p_0 + i_0 + d_0;
    }
    if (!isfinite(u_0))
    {
        removeRegelaarHistory(regelaar);
        return false;
    }

    // 3. Uitgang begrenzen en historie doorschuiven.
    *uitgang = BegrensUitgang(regelaar, u_0);
    BewaarRegelaarHistory(regelaar, error, meting, i_0, d_0);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// void removeRegelaarHistory(Regelaar *regelaar)
/* Een ventilator heeft een historieblok; geen arm-index of N_MOTORS nodig. */
void removeRegelaarHistory(Regelaar *regelaar)
{
    if (regelaar == NULL)
        return;
    regelaar->motorHist = (MotorIOhistory_t){0};
}
