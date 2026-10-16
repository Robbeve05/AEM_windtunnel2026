#pragma once
#include <stdbool.h>

/* Zoals MotorIOhistory_t uit de deltarobot, nu voor een ventilator.
 * PI heeft slechts een vorige fout en een integratietoestand nodig. */
typedef struct
{
    float e_1;              // vorige fout [m/s]
    float i_1;              // vorige I-bijdrage [%]
    float meting_1;         // vorige luchtsnelheid [m/s], voor optionele D-term
    float d_1;              // vorige gefilterde D-bijdrage [%]
    bool gevuld;           // eerste meting heeft nog geen afgeleide
} MotorIOhistory_t;

typedef struct
{
    float Kp, Ki, Kd;
    float Tsample, Tau_f;
    float minimum, maximum;
    float i_factor;         // Ki * Tsample / 2 (Tustin)
    float d_a, d_b;         // coefficienten van de gefilterde D-term
    MotorIOhistory_t motorHist;
} Regelaar;

bool Regelaar_INIT(Regelaar *regelaar, float Tsample);
bool Regelaar_Instellen(Regelaar *regelaar, float Kp, float Ki, float Kd,
                        float Tsample, float minimum, float maximum, float Tau_f);
void removeRegelaarHistory(Regelaar *regelaar);
/* error = gewenst - gemeten [m/s]. Uitgang in %, 100% = 50 Hz.
 * meting is alleen nodig voor de optionele D-term zonder setpoint-kick.
 * false betekent ongeldige berekening; uitgang wordt dan nul. */
bool PIDregelaar(Regelaar *regelaar, float error, float meting, float *uitgang);
