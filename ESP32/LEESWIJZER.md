> Industriële migratie: lees eerst [MIGRATION.md](MIGRATION.md). Onderstaande uitleg van de regelaar blijft bruikbaar; oude externe-klok/UART/DevKit-instructies zijn vervangen.

# Leeswijzer: van deltarobot naar windtunnel

Begin bij `src/Regelaar.c`. De indeling volgt je oude bestand: includes, constanten, rekenfuncties, initialisatie, regelstap en historie wissen.

## Dezelfde gedachte als vroeger

| Deltarobot | Windtunnel |
| --- | --- |
| `Regelaar_INIT()` | `Regelaar_INIT(&pid, Tsample)` berekent de gains en Tustin-coefficienten eenmalig |
| `motorHist[arm]` | `pid.motorHist`: een historieblok voor de ventilator |
| `PIDregelaar(motorIndex, error)` | `PIDregelaar(&pid, error, meting, &uitgang)` voert een regelstap uit |
| `removeRegelaarHistory(arm)` | `removeRegelaarHistory(&pid)` wist de historie bij stoppen/fouten |
| Fout in radialen | Fout in m/s |
| Uitgang in volt | Uitgang in procenten, 100% = 50 Hz |
| Resonantie, Je1M/Je2M/K12M, Ka*Kt | Aangenomen VFD-model: tijdconstante en luchtsnelheid per Hz |

`pid` bevat instellingen en historie. `&pid` geeft de functie toegang tot datzelfde blok, zodat de vorige fout en integratie tussen aanroepen bewaard blijven. Er is geen motorindex nodig: de windtunnel heeft hier een ventilator.

De regelstap geeft `true` of `false` terug om een rekenfout te kunnen melden. De berekende motoropdracht wordt in `uitgang` geschreven via `&uitgang`. Dit is de reden dat er nu een uitgangsargument bij staat. De meting is alleen nodig voor een optionele D-term zonder piek bij een setpointwijziging; de standaard windtunnelregeling heeft Kd=0.

## Regelcyclus in ControlTask.c

```c
// Eenmalig bij het starten van de software:
Regelaar_INIT(&pid, CONTROL_CLOCK_PERIOD_S);

// Iedere kloktik, nadat sensoren en toestand zijn gecontroleerd:
SetpointCurve_Step(&curve, gewenst, CONTROL_CLOCK_PERIOD_S, &setpoint);
float error = setpoint - meting;
PIDregelaar(&pid, error, meting, &uitgang);

// Bij stoppen of een fout:
removeRegelaarHistory(&pid);
```

Dit is een verkort leesvoorbeeld. De werkelijke code controleert iedere returnwaarde en initialiseert de curve bij START. STOP omzeilt de curve. De motoropdracht gaat via de queue naar ActuatorTask; de regelaar zelf doet geen Modbus-I/O.

## Wat doet iedere losse regelfunctie?

1. `BerekenWindtunnelParameters`: berekent Kp en Ki uit bandbreedte en ventilatormodel. Hier zie je ook Tau_i.
2. `BerekenTustinCoefficienten`: berekent eenmalig de factoren voor de I- en optionele D-term.
3. `BerekenIntegratieStap`: berekent hoeveel er deze sample bij de I-term komt.
4. `BerekenDTerm`: berekent de gefilterde afgeleide van de meting; standaard nul.
5. `IntegratieToegestaan`: voorkomt dat de integrator verder oploopt bij verzadiging.
6. `BegrensUitgang`: houdt de opdracht tussen minimum en maximum.
7. `BewaarRegelaarHistory`: schuift de huidige waarden door voor de volgende sample.

`static` voor deze hulpfuncties betekent dat ze alleen binnen Regelaar.c worden gebruikt. De functies zonder `static` staan in Regelaar.h en worden vanuit ControlTask aangeroepen. `Regelaar_Instellen` is bedoeld voor handmatige PID-gains vanuit de Pi; bij normaal opstarten wordt Regelaar_INIT gebruikt.

## Waar is de oude verschilvergelijking?

Je oude PID gebruikte twee vorige fouten en twee vorige uitgangen. Een PI heeft minder historie nodig. Zonder begrenzing geldt:

```
u[n] = u[n-1] + (Kp + Ki*Tsample/2)*e[n]
              + (-Kp + Ki*Tsample/2)*e[n-1]
```

De code bewaart P en I apart zodat de extra integratie bij verzadiging kan worden teruggenomen. Dit is dezelfde Tustin-PI in het onbegrensde geval. De oude robot-feedforward en graden/radialen-omrekening horen niet bij dit ventilatormodel.

## Bewegingscurve en Modbus

`src/SetpointCurve.c` heeft nu losse functies voor de overgangstijd, de vijfdegraads factor, het starten van een overgang en de volgende waarde. De drie instelwaarden staan onderaan AppConfig.h.

`ActuatorTask` geeft de procentopdracht aan `MotorLib_Write`. `V20Modbus.c` maakt daar drive-opdrachten van, controleert antwoorden en verwerkt storingen. `V20Transport.c` verstuurt de bytes over RS485. Deze communicatie hoeft niet in de wiskunde van Regelaar.c te staan.
