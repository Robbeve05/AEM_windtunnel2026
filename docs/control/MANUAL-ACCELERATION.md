# Handmatige acceleratie: software en V20 afzonderlijk

Datum: 2026-11-09. Geen upload of fysieke motortest uitgevoerd.

## Software

`AppConfig.h`: MANUAL_TRANSITION_TIME_S=5.0, maximale slew=20 Hz/s,
acceleratie=12 Hz/s², jerk=25 Hz/s³. MANUAL_MAX_HZ en V20_REFERENCE_HZ
blijven 50 Hz. `ControlTask.c` gebruikt de eigen handmatige overgangstijd;
AUTO-instellingen en de veilige state-machine zijn niet aangepast.

De bestaande vijfdegraadsfunctie blijft s(q)=10q³-15q⁴+6q⁵.
De segmentduur blijft het maximum van de gevraagde tijd en:

- 1.875 |delta Hz| / maximale slew;
- sqrt((10/sqrt(3)) |delta Hz| / maximale acceleratie);
- cbrt(60 |delta Hz| / maximale jerk).

Voor 0→50 Hz: T=5.000 s, pieken 18.750 Hz/s, 11.547 Hz/s²,
24.000 Hz/s³. Alleen de nieuwe grenzen zouden T≈4.905 s geven; de
expliciete minimumduur maakt dit 5.000 s. Voor 50→20 Hz: T=5.000 s,
pieken 11.250 Hz/s, 6.928 Hz/s² en 14.400 Hz/s³. Beide segmenten zijn
monotoon. Dezelfde begrenzingsformules blijven gelden bij afregelen.
De vorige configuratie (10/1/0.5) gaf voor 0→50 circa 16.990 s en voor
50→20 circa 13.161 s; die oude tijden zijn geen behoudseis.

`SetpointCurve.c` rondt het tijdseindpunt af binnen floatprecisie (bij 5 s
maximaal 0.60 microseconde). Daardoor leiden 500 float-tijdstappen van
0.01 s niet tot een onbedoelde 501e cyclus. De polynoom blijft ongewijzigd.

Doelwijzigingen blijven uitgesteld tot het huidige segment klaar is; alleen
het nieuwste doel wordt daarna uitgevoerd. Dus 50→20 aanvragen tijdens
0→50 kan de totale wachttijd langer dan 5 s maken. Dit bewaart de bestaande
C2-aansluiting. STOP en FAULT wachten niet op het einde van de curve:
STOP gebruikt bestaande OFF1-afhandeling met gezonde permissive; FAULT
verwijdert permissive en behoudt de bestaande coast/stopafhandeling.
Noodstop, OFF2, OFF3, timeouts, retries en reset/startlogica zijn ongewijzigd.

## V20-instellingen: afzonderlijke controle

De actuele aangesloten V20 is niet uitgelezen. Er is geen geverifieerd
parameterexportbestand aangetroffen. P1120/P1121 zijn dus **onbekend**.
De firmware leest status, referentie en watchdoginformatie, maar geen
rampinstellingen; bootstrap schrijft deze rampinstellingen ook niet.
`V20Modbus.c` blijft ongewijzigd. De referentievalidatie op 50 Hz, de
begrenzing van HSW en de bestaande besturingswoorden blijven behouden.
De bestaande `V20Simulation.c` gebruikt illustratief 5 Hz/s voor normaal
bedrijf/OFF1; dat is geen uitlezing of claim over de echte V20.

Lees bij een afzonderlijke installatiecontrole het actieve gegevenset uit:
P1082 (maximumfrequentie), P2000 (referentie), P1120 (optijd), P1121
(normale aftijd), P1130–P1134 (afronding en bijbehorende configuratie) en
P1135 (bestaande OFF3-aftijd, uitsluitend vastleggen). Leg ook eventuele
stroombegrenzing, DC-link/Vdc-regeling en rampverlenging vast. Wijzig geen
parameters als onderdeel van deze softwarecorrectie.

Volgens de [Siemens V20-handleiding, 12/2024](https://cache.industry.siemens.com/dl/files/897/109977897/att_1309600/v1/V20_op_instr_1224_en-US.pdf)
definieert P1120 zonder afronding de tijd van stilstand tot P1082; P1121
definieert de omgekeerde normale ramp. Bij aangenomen P1082=50 Hz is de
lineaire optoergrens dus 50/P1120 Hz/s. Om de piekslew van de softwarecurve
zonder slewbeperking te volgen is P1120≤2.667 s nodig. Dit is een afleiding
voor het model, geen opdracht om die parameter te wijzigen.

## Gesimuleerde motorfrequentie

`test_manual_ramp.c` koppelt de productiecurve aan een afzonderlijke
lineaire V20-rampbegrenzer, met P1082=P2000=50 Hz. Integratie 1 ms,
softwarecyclus 10 ms, 14-bit HSW-kwantisatie. Commandoupdates zijn ofwel
iedere integratiestap (de actuele 10 ms softwarewaarde) of iedere 100 ms,
met vasthouden tot de volgende update. De 100 ms variant sluit aan op de
huidige cyclische V20-commandoperiode, maar modelleert geen extra UART-
transactieduur, schedulerjitter, retries of feedbacklatentie.

| Aangenomen P1120 | Software 0→50 | Simulatie 50 Hz, fijn aangeboden | Simulatie 50 Hz, 100 ms updates | Gesimuleerde Hz op t=5 s, 100 ms |
|---|---:|---:|---:|---:|
| 2 s | 5.000 s | 4.931 s | 5.001 s | 49.997 |
| 5 s | 5.000 s | 5.744 s | 5.834 s | 41.663 |
| 10 s | 5.000 s | 10.495 s | 10.583 s | 22.087 |
| 20 s | 5.000 s | 20.340 s | 20.430 s | 11.427 |

Het eerste bereiken van exact 50 Hz kan bij fijn aanbieden eerder dan 5 s
vallen doordat HSW de laatste fractie naar 50 Hz afrondt; de interne curve
blijft 5.000 s duren. Dit is geen overshoot. Tijdresolutie simulatie: 1 ms.
Deze cascade is een vereenvoudigde frequentiesimulatie, geen gemeten
motorfrequentie of mechanisch toerental. Geen rampafronding, slip, belasting,
inertie of stroombegrenzing gemodelleerd. Werkelijke optoertijd blijft
onbekend totdat de V20 is uitgelezen en actual_hz tijdens een proef is gelogd.

## Regressies

- Nieuwe `manual_ramp` hosttest: exact 500 cycli, 0→50 en 50→20,
  analytische controle van drie afgeleiden, geen overshoot, nieuwste doel
  na voltooiing van de lopende curve en cascadesimulaties.
- Uitgebreide `test_control.c`: productieroute in beide controllerbuilds,
  5 s eindpunten, monotoniciteit, 50 Hz commandolimiet, afwijzen van 51 Hz,
  STOP en communicatie-FAULT tijdens een actieve curve.
- Bestaande curve-, V20-, recovery-, OFF1/2/3- en state-machinechecks
  blijven onderdeel van de volledige hostsuite.

Uitvoeren: `& ESP32/tests/run_host_tests.ps1`.
Resultaatlog: `output/manual-acceleration-host-tests.log`.

## Uitgevoerde validatie

Hardwarebuild `waveshare_s3` en simulatiebuild `waveshare_s3_sim`: beide
succesvol, zonder upload. Nieuwe gerichte regressies slagen voor torque- en
frequentiecontroller. `git diff --check` geeft geen problemen.
De simulatie-uitvoer staat ook in `output/manual-ramp-simulation.log`;
firmwarebuilds in `output/manual-acceleration-build.log`.

Volledige hostsuite: **30/30 executables geslaagd**.
