# Siemens V20 Modbus RTU: schedulervergelijking

Datum: 9 november 2026. Metingen met de echte driver en een deterministische UART-fake, zonder aangesloten V20. De oorspronkelijke driver staat in git-commit `3df7bc2b60d6c3b2412bf43f23c99e7fcadd1d2c` (`ESP32/src/V20Modbus.c`); een lokale meetkopie staat in `output/V20Modbus_baseline.c`. De numerieke resultaten zijn hostmetingen, geen meting van fysieke RS-485-signalen of bevestiging van de ingestelde P2014 op een echte drive.

## Onderbouwde wijzigingen

1. Statuspolling vanaf het geaccepteerde TX-tijdstip, zoals de bestaande procesdatawrite. De vorige planning vanaf de respons stapelde RTT op en liet statusreads steeds opnieuw de volgende write blokkeren.
2. Na de volledige late-responsequarantaine krijgt een vervallen RUN-procesdatawrite voorrang op een readretry. De ene readretry blijft pending en behoudt zijn budgetcontrole. Een mislukte write wordt nooit blind herhaald als RUN.
3. Een volledig frame dat pas na de 80 ms responsdeadline wordt verwerkt, resulteert nu ook in TIMEOUT. Voorheen kon zo'n frame bij een vertraagde ControlTask alsnog slagen. Exact op de deadline blijft een volledig geldig antwoord toegestaan.
4. Cumulatieve busdiagnostiek via MotorStatus, Ethernet STATUS en de HMI-diagnostiektabel: transactiefrequentie, geschatte wirebelasting, write/statuscyclus, maximale statuscyclus, writejitter, te late write-intervallen, maximale transactieduur, idle voor TX, maximale servicepauze en retryresultaten.

115200 baud, 8E1, slave 1, P2014=500 ms, 80 ms timeout, quarantaine van 160 ms, fysieke knoppen, OFF1/OFF2, commissioning-flags, armvoorwaarden en begrensde STOP-pogingen zijn door deze wijziging niet aangepast. Er is geen nieuwe automatische arm- of herstartactie.

## Busbelasting en transacties

Een write bestaat uit 13 requestbytes + 8 responsebytes. Een statusread bestaat uit 8 + 9 bytes. Bij 8E1 duurt een byte 11/115200 s = 95,49 us. Tien writes en tien statusreads per seconde vergen 380 bytes/s, oftewel 3,63% zuivere wiretijd. Bij twee minimale framepauzes van 1,75 ms per transactie komt daar minimaal 7% stilte bij: samen 10,63%, exclusief extra slaveverwerking en schedulingwacht.

De ControlTask bedient de driver iedere 10 ms. Na een waargenomen respons geldt minimaal 1,75 ms stilte; de volgende servicetick maakt dat normaal 10 ms. Bij 70 ms waargenomen RTT kost een transactie daarom 80 ms schedulertijd. Twee transacties passen dan niet in 100 ms, ook al is de fysieke wirebelasting laag. De 80 ms timeout is geen RTT-meting.

Bij normaal RUN en gecontroleerde OFF1-ramp is het doel 10 writes + 10 statusreads/s. In gewone stilstand blijft de bestaande writeheartbeat 150 ms; bij niet vrijgegeven commissioning wordt uitsluitend gelezen en is RUN verboden. In een gelatchte fout stoppen reguliere writes; de bestaande begrensde OFF1-afhandeling blijft gelden. De twee configuratiereads worden alleen bij bootstrap of expliciete recheck aangevraagd. De nieuwe duurtest verifieert dat gedurende stabiel bedrijf uitsluitend WRITE_CONTROL en READ_STATUS voorkomen.

## Vergelijking: 60 seconden per vaste responstijd

Elke proef heeft 1 seconde opwarming. De fake biedt het volledige antwoord na 1, 30 of 70 ms aan; een aangeboden antwoord na 1 ms wordt pas bij de volgende 10 ms servicetick verwerkt. Frequenties tellen verzonden requests, inclusief een eventueel nog pending antwoord aan het meeteinde. Gemiste deadlines tellen geobserveerde opeenvolgende write-intervallen groter dan 100 ms; het aantal intervallen verschilt door de gewijzigde frequentie.

| Aangeboden antwoord / waargenomen RTT | Writes/s oud -> nieuw | Statusreads/s oud -> nieuw | Totaal transacties/s oud -> nieuw | Maximale writeafstand oud -> nieuw | Maximale absolute writejitter oud -> nieuw | Late write-intervallen oud -> nieuw |
|---|---|---|---|---|---|---|
| 1 / 10 ms | 9,100 -> 10,000 | 9,083 -> 10,000 | 18,183 -> 20,000 | 110 -> 100 ms | 10 -> 0 ms | 545 -> 0 |
| 30 / 30 ms | 7,700 -> 10,000 | 7,700 -> 10,000 | 15,400 -> 20,000 | 130 -> 100 ms | 30 -> 0 ms | 461 -> 0 |
| 70 / 70 ms | 5,883 -> 6,250 | 5,883 -> 6,250 | 11,766 -> 12,500 | 170 -> 160 ms | 70 -> 60 ms | 352 -> 374 |

De maximale status-requestafstand is in deze vaste proeven gelijk aan de maximale writeafstand. RTT verandert niet door de scheduler. Geen starvation of run lock in deze proeven. De 70 ms proef bewijst nadrukkelijk geen 100 ms garantie: beide cycli zijn daar 160 ms. Daarvoor zou een andere servicearchitectuur en/of kortere werkelijke RTT nodig zijn; hogere baudrate, kortere timeout en minder statusfeedback zijn hier niet ingevoerd.

De nieuwe diagnostiek geeft in de vaste proeven 10 ms minimale en 50 ms maximale idle voor TX aan (inclusief bootstrap en opwarming), 10 ms maximale servicepauze en respectievelijk 10/30/70 ms maximale transactieduur. De bootgemiddelde wirebelasting is afgerond 36/36/22 promille. Dit zijn UART-waarnemingen: idle is de tijd vanaf softwarematige transactieafronding tot het volgende TX-aanbod, niet de fysieke afstand tussen stopbits.

## Jitter, retries, STOP en watchdog

Een tweede proef varieert serviceperioden deterministisch tussen 8/12/10 ms en antwoordbeschikbaarheid tussen 1/30/50 ms:

| Metriek | Oud | Nieuw |
|---|---:|---:|
| Writes/s | 8,483 | 8,833 |
| Statusreads/s | 7,200 | 8,817 |
| Maximale TX-afstand | 150 ms | 130 ms |
| Maximale ACK-afstand | 170 ms | 150 ms |
| Maximale positieve writeafwijking t.o.v. 100 ms | 50 ms | 30 ms |
| Maximale waargenomen RTT | 60 ms | 60 ms |

Ook met deze jitter blijven beide operaties bediend en blijft de drive geautoriseerd. De extra RTT boven de aangeboden 50 ms is softwarematige waarnemingsvertraging.

Bij precies één verloren statusantwoord (normale overige antwoorden) is de maximale procesdata-TX-afstand 310 ms oud en 280 ms nieuw. Beide herstellen met precies één readretry, één recovered transaction en zonder run lock. De nieuwe gemeten afstand laat 220 ms tot P2014=500 ms over, gemeten vanaf TX-acceptatie, niet vanaf ontvangst door de drive.

STOP direct na de start van een statusread met 70 ms RTT geeft in beide versies de OFF1-write na 80 ms: de lopende read plus één servicetick voor de framepauze. Normale polling komt daar niet tussen. Een transactie wordt niet halverwege afgebroken; dit voorkomt het mengen van late antwoorden. Bij een verloren antwoord blijft de bestaande quarantaine extra vertraging geven. Fysieke OFF2-afhandeling is hiervan onafhankelijk en is niet aangepast.

Readretries blijven maximaal één per mislukte read, met de bestaande 450 ms budgetgrens inclusief 30 ms scheduler/write-reserve. Late bytes verlengen de quarantaine, maar de bestaande controle vergrendelt RUN zodra de resterende retry niet meer binnen dit budget past. De regressietest injecteert doorlopend late bytes en controleert de lock vóór 450 ms. Een permanente busstoring garandeert uiteraard geen volgende succesvolle write: dan moeten de fail-safe bewaking en de V20-watchdog ingrijpen. 280 ms is het gemeten maximum van de genoemde herstelproef, geen algemene bovengrens voor alle storingen of vastgelopen taken.

## Interpretatie diagnostiek

- `modbus_wire_load_permille` en `modbus_transactions_per_minute` zijn gemiddelden vanaf driverinitialisatie. Wirebelasting rekent 11 bits per geaccepteerde TX-byte en binnen een transactie verzamelde RX-byte. Verworpen bytes buiten transacties en gedeeltelijk mislukte UART-TX ontbreken; dit is een schatting, geen busanalyser.
- `modbus_write_cycle_us`, `modbus_status_cycle_us`, `modbus_status_cycle_max_us`, `modbus_write_jitter_max_us` meten TX-acceptatieafstanden. Writejitter en `modbus_write_deadline_misses` tellen alleen opeenvolgende RUN-writes; een te laat interval telt één keer, niet één keer per gemiste 100 ms periode. Stopped heartbeats tellen daar niet mee.
- `modbus_transaction_max_us` omvat ook mislukte transacties; RTT/max RTT gelden voor succesvolle antwoorden. `modbus_enqueue_us` meet de UART-aanbiedtijd. Idle en servicepauzes beschrijven softwarewaarnemingen. De maximale servicepauze wordt bijgehouden zolang een transactie pending is.
- Bestaande maximale TX-, ACK- en responsafstanden blijven afzonderlijk beschikbaar. Een TX-acceptatie bewijst geen driveontvangst; een FC16-echo bewijst geen motorstilstand. Retry/recovered/final-error tellers zijn nu ook via STATUS en HMI zichtbaar.
- Cumulatieve tellers zijn 32-bits en worden bij driverinitialisatie gereset. Bootstrap, recheck en storingen kunnen historische maxima bepalen.

Voor fysieke RTT, DE-turnaround en exacte inter-framepauzes is een RS-485-analysermeting nodig naast de nieuwe diagnostiek. Er is tijdens dit onderzoek geen firmware geflasht en geen motor bediend.

## Reproductie en validatie

- `ESP32/tests/run_host_tests.ps1` voert alle hostexecutables uit, inclusief `v20_scheduler`, de bestaande 10-minuten heartbeatproef, UART-fouten, late antwoorden, begrensde retries, STOP/RESET en V20/state-machine-simulatie.
- De nieuwe `test_v20_scheduler.c` gebruikt de bestaande UART-fake en compileert tegen de echte driver. Met `/DBASELINE` kan dezelfde test tegen de bewaarde oorspronkelijke driver worden gelinkt; alleen de nieuwe prestatie- en deadlineasserties worden dan uitgeschakeld.
- Vergelijkingslogs: `output/v20-scheduler-baseline.log` en `output/v20-scheduler-optimized.log`.
- Volledige host- en firmwarelogs: `output/v20-optimization-host-tests-final.log` en `output/v20-optimization-builds-final.log`.
- Firmwarebuilds: `pio run -d ESP32 -e waveshare_s3 -e waveshare_s3_sim -e waveshare_s3_frequency -e waveshare_s3_i2c100`.
- HMI: unittest `test_esp32_ethernet.py` (16 tests) en `test_native_hmi.py` (20 tests); daarnaast de ESP32 source-layouttest. Alle uitgevoerd en geslaagd.

Resultaat: volledige host-suite 28/28 geslaagd; definitieve V20-hercontrole 6/6 geslaagd; alle vier PlatformIO-omgevingen SUCCESS. De aanvullende definitieve V20-log staat in output/v20-optimization-v20-final.log. git diff --check geeft geen fouten. Bestaande lokale wijzigingen buiten deze Modbus-optimalisatie zijn behouden.
