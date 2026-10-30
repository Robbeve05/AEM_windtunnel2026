# Siemens SINAMICS V20 via Modbus RTU

Voor 6SL3210-5BE25-5UV1. De implementatie gebruikt UART1 en de ingebouwde geïsoleerde RS485-transceiver; er wordt geen Modbus TCP gebruikt. De drive wordt niet door deze software gecommissioned of automatisch van storingen gereset.

## Aansluiten en configureren

Interne boardverbindingen: GPIO17 TX, GPIO18 RX, GPIO21 RTS. Gebruik de geïsoleerde A+/B- klemmen; geen externe transceiver nodig. De UART-driver schakelt de zendrichting. Gebruik de RS485-aansluitingen van de drive volgens de Siemens-handleiding; controleer polariteit, referentie, afscherming en busterminatie. ESP32-pinnen niet rechtstreeks op RS485 aansluiten.

Stel de actieve command/data set van de V20 passend in:

| Parameter | Instelling voor deze software |
| --- | --- |
| P0700 | 5, opdrachten via USS/Modbus |
| P1000 | 5, frequentiesetpoint via USS/Modbus |
| P2023 | 2, Modbus; daarna voorgeschreven power-cycle |
| P2021 | 1, slave-adres |
| P2022 | 20 ms maximale antwoordvorming; ESP32 wacht 80 ms inclusief ontvangst |
| P2010[0] | 6, 9600 baud |
| P2034 / P2035 | 2 / 1, even parity / 1 stopbit (8E1) |
| P2000 | 50 Hz referentiefrequentie |
| P2014[0] | 500 ms telegramwatchdog; init schrijft en verifieert dit |

Controleer motorgegevens, toegestane maximumfrequentie, draairichting en P1120/P1121 ramps afzonderlijk. De code verandert deze niet. De 0,6 s uit het PI-model is een geschatte planttijdconstante, geen aanbevolen rampinstelling. Een fysieke noodstop blijft onafhankelijk van Modbus. OFF1 is een normale afremstop volgens de ingestelde ramp.

## Registercontract

| Functie | Register | PDU-adres | Waarde |
| --- | --- | --- | --- |
| STW | 40100 | 99 | 0x047E OFF1, 0x047F vooruit/run |
| HSW | 40101 | 100 | 0..16384 = 0..100% van P2000 |
| ZSW / HIW | 40110 / 40111 | 109 / 110 | status en werkelijke frequentie |
| Watchdog | 40001 | 0 | 500 ms |
| Referentie | 40016 | 15 | 5000 = 50,00 Hz |

FC16 schrijft STW en HSW samen. FC03 leest status/waarden; FC06 stelt de watchdog in. Bytevolgorde: registers big-endian, CRC little-endian. Init stuurt eerst STOP en weigert een andere P2000. De statuscontrole vereist ready-to-switch-on en remote-control, zonder fault of switch-on-inhibit. Operation-enabled is niet verplicht bij stilstand. Een start met 0% blijft een RUN-opdracht; STOP gebruikt altijd OFF1 en nul HSW.

Iedere opdracht krijgt een begrensde response-wachttijd van 80 ms, plus zend- en framepauze. De drive moet binnen die termijn antwoorden; meet dit bij ingebruikname. P2022=20 ms voorkomt dat een veel later gevormd antwoord bij een volgende aanvraag terechtkomt. Een aanvraag kan bij overschrijding al uitgevoerd zijn, daarom volgt bij ontbrekende bevestiging STOP. Late antwoorden na timeout worden uitgefilterd met een extra wachttijd. Slave, functie, lengte, bytecount, schrijfbevestiging en CRC worden gecontroleerd. Exceptions zijn fouten. Er zijn geen automatische RUN-retries. Bij fouten volgt een best-effort STOP; een geslaagde gestopte cyclus is vereist voordat RUN weer mag. ControlTask valt bij een motorfout terug naar gestopt en vereist opnieuw START. Een mislukte initialisatie vereist een firmwareherstart na herstel. Als STOP niet aankomt, is de drivewatchdog de terugvalvoorziening; test kabelverlies daadwerkelijk.

## Bouwen en testen

- `pio run -e waveshare_s3_sim`: simulatie (standaard).
- `pio run -e waveshare_s3`: echte Modbus- en ASPD-4525-code, SIMULATION_MODE=0.
- `./tests/run_host_tests.ps1`: controller/protocol en V20-mocktests met Visual Studio C++ tools.

De ASPD-4525-driver gebruikt de bestaande Uno-conversie en filtering. Houd de pitot bij opstart in stilstaande lucht voor nulkalibratie. De interne GPTimer wekt ControlTask op 1 kHz; sensoren nominal 25 Hz en actuator 10 Hz. Zie MIGRATION.md voor bedrading, foutrespons en hardwaretests.

De PI gebruikt het vfd_proxy-model uit windtunnel_pi_design.py: G(s)=0,8/(0,6s+1), crossover 0,20 Hz, PI-nulpunt 0,02 Hz. Kp=1,557722505 Hz/(m/s), Ki=0,195749183 Hz/m; de C-uitgang in procenten gebruikt tweemaal deze gains. Tustin gebruikt de werkelijke sampletijd van de aanroeper (5 ms / 200 Hz). De oude torque-modus en robot-Ka/Kt worden niet gebruikt. Het Python-bronbestand zelf is niet gewijzigd. De bestaande simulator (30 m/s bij 100%, tau 0,5 s) blijft een afzonderlijk testmodel.

Bron: [Siemens V20 Operating Instructions 12/2024, sectie 7.2 en parameterlijst](https://support.industry.siemens.com/cs/attachments/109977897/V20_op_instr_1224_en-US.pdf).

Aanvullend gecontroleerd tegen de aangeleverde Siemens Operating Instructions 09/2014, A5E34559884: p. 139-145 (Modbus), p. 157 (statusbits), p. 235 (P2022). Deze oudere editie vermeldt geen P2014-koppeling in de watchdog-registertabel; controleer P2014[0]=500 ms ook op de drive zelf en test F72 bij kabelverlies.
