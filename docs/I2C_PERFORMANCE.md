# I2C performance en NACK-betrouwbaarheid

Datum: 9 november 2026. Branch: main. De wijzigingen zijn niet geflasht.

## Resultaat en beperkingen

- Standaardklok 400 kHz, configureerbaar met SENSOR_I2C_HZ. De aparte
  PlatformIO-omgeving waveshare_s3_i2c100 bouwt dezelfde firmware op 100 kHz.
- DO-wijzigingen schrijven direct binnen de bestaande InputHandler-cyclus.
  Een ongewijzigd beeld krijgt iedere 10 ms een register-1-readback in plaats
  van een herhaalde write. De controlefrequentie is dus niet verlaagd.
- Sensortransacties krijgen 2 ms driverbudget, tegenover voorheen 10 ms.
  DO houdt zijn bestaande 10 ms mutexacquisitie en 10 ms driverbudget.
- HMI-systeemdiagnostiek bevat een I2C-groep met per apparaat een uitklapbare
  adresgroep, tellers, tijdmetingen en foutcodes. Niet benaderde adressen tonen
  'Not accessed'; nul tellers bewijzen geen aanwezigheid of goede verbinding.
- Er zijn geen wijzigingen in V20, ControlTask, StateMachine, noodstop,
  fysieke startautorisatie, commissioning-flags of Pitot-filter/conversie.

De live connectiepoging naar 192.168.50.2:5000 eindigde in een timeout.
De fysiek aangesloten apparaten konden daardoor niet worden geïnventariseerd.
Werkelijke NACK-incidentie, stijgtijden, busduur en gemiste deadlines op hardware
zijn NIET gemeten. De onderstaande busbelasting is een expliciet draadmodel.

## Compatibiliteit vooraf

De inventaris is gebaseerd op de opgegeven Waveshare-boardconfiguratie en de
productiecode, niet op een geslaagde fysieke busscan. De code gebruikt momenteel
alleen de DO-expander en Pitot. De RTC blijft elektrisch onderdeel van de bus,
ook zonder RTC-transacties. De optionele druktaps staan standaard uit.

| Apparaat | Adres | Ondersteunde klok | Status in huidige code |
|---|---|---|---|
| TCA9554 DO-expander | 0x20 | Tot 400 kHz | Actief |
| MATEKSYS ASPD-4525, TE MS4525DO | 0x28 | 100–400 kHz | Actief |
| PCF85063 RTC volgens board/project-inventaris | 0x51 | Tot 400 kHz | Geen RTC-driververkeer |
| TCA9548A muxen | 0x70, 0x71 | Tot 400 kHz | Optioneel, uit |
| Honeywell ABP2 | Geconfigureerd adres, test 0x29 | 100–400 kHz | Optioneel, uit |

Fabrikantbronnen:

- [Waveshare-board](https://www.waveshare.com/product/modules/others/esp32-s3-poe-eth-8di-8do.htm).
- [TI TCA9554](https://www.ti.com/lit/ds/symlink/tca9554.pdf): Fast-mode en
  register 1 leest de uitgangslatch, niet de fysieke pin/contactstand.
- [NXP PCF85063A](https://www.nxp.com/products/PCF85063A): 400 kHz interface.
- [MATEKSYS ASPD-4525](https://www.mateksys.com/?portfolio=aspd-4525): identificeert
  de TE 4525DO-DS5AI001DP.
- [TE MS4525DO datasheet, juli 2023](https://www.te.com/commerce/DocumentDelivery/DDEController?Action=srchrtrv&DocFormat=pdf&DocLang=English&DocNm=MS4525DO&DocType=Data+Sheet&PartCntxt=4525DO-DS3BI005DS):
  pagina 9, I2C timing, 100 tot 400 kHz.
- [TI TCA9548A](https://www.ti.com/product/TCA9548A).
- [Honeywell ABP2 datasheet](https://prod-edam.honeywell.com/content/dam/honeywell-edam/sps/siot/en-us/products/sensors/pressure-sensors/board-mount-pressure-sensors/basic-abp2-series/documents/sps-siot-abp2-series-datasheet-32350268-en.pdf):
  I2C-timing 100–400 kHz.

Deze ondersteuning van de IC's bewijst nog niet dat lange kabels, pull-ups,
levelshifters, extra onbekende apparaten en EMC op deze opstelling geschikt zijn.
Controleer de daadwerkelijk gemonteerde RTC-variant en aangesloten uitbreidingen
voor gebruik van 400 kHz; houd bij onvoldoende signaalmarge de fallback aan.

## DO-strategie en veiligheidsgrenzen

DigitalOutputs_Set blijft één geserialiseerde writer. Bij ieder gewijzigd
uitgangsbeeld volgt een write, zonder uitstel of debounce voor DO1 of lampen.
Bij gelijkblijvend beeld volgt één gecombineerde registerselectie/read met
repeated START. De busmutex blijft tijdens deze combinatie vast: een andere
transactie kan de registerpointer niet tussendoor wijzigen.

De kosten van het gelijkblijvende beeld zijn één 36-kloks registerread in plaats
van één 27-kloks write. Er zijn dus minder writes, maar niet minder transacties.
Op dezelfde bussnelheid kost deze sterkere controle ongeveer 33% meer draadklokken
voor DO alleen. Bij 400 kHz blijft de totale draadbelasting ruim lager dan de
oude strategie op 100 kHz. Een langzame controle van alleen indicatie-uitgangen
is niet veilig te scheiden van DO1: alle acht bits delen hetzelfde register.
Daarom is de bestaande 10 ms bewakingsfrequentie behouden, ook als de motor uit is.

Een mislukte read of een afwijkende latch maakt confirmed_valid en
vfd_acknowledged direct false; DigitalOutputs_Set retourneert false en de
bestaande outputs_ok-interlock krijgt dit in dezelfde InputHandler-cyclus.
Een succesvolle nieuwe write geldt, zoals voorheen, als driver-ACK; de volgende
ongewijzigde cyclus controleert de latch. De bestaande taakperiode en timeouts
bepalen nog steeds de uiterste foutdetectietijd, inclusief schedulervertraging.

Er zijn drie afzonderlijke bewijsniveaus:

1. output_write_ack_valid: resultaat van de laatste write, historische ACK.
2. output_latch_valid/image/match: ontvangen registerwaarde en vergelijking met
   het gevraagde beeld. Dit is geen fysieke pin- of configuratieregistercontrole.
3. Bestaande contactor-/11K3-/safety-feedback: afzonderlijke fysieke ingang.

outputs_ok/confirmed_valid is het laatste driverbewijs (write-ACK of passende
readback). De bestaande legacy output_ack_* velden behouden die functie;
de nieuwe velden tonen expliciet het verschil. Write errors, read errors en
latch mismatches hebben afzonderlijke tellers. Er zijn geen DO-retries of
reset/re-init van een actief uitgangsbeeld toegevoegd. Na communicatieherstel
blijft de bestaande gelatchte fout/reset/fysieke-startprocedure van kracht.

## Driver, begrenzing en diagnostiek

Sensoren gebruiken een niet-wachtende mutexacquisitie. InputHandler heeft prioriteit
5, SensorTask 3; de bestaande mutex heeft priority inheritance. Sensorwerk dat
al onderweg is kan niet veilig worden afgebroken. Het lagere sensorbudget beperkt
dat blokkeringsvenster tot nominaal 2 ms plus tick/scheduler/API-overhead.
De productieconfiguratie gebruikt een 1 ms RTOS-tick; een compilecontrole voorkomt
een timeout die naar nul ticks afrondt.

Een snelle ESP_FAIL bij een niet-wachtende sensorread mag één retry krijgen.
De tweede poging krijgt alleen het resterende budget (afgerond naar hele ticks).
Geen retries bij timeout, driverfout, bus-busy, writes of DO-registerreadback.
Een NACK op 0x20 wordt daardoor niet door een stille tweede poging verborgen.
Er is geen automatische busreset, snelheidswisseling of motorherstart.

I2CBusDiagnostics verzamelt per adres onder een korte kritieke sectie:

- OK, BUSY, NACK, TIMEOUT en DRIVER; ESP_FAIL wordt als NACK geclassificeerd.
- Ruwe ESP-IDF-code van de laatste call en de laatste mislukking met tijdstempel.
- Laatste en maximale mutexwachttijd, laatste en maximale driverduur,
  maximale complete API-callduur.
- Totale driverduur (64-bit), aantal driverpogingen, retries, geslaagde recoveries
  en uiteindelijk mislukte calls.

De driverduur wordt gemeten direct rond de IDF-call(s), vóór vrijgave van de
busmutex, en sluit mutexwachten uit. Het gemiddelde is totale driverduur gedeeld
door het aantal verworven transactiecalls (wire_calls - retries); een call kan
één retry omvatten. max_us gebruikt dezelfde definitie. Het zijn softwaremetingen
rond de IDF-driver, geen oscilloscoopmeting van uitsluitend SCL-klokken.
OK telt succesvolle API-calls; NACK telt mislukte wirepogingen, ook vóór een
succesvolle retry. BUSY veroorzaakt geen wirepoging. Tellers lopen sinds boot en
zijn 32-bit, totale microseconden 64-bit.

STATUS gebruikt i2c_diag_v=1, i2c_hz en per geconfigureerd adres een compact
getypeerd i2c_XX-token. De parser controleert formaat/versie en de HMI behoudt de
uitklapstand. Stale/simulatie toont geen actuele fysieke waarden. Een aangepast
ABP2-adres wordt dynamisch opgenomen. De transmitbuffer is statisch 12 KiB;
de backendlimiet is dezelfde en de asyncio-streamlimiet 16 KiB. Truncatie wordt
niet als geldige STATUS verzonden. Er wordt geen I2C-verkeer voor de diagnostiek
zelf gegenereerd. RTC-tellers blijven dus nul zonder RTC-drivergebruik.

## Busbelasting: berekening en deterministische hostbenchmark

Nominaal, zonder START/STOP-overhead, clock stretching, retries of software:
Pitot-read vier bytes = 45 klokpulsen; DO-write register+byte = 27;
DO-latchread register+repeated START+byte = 36.

| Strategie bij stabiel beeld, 100 sensor- en 100 DO-calls/s | Klokpulsen/s | 100 kHz | 400 kHz |
|---|---:|---:|---:|
| Voorheen: Pitot-read + DO-write | 7200 | 7,20% | 1,80% |
| Nu: Pitot-read + DO-latchread | 8100 | 8,10% | 2,025% |

De C-hostbenchmark voert de productie-I2CBus-code uit met een deterministische
IDF-fake die draadduur uit bytes en ingestelde klok berekent. Microseconden worden
naar boven afgerond. Verwacht/waargenomen: 81.000 us voor 100 kHz en 20.300 us voor
400 kHz per 100 sensorreads + 100 latchreads; 8,10% en 2,03% van één seconde.
Dit is ongeveer 72% minder nominale draadbelasting dan de oude 100-kHz-strategie.
Bij voortdurend gewijzigde outputs blijft de write-strategie gelden. Op optionele
mux-/taptransacties of RTC-verkeer zijn deze percentages niet gebaseerd.

## Reproduceerbare softwarevalidatie

```powershell
& ESP32/tests/run_host_tests.ps1
$env:PYTHONPATH='HMI'
$env:QT_QPA_PLATFORM='offscreen'
& .\.venv-hmi\Scripts\python.exe -m unittest discover -s HMI/tests -q
& .\.venv-hmi\Scripts\python.exe -m unittest discover -s ESP32/tests -q
& C:\Users\robbe\.platformio\penv\Scripts\platformio.exe run -d ESP32 -e waveshare_s3 -e waveshare_s3_i2c100
```

De gerichte C-tests dekken beide klokken, begrensde retries, timeouts, ruwe codes,
wachttijden, registerread zonder retry, 100 Hz Pitot-calls, geen duplicate DO-writes,
latchverschil, NACK op 0x20/0x28 en herstel. Een aparte test gebruikt echte Windows-
threads met de productiebus en DO-code: geblokkeerde sensor, wachtende DO-writer,
overgeslagen gelijktijdige sensorcall, latch-NACK, STOP/FAULT-off en herstel naar off.
Dat test arbitrage; de fake vertraagt transacties expres en meet geen ESP32-jitter.
De bestaande controltests bewaken 10 ms regelperiode, gemiste deadlines, STOP,
FAULT, fysieke start, noodstop en geen automatische herstart.

HMI-tests controleren numerieke doorgifte, versie/ongeldig formaat, alle zes adressen
binnen de ruimere framegrens, actuele foutcodes, custom tap-adres en stale-weergave.
Een bestaande test met een exact verwachte lijst diagnostiekgroepen is bijgewerkt
voor de nieuwe I2C-groep. De bestaande hostrunner -Case-filter is gerepareerd;
voorheen kon een geselecteerde run ten onrechte 0/0 tests opleveren.

## Definitieve testresultaten en gewijzigde bestanden

- Huidige volledige C-hostsuite: 28/28 executables geslaagd, inclusief
  i2c100, i2c en de nieuwe i2c_concurrency-test.
- Huidige volledige HMI-suite: 113 tests geslaagd.
- ESP32 Python source-layoutcheck: 1 test geslaagd; een ontbrekende
  functiekopcomment in de eerdere Pitot-diagnostiek is daarvoor aangevuld.
- Firmware waveshare_s3 (400 kHz) en waveshare_s3_i2c100: beide geslaagd.
  RAM in de geteste werkmap: 49.684 bytes; flash circa 565 kB.
- Hostdraadmodel: 81.000 us (100 kHz), 20.300 us (400 kHz).
- Live TCP/hardware: timeout, geen fysieke meetresultaten en geen upload.

Testlogs staan lokaal onder ESP32/.pio/i2c-performance-host-tests.log en
ESP32/.pio/i2c-performance-build.log; dit zijn genegeerde buildartefacten.

Wijzigingen voor deze opdracht:
AppConfig.h, DigitalOutputs.h/.c, I2CBus.h/.c, InputHandlerTask.c,
PiCommunicationTask.c (I2C-/uitgangsdiagnostiek en buffer), main.c (I2C-log),
platformio.ini, host/driver/i2c.h, run_host_tests.ps1, test_i2c.c,
test_i2c_concurrency.c, test_outputs.c, backend/esp32.py (I2C-parser en framelimiet),
native_diagnostics.py (I2C-/uitgangsweergave), test_i2c_diagnostics.py,
test_esp32_ethernet.py (framelimiet), test_native_hmi.py (nieuwe groep),
PressureSensors.c (uitsluitend ontbrekende functiekopcomment) en dit rapport.

Tijdens uitvoering verschenen tevens gelijktijdige wijzigingen in V20Modbus.c,
TaskData.h, V20-telemetrie en bijbehorende tests/buildlogs. Deze zijn niet door
deze I2C-opdracht aangebracht of teruggedraaid. De testresultaten hierboven
betreffen de gezamenlijke actuele werkmap; de 28 tests omvatten ook die extra
V20-schedulertest. De veiligheidslogica is voor deze opdracht niet gewijzigd.

## Fysieke commissioning en A/B-meting (nog uit te voeren)

1. Stel de aangesloten inventaris vast. Controleer voeding, massa, pull-ups en
   SDA/SCL-stijgtijd met motor uit. Voor Fast-mode geldt maximaal 300 ns rise time
   volgens de TCA9554-datasheet. Vergelijk beide buildvarianten op dezelfde bekabeling.
2. Leg per variant bij stilstaande motor minimaal tien minuten vast: i2c_hz,
   sensor- en DO-OK/NACK/timeout/retry/recovered, avg/max us, max wait/call us,
   Pitot-volgnummer, control_hz, execution_us, overruns en missed_deadlines.
   Gebruik tellerdelta's per meetinterval; gemiddelden sinds boot zijn geen p95.
3. Controleer 100 Pitot-transacties/s en circa 20 gepubliceerde gemiddelden/s.
   Bedien toegestane DO-veranderingen en vergelijk requested image, write-ACK,
   latch en afzonderlijke fysieke feedback. Observeer de DO1-off-overgang met
   logic analyzer/contactmeting tijdens STOP en FAULT.
4. Injecteer in een veilig geïsoleerde testopstelling afzonderlijk NACK op 0x20
   en 0x28, een korte kabelonderbreking en gelijktijdig sensor/DO-verkeer. Een
   onbekende DO-status tijdens RUNNING moet via de bestaande interlock afvallen;
   een Pitot-uitval behoudt de bestaande timeout. Meet de werkelijke reactietijden.
5. Herstel de verbinding. Controleer dat een gelatchte fout niet vanzelf RUN wordt
   en dat bestaande fysieke reset/startautorisatie vereist blijft. Test motor/EMC
   alleen volgens de bestaande commissioningprocedure.
6. Bij verslechterde NACKs, timeouts of deadlines: gebruik waveshare_s3_i2c100.
   Er is bewust geen runtime snelheidswisseling. Vergelijk eerst de oorzaak
   (kabelcapaciteit, voeding, pull-ups, EMC) voordat 400 kHz wordt vrijgegeven.
