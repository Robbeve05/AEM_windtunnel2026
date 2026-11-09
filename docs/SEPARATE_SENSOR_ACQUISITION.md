# Gescheiden Pitot- en druktapacquisitie

Datum: 9 november 2026. Repository AEM_windtunnel2026, branch main.

## Uitgevoerde refactor

| Gegevens | Producent | Queue | Consumenten |
|---|---|---|---|
| PitotSample | ControlTask, Core 1, prioriteit 4 | pitot_samples, lengte 1 | PiCommunicationTask, main-diagnostiek |
| PressureTapSample | SensorTask, Core 0, prioriteit 3 | pressure_taps, lengte 1 | PiCommunicationTask, main-diagnostiek |

ApplicationTasks maakt beide queues vóór de taken aan. De oude gecombineerde
Sample en sensors-queue zijn verwijderd, zonder alias. PitotSample bevat geen
tapwaarden; PressureTapSample bevat geen Pitot-waarden. Beide producenten
publiceren uitsluitend hun eigen queue met xQueueOverwrite. Lezers gebruiken
xQueuePeek met timeout 0. De regelaar leest pitot_samples nooit terug.

ControlTask initialiseert zelf de Pitot-filter-/kalibratiestatus vóór de timer.
SensorTask initialiseert of wijzigt geen Pitot-status meer. De tapscanner werkt
rechtstreeks op het ene private werkbeeld van SensorTask, zonder tweede statische
kopie van de zestien kanaalwaarden. De scanmetadata blijft uitsluitend van SensorTask.

## Volgorde van een normale ControlTask-cyclus

1. Timerwake, watchdog en bestaande gemiste-deadlinecontrole.
2. Veiligheids-/heartbeat-/motorstatus en fysieke knoppen lezen.
3. Bestaande commandoverwerking, inclusief STOP.
4. Eén SensorLib_PitotStep: één I2CBus_TryReadOnce naar 0x28.
5. Lokale meting onmiddellijk gebruiken en een kopie publiceren naar pitot_samples.
6. Heartbeat-expiratie, actuele safety-snapshot en eventueel tijdens I2C ontvangen
   STOP opnieuw controleren; bestaande veiligheids- en toestandslogica uitvoeren.
7. ISA-regelaar uitsluitend bij geldige feedback; motorvraag publiceren,
   bestaande niet-blokkerende Modbus-service uitvoeren en totale cyclustijd bewaken.

Een al herkende STOP, noodstop, urgente fysieke fout of control-clockfout slaat
de Pitot-call over. Daarbij wordt geen oud meetresultaat opnieuw gepubliceerd als
nieuwe meting. De bestaande toestandsmachine en motorafhandeling volgen direct.
De ISA-stap heeft bovendien een expliciete sensor_ok/STOP-guard. Een tijdens de
begrensde uitlezing ontvangen STOP wordt vóór de regelstap verwerkt.

De bestaande Modbus-protocolinstellingen, OFF1/OFF2, fysieke START, DO1-interlocks,
noodstopketen en commissioning-flags zijn door deze opdracht niet gewijzigd.
InputHandler blijft op Core 0/prioriteit 5 en doet zijn bestaande 10 ms DO-controle.

## Pitot: geldigheid, kalibratie en filtering

- Iedere uitgevoerde poging krijgt een nieuw sequence-nummer en timestamp,
  ook als de poging ongeldig is. Een overgeslagen STOP-/noodstopcyclus krijgt
  geen nieuw sequence-nummer. Sequence is dus een pogingsteller, geen OK-teller.
- Alleen MS4525DO-status 0 wordt geaccepteerd. Status 1, stale status 2 en
  foutstatus 3 zijn ongeldig, evenals ongeldige drukcounts en busfouten.
- De nieuwe one-shot busfunctie heeft geen interne NACK-retry: maximaal één
  wire-read per cyclus. De bestaande retryfunctie blijft voor andere gebruikers
  beschikbaar. Mutexacquisitie is niet-wachtend; het driverbudget blijft 2 ms.
- Bij NACK, timeout of busconflict wordt een expliciet ongeldige sample gepubliceerd.
  Filter en nulkalibratie worden niet bijgewerkt door ongeldige reads.
- De bestaande toestandsmachine krijgt sensor_ok=false. In READY/STARTING/RUNNING
  en de bestaande overige sensorbewaakte toestanden leidt dit tot de bestaande
  FAULT_SENSOR-route. Er is geen grace waarbij ISA op oude feedback integreert.
  Eén busconflict kan dus al tot een gelatchte sensorfout leiden; dit is bewust
  zichtbaar en moet op de fysieke opstelling op ongewenste trips worden getest.
- Nulkalibratie gebruikt exact 200 geldige ruwe reads, circa twee seconden bij
  100 Hz zonder fouten. De 200e read voltooit kalibratie; de volgende geldige read
  is de eerste geregelde meting. Geen automatische herkalibratie bij herstel.
- De vijfpunts-batchmiddeling is een voortschrijdend vijfpuntsgemiddelde geworden.
  Iedere nieuwe read beïnvloedt daardoor de feedback in dezelfde 10 ms-cyclus.
  De eerste reads gebruiken het beschikbare aantal punten tot vijf bereikt is.
- Het 1 Hz-laagdoorlaatfilter gebruikt werkelijke verstreken tijd. De 5 Pa-deadband,
  druk-/temperatuurconversies, luchtdichtheid en absolute-druk/snelheidsconversie
  zijn behouden. De publicatiefrequentie wordt ongeveer 100 Hz in plaats van 20 Hz.

Onveranderde oude druk/snelheidswaarden kunnen tijdens een fout diagnostisch
zichtbaar blijven; valid=false/read_ok=false voorkomt gebruik als geldige feedback.
De diagnostiek-getter blijft een coherent, beschermd snapshot voor de andere core.

## Druktaps en telemetrie

De bestaande incrementele zestienkanaalscan en conversietijden zijn behouden.
SensorTask doet maximaal één scanstap per timerfase en publiceert na een voltooid
kanaal de onafhankelijke tap-sample met sequence, tick, errors, taps_valid,
per-kanaaltimestamps en updated_mask. De Pitot is daarvoor niet nodig.

PiCommunicationTask leest beide queues afzonderlijk. Bestaande veldnamen pv,
pressure_pa, temperature_c, sample_seq, sensor_errors, taps_valid en p0..p15 blijven
beschikbaar. sensor_errors is nu uitsluitend de Pitot-foutteller; taps_errors is
apart beschikbaar. Ontbrekende Pitot-data heeft sensor_ok=0. Ontbrekende of per
kanaal verouderde taps krijgen een gewist geldigheidsbit; de p-waarde is dan een
protocolcompatibele numerieke placeholder, niet een geldige drukmeting.

Nieuwe velden:

- taps_seq, taps_age_ms, taps_available, taps_errors, taps_updated.
- tap_age0_ms tot tap_age15_ms; UINT32_MAX bij ontbrekend/ongeldig kanaal.
- pitot_acquisition_us en pitot_acquisition_max_us.

Tapgeldigheid wordt bij verzending berekend uit de oorspronkelijke kanaaltimestamps
met TAP_TIMEOUT_MS. Een recent gepubliceerde aggregate verjongt oude kanalen dus
niet. De maskfunctie gebruikt wrap-veilige tickaftrekking en is getest op wrap.

HMI/backend/esp32.py parseert de nieuwe velden numeriek. De status-API toont een
onafhankelijk pressure_taps-snapshot, ook tijdens ongeldige Pitot-feedback en WAIT.
De systeemdiagnostiek toont acquisitieduur en tapmetadata. Bestaande Pitot-HMI-
updates blijven werken in WAIT, READY, MANUAL en AUTO. STATUS kan herhaalde of
niet alle 100 Hz-Pitot-sequences bevatten; de UI wordt geen 100 Hz-bemonsteraar.

Het meetlog neemt alleen nieuw gemarkeerde tapkanalen van een nieuw taps_seq op.
Andere kanaalposities zijn null, zodat een tapwaarde niet verschuift naar een ander
kanaal of als nieuwe meting wordt herhaald. pressure_pa behoudt de zestien vaste
kanaalposities zodra de nieuwe tapmetadata aanwezig is. De Cp-code slaat nulls over.
CSV bevat tevens taps_seq, taps_age_ms, taps_valid en taps_new. Deze extra kolommen
laten de herkomst van gedeeltelijke tapmetingen achteraf controleren.

## Timinganalyse en bewezen grenzen

De timer blijft twee fasen van 5.000 us afwisselen. ControlTask blijft 100 Hz op
Core 1; SensorTask blijft 100 scanstappen/s op Core 0. De druktapstap ligt nominaal
5 ms voor de ControlTask, maar levert geen feedback voor die regelcyclus. Er is
geen reden deze fase zonder hardwaremetingen op hetzelfde tijdstip te leggen:
het behouden ervan spreidt juist de twee I2C-gebruikers.

De vorige Pitot-acquisitie zat buiten de regelaar; de volledige 10 ms-cyclus bevat
nu ook het begrensde I2C-driverdeel. Nominaal 2 ms sensorbudget plus minder dan
5 ms Modbus-service laat circa 3 ms voor overige code. Dit is budgettering,
geen harde realtimegarantie: tickgranulariteit, IDF, RTOS-preëmptie en echte
busstoringen moeten op de ESP32 worden gemeten.

pitot_acquisition_us meet direct vóór/tot direct na de bus-API, inclusief de
mutexpoging en IDF-wachttijd. De bestaande execution_us begint vóór acquisitie en
eindigt na Modbus-service. De bestaande afzonderlijke service_us- en totale
10 ms-overschrijdingsbewaking blijven actief.

| Claim | Hostbewijs | Nog fysiek te valideren |
|---|---|---|
| Nieuwe meting naar ISA in dezelfde cyclus | Echte ControlTask + PressureSensors; queue met 999 vergiftigd, ISA gebruikt lokale PV | Werkelijke wake/read/ISA-latentie |
| Maximaal één Pitot-read per cyclus | One-shot API, geen retry; calltellers en ownershiptest | Logic-analyzertrace op 0x28 |
| Geen ISA-integratie op invalid/stale | Foutinjectie, ongeldige publicatie en FAULT_SENSOR/reset van ISA-status | Echte NACK/stale/busconflict onder motor-EMC |
| STOP/noodstop wachten niet op nieuwe I2C | Acquisitiecall wordt overgeslagen; bestaande safetytests slagen | Fysieke DO1-/drive-reactietijd |
| Pitot inclusief Modbus telt mee in cyclustijd | 2.000 us I2C + 4.999 us service = 6.999 us in geïnjecteerde klok | Echte worst-case blijft onder 10 ms |
| Overschrijding leidt tot timingfout | Geforceerd minstens 10.000 us activeert bestaande overrunbewaking | Deadlines/jitter over lange hardwaretest |
| Onafhankelijke tapleeftijd/sequence | Geldigheidsmaskers, kanaalupdatebits, wrap en HMI-regressie | Complete sweepfrequentie onder busbelasting |

## Testresultaten

- Volledige actuele C-hostsuite: 30/30 executables geslaagd.
- Na de laatste interne tapgeheugen-/timingaanpassing: sensors en control_pitot
  opnieuw gecompileerd en beide geslaagd.
- Volledige HMI-suite met regressies voor de queuesplitsing: 118 tests geslaagd.
- Python ESP32-structuur-/ownershiptests: 2 tests geslaagd.
- Firmware: waveshare_s3, waveshare_s3_frequency, waveshare_s3_sim en
  waveshare_s3_i2c100 alle vier geslaagd, ook na de laatste sensorwijziging.
- git diff --check geslaagd.

Lokale logs staan onder ESP32/.pio/acquisition-*.log (genegeerde buildartefacten).
De integratietest voert ControlTask en PressureSensors in productiemodus uit;
RTOS, UART en I2C zijn deterministische hostfakes. SafetyIO gebruikt de bestaande
simulatie-implementatie in een apart hostbestand; echte GPIO/ISR-timing wordt
hiermee niet geclaimd. Een test-only torquecommissioningdefine maakt de ISA-route
bereikbaar en verandert geen firmwareconfiguratie.

## Gewijzigde onderdelen voor deze opdracht

- TaskData.h en ApplicationTasks.c: aparte structs en queues.
- ControlTask.c: lokale Pitot-acquisitie, publicatie, validiteitsguard en timing.
- SensorTask.c en PressureSensors.h/.c: afzonderlijke eigenaars, rolling Pitot en tapbeeld.
- I2CBus.h/.c: one-shot read zonder retry.
- PiCommunicationTask.c en main.c: onafhankelijke queues en diagnose.
- HMI/backend/esp32.py, main.py, native_diagnostics.py: parsing, snapshots en metadata.
- HMI/backend/measurements.py en cp.py: kanaalvaste sparse meetdata en logmetadata.
- Hosttests: test_control.c, test_control_pitot.c, host/safety_sim.c, test_i2c.c,
  test_sensors.c, test_startup.c, test_main_diagnostics.c, test_acquisition_layout.py
  en run_host_tests.ps1; HMI/tests/test_split_acquisition.py.

Er stonden/verschenen ook wijzigingen uit parallel werk aan de MANUAL-ramp,
AppConfig.h, SetpointCurve.c en bijbehorende tests/documentatie. Die zijn behouden,
niet door deze sensoropdracht aangebracht. De volledige suite test de gezamenlijke
werkmap. De MANUAL-test is inbegrepen in het totaal van 30 executables.

## Fysieke validatie: nog open

De connectiepoging naar 192.168.50.2:5000 gaf een timeout. Er is geen firmware
geflasht en geen motor gestart. Daarom zijn daadwerkelijke 100 Hz-deadlines,
I2C-mutexconflicten tussen de cores, NACK-frequentie en fysieke safetyreactietijden
nog niet bevestigd.

Controleer op de opstelling eerst met motor uit nulkalibratie, 100 reads/s,
statusbits en onafhankelijke sequences. Leg pitot_acquisition_us/max, execution_us,
service_us, jitter, overruns en missed_deadlines vast bij 100 en 400 kHz, daarna
met toegestane motorstanden en actieve taps/DO1-controle. Injecteer afzonderlijk
NACK/timeout/stale/busconflict en controleer STOP, FAULT, noodstop, fysieke RESET
en een nieuwe fysieke START. Controleer daarbij dat herstel nooit automatisch
RUN wordt en dat taptimestamps tijdens Pitot-uitval hun eigen leeftijd behouden.
