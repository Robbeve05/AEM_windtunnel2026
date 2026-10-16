# Windtunnel PI-controller - ESP32 / FreeRTOS

De ESP32 regelt luchtsnelheid; de Raspberry Pi stuurt setpoints en start/stop via UART1. De Siemens SINAMICS V20 6SL3210-5BE25-5UV1 ontvangt de motoropdracht via Modbus RTU/RS485. Zie [V20_MODBUS.md](V20_MODBUS.md) voor bedrading, commissioning, registers en beperkingen.

## Software

| Onderdeel | Functie |
| --- | --- |
| `src/ControlTask.c` | Regellus, start/stop en foutafhandeling op externe 1 kHz-klok |
| `lib/Regelaar` | VFD-PI-ontwerp uit Python, Tustin-integratie, begrenzing en anti-windup |
| `src/ActuatorTask.c` | Motoropdracht iedere 50 ms, expliciete run/stop-vlag en versheidscontrole |
| `lib/V20Modbus` | RTU-frames, CRC, registermapping, drivestatus en foutvergrendeling |
| `lib/WindtunnelHAL/V20Transport.c` | UART2/RS485-transport met begrensde wachttijden |
| `lib/WindtunnelHAL/WindtunnelHAL.c` | Simulator en nog in te vullen echte sensordriver |
| `src/PiCommunicationTask.c` | UART1-protocol en statusrapportage |
| `tests` | Uitvoerbare C-tests voor PI, Pi-protocol en gesimuleerde Modbus-antwoorden |

PlatformIO: `espressif32@6.12.0`, Arduino-ESP32 2.0.17, klassieke `esp32dev`. Arduino start alleen de applicatie; taken en libraries zijn C/FreeRTOS.

## Bouwen

```sh
pio run -e esp32dev
pio run -e esp32dev_modbus
```

`esp32dev` is de standaard en simuleert zowel sensor als motor. `esp32dev_modbus` bouwt de echte Modbus-driver; de ontbrekende echte sensordriver blokkeert START. Geen simulatiesensor wordt aan een echte motor gekoppeld. Upload pas de bedoelde variant met bijvoorbeeld `pio run -e esp32dev -t upload --upload-port COM5`.

Hosttests op Windows met Visual Studio C++ tools:

```powershell
./tests/run_host_tests.ps1
```

Met GCC:

```sh
gcc -std=c11 -Wall -Wextra -Ilib/Regelaar -Ilib/PiProtocol tests/test_core.c lib/Regelaar/Regelaar.c lib/PiProtocol/PiProtocol.c -lm -o core_tests
./core_tests
gcc -std=c11 -Wall -Wextra -Ilib/V20Modbus tests/test_v20.c lib/V20Modbus/V20Modbus.c -lm -o v20_tests
./v20_tests
```

## Timing en foutafhandeling

Externe 1 kHz-klok op GPIO4, 3,3 V. Iedere flank wekt ControlTask. Een gemiste of samengevoegde tik stopt de regelaar (CLOCK_TIMEOUT/CLOCK_OVERRUN); START is opnieuw nodig. De PI gebruikt 1 ms, de sensorperiode is 10 ms en de actuatorperiode 50 ms. Deze verschillende snelheden en echte drive-rampen moeten op de installatie worden gevalideerd.

Sensorfeedback verloopt na 100 ms, motorstatus na 250 ms. Een motorrequest ouder dan 100 ms wordt lokaal STOP. Zonder verwerkt geldig Pi-commando gedurende 2 s stopt de regelaar. De V20-watchdog is 500 ms. Een fout wist de PI-historie. Een initfout vereist herstart na herstel. Communicatieherstel leidt niet automatisch tot RUN. Softwarestop via OFF1 gebruikt de drive-ramp; de noodstop is onafhankelijk.

## Raspberry Pi-protocol

UART1 op TX17/RX16, 115200 8N1, 3,3 V TTL, gedeelde signaal-GND. UART0 blijft USB/debug. ASCII-commando's, LF of CRLF, maximaal 95 tekens:

| Commando | Betekenis |
| --- | --- |
| `PING` | Houdt de verbinding actief, bijvoorbeeld iedere 500 ms |
| `SETPOINT 10` | Gewenste luchtsnelheid, 0-30 m/s |
| `PID 3.115445 0.391498 0` | Handmatige Kp/Ki/Kd in procent-uitgangseenheden; alleen bij stilstand |
| `START` | Start alleen bij geldige sensor- en motorstatus |
| `STOP` | Stopverzoek buiten de queue; heeft voorrang op START in dezelfde regelcyclus |

`OK QUEUED` bevestigt plaatsing, niet uitvoering. Controleer `STATUS` en `reason`. STOP en START moeten in afzonderlijke regelcycli worden verstuurd. Geen sequence-ID's in protocol v1. `out` is procent van 50 Hz, geen gemeten toerental. `sim=1` betekent volledige simulatie. De simulator gebruikt tau=0,5 s en 30 m/s bij 100%; het theoretische VFD-ontwerp gebruikt tau=0,6 s en 40 m/s bij 50 Hz.

## Nog op hardware te doen

Implementeer het werkelijke luchtsnelheidssensortype, kalibratie en omzetting naar m/s. Controleer ESP32-board, pinnen, klokbron en RS485-transceiver. Commission de V20 volgens V20_MODBUS.md en de motorgegevens. Test stop, kabelverlies, stale feedback, drivestoringen, responstijden en ramps op de installatie voordat de theoretische PI-gains worden gebruikt. Er is geen hardwaretest of flash uitgevoerd als onderdeel van deze wijziging.

## Instelbare vijfdegraads setpointcurve

Pas deze drie waarden aan in `include/AppConfig.h` en bouw/flash opnieuw:

| Instelling | Voorlopige waarde | Betekenis |
| --- | --- | --- |
| `SETPOINT_TRANSITION_TIME_S` | 5,0 s | Gewenste totale tijd per overgang |
| `SETPOINT_MAX_ACCELERATION_MPS2` | 2,0 m/s² | Maximale veranderingssnelheid van het luchtsnelheidsetpoint |
| `SETPOINT_MAX_JERK_MPS3` | 2,0 m/s³ | Maximale verandering van die versnelling |

Alle waarden moeten positief zijn. Het profiel is `v(q)=v0+(v1-v0)*(10q³-15q⁴+6q⁵)`, met `q=t/T`. Begin en eind hebben nul versnelling en nul jerk. De werkelijke duur is de grootste van de gevraagde tijd, `1,875*abs(v1-v0)/a_max` en `sqrt((10/sqrt(3))*abs(v1-v0)/j_max)`. Bijvoorbeeld 0 naar 10 m/s duurt met de standaardwaarden 9,375 s, omdat de versnellingsgrens de gevraagde 5 s verlengt. 0 naar 2 m/s duurt 5 s.

Bij START begint het profiel bij de gemeten luchtsnelheid. Een nieuwe SETPOINT tijdens een lopend segment wordt pas na dat segment opgepakt; alleen het nieuwste doel telt. Dit voorkomt sprongen door herplanning. Voor onmiddellijk stoppen gebruik je STOP: stoppen en fouten omzeilen het profiel en sturen OFF1. Bij herstart begint een nieuw profiel bij de dan gemeten luchtsnelheid.

`STATUS sp=...` blijft het gevraagde einddoel. Het extra veld `sp_ramp=...` toont het actuele setpoint dat naar de PI gaat. De grenzen gelden voor de berekende referentiecurve, niet als garantie op de echte luchtstroming of motorbeweging. Drive-rampen en plantdynamiek blijven afzonderlijk van toepassing. De curve wordt op 1 ms bemonsterd.
