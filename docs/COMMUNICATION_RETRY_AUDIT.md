# Communicatie: timeout, retry en onbekende uitvoering

Datum: 9 november 2026. Onderzocht: V20Modbus, I2CBus, DigitalOutputs,
InputHandlerTask, SensorTask en de bestaande ControlTask/StateMachine-interlocks.
Bestaande lokale wijzigingen zijn behouden. Geen commissioning-flags,
veiligheidsingangen, watchdogparameter of resetvoorwaarden zijn aangepast.

## Bevinding uit de aangeleverde hardwarelog

De eerste fout op 81534 ms is INTERLOCK vanuit RUNNING. De snapshot heeft
outputs_ok=0, do1_acknowledged=0 en do1_ack_valid=0, terwijl Modbus errors=0,
timeouts=0 en een gezonde recente status meldt. De WRITE_CONTROL-transactie
is pas 9 ms onderweg; pending=1/result=0 is geen aangetoonde Modbusfout.
De daaropvolgende outputwrite slaagt weer; de foutvergrendeling blijft terecht
actief. DO1-uit waargenomen op 81544 ms en OFF2 op 81586 ms zijn samples,
geen bewijs van fysieke flankvolgorde. Sensor-errors stijgen ook (53 naar 63),
maar bewijzen geen specifieke busoorzaak. Zonder driverresultaat kan deze log
mutex-busy, NACK en timeout niet onderscheiden. Registerwaarde 500 bevestigt
niet automatisch de P2014-mapping: mapping_confirmed blijft ongewijzigd 0.

## Modbusbeleid

| Instelling | Waarde / reden |
|---|---|
| Responstimeout | 80 ms vanaf succesvolle UART-enqueue; bij 115200 8E1 zijn de korte frames veel korter. De hardwarelog toont RTT-max 21,305 ms; hosttests toetsen ook 70 ms plus enqueuevertraging. |
| Service | Bestaande 10 ms ControlTask, niet-blokkerend RX. |
| Leesretry | Maximaal 1 extra poging per mislukte transactie, alleen timeout, CRC of UART-RX-fout. |
| Quarantaine | 160 ms (2 maal responstimeout), late bytes worden weggegooid en verlengen de stilte. |
| RUN-budget | Retry alleen als leeftijd laatste process-data-TX + quarantaine + 80 ms + 30 ms reserve < 450 ms. Na quarantaine opnieuw controleren. Bij ruis wordt ook tijdens quarantaine vergrendeld zodra het resterende budget onvoldoende is. |
| Write-retries | Geen replay van WRITE_CONTROL, ook geen replay van RUN, setpoint of fault-ACK. |
| STOP | Bestaande grens van 3 OFF1-pogingen, ON=0 en setpoint=0; blijft onder bestaande commissioningvoorwaarden. |

READ_STATUS, READ_REFERENCE en READ_WATCHDOG zijn alleen-lezen en mogen onder
bovenstaande voorwaarden eenmaal opnieuw. Modbus-exceptions, verkeerde frames,
TX-fouten en configuratiemismatches worden niet automatisch herhaald als retry.
Een tweede leesfout of tekort aan RUN-budget behoudt de bestaande foutvergrendeling.
Tijdens initialisatie blijft de bestaande alleen-lezen bootstrap beschikbaar;
herhaalde diagnostiek is geen onbeperkte retry binnen één transactie en autoriseert
geen RUN. De bestaande INIT-timeout blijft de bovengrens van systeeminitialisatie.

Een mislukte WRITE_CONTROL heeft onbekende uitvoering, ook als de UART-TX faalt:
een deel van het frame kan zijn verzonden. De controlword-bevestiging vervalt,
arm/RUN vervallen, fault-ACK wordt afgebroken en de run-lock blijft vergrendeld.
De driver probeert begrensd OFF1 en daarna READ_STATUS wanneer communicatie dat
toelaat. Een statusread observeert de aandrijving; hij bewijst niet dat precies
de onbekende write uitgevoerd werd en herstelt geen START-autorisatie.
STOP, noodstop, ongeldige/verouderde aanvraag en verlies van safety worden vóór
het retry-schedulen verwerkt. STOP annuleert een wachtende diagnostische retry.
De bestaande hardware- en software-interlocks wachten niet op herstel van Modbus.
Een in-flight RTU-frame wordt zoals voorheen niet afgebroken.

P2014=500 ms wordt niet gewijzigd. De driver wacht dus niet meer 1500 ms op een
antwoord; de legacy MODBUS_RESPONSE_TIMEOUT_MS=1500 is expliciet als ongebruikte
compatibiliteitsconstante gedocumenteerd. V20_RESPONSE_TIMEOUT_MS=80 is actief.
Geen aanname dat een FC03-read de process-data-watchdog ververst: het budget rekent
vanaf process-data-TX. Dit is een softwarebudget met schedulerreserve, geen bewijs
van fysieke watchdogreactie of veiligheidsfunctie. De commissioning moet de echte
P2014-instelling en maximale slave/bridge-latentie bevestigen. RTU heeft geen
transactie-ID: willekeurig laat ontvangen frames buiten de gevalideerde
quarantainegrens kunnen niet sluitend worden gecorreleerd.

MotorStatus en de periodieke seriële diagnose bieden retries,
recovered_transactions en final_comm_errors. errors/timeouts blijven ruwe
mislukte pogingen tellen; herstel wist historie niet. final_comm_errors telt
niet-herstelbare transacties/exceptions, UART-initfouten en stale-response-events.

## I2C-beleid en busbelasting

| Resultaat | Betekenis / actie |
|---|---|
| OK (0) | Driver bevestigt transactie. Geen fysieke relayfeedback. |
| BUSY (1) | Mutex niet verkregen. Er is geen busopdracht verstuurd. |
| NACK (2) | ESP_FAIL volgens de gebruikte ESP-IDF legacy-driver. |
| TIMEOUT (3) | ESP_ERR_TIMEOUT van de busdriver. |
| DRIVER (4) | Niet-geïnitialiseerd of ander driverresultaat; ruwe foutcode behouden. |

De bustimeout blijft 10 ms; blokkerende outputcalls wachten daarnaast maximaal
10 ms op de mutex (bestaand maximum circa 20 ms inclusief bus, plus scheduling).
TryRead/TryWrite wachten niet op de mutex. Per 7-bit adres worden resultaten,
laatste/maximale transactieduur, maximale totale calltijd inclusief mutex,
retries, herstel en definitieve fouten geregistreerd. De laatste fout met code
en tijd blijft na succesvolle vervolgtransacties beschikbaar. BUSY heeft
transactieduur 0. De periodieke I2CDiag-regels tonen alle gebruikte adressen;
GetDiagnostics is een gelockte snapshot zonder busverkeer.

Alleen TryRead mag een snelle NACK (<1 ms) eenmaal herhalen. De tweede drivercall
heeft maximaal 9 ms wachttijd, zodat beide binnen het oorspronkelijke nominale
10 ms budget blijven (CONFIG_FREERTOS_HZ=1000; inclusief tick/schedulerjitter niet
een harde wall-clockgarantie). Dit geldt voor de bestaande pitot-/ABP2-datareads,
zonder conversietrigger of mux-write te herhalen. BUSY, bus-timeout, driverfouten,
langzame NACKs en alle writes krijgen geen inline retry. Muxselectie en
conversiestappen behouden hun bestaande incrementele taakplanning. Nieuwe
gebruikers van TryRead moeten eveneens reads zonder bijwerkingen gebruiken.

Outputwrites lopen op Core 0 elke 10 ms; de sensorfase ligt 5 ms vóór control,
maar InputHandlerTask gebruikt een onafhankelijke delay-until. Overlap is dus
mogelijk en de mutex voorkomt gelijktijdige wiretransacties. Een sensor-Try-call
kan daardoor een slot missen; een vastgelopen sensorcall kan de outputcall laten
wachten. Bij 100 kHz kost de outputwrite (adres, register, byte inclusief ACKs)
nominaal 27 klokpulsen = 270 us: circa 2,7% busbezetting bij 100 writes/s,
exclusief START/STOP, stretch en softwareoverhead. Een pitotread van 4 bytes
kost circa 450 us. Gezond nominaal verkeer verklaart daarmee geen 10 ms timeout;
werkelijke busstretch, ruis en contention moeten uit de nieuwe adrescounters en
hardwaremeting blijken. Geen ongefundeerde conclusie dat de taken altijd botsen.

De herhaalde outputwrite is tevens de bestaande 10 ms ACK-bewaking. Het ongewijzigde
outputbeeld cachen en writes overslaan zou verlies van de expander later detecteren.
Daarom blijft die probe bestaan. Er komen geen extra writes/readbacks of busresetloops
bij; normale sensortransacties krijgen evenmin extra verkeer. Dit voorkomt extra
belasting zonder de bestaande bewaking af te zwakken.

Bij elke onbevestigde outputwrite worden confirmed_valid én vfd_acknowledged
false. InputHandlerTask publiceert outputs_ok=false zonder retry of respijttermijn.
ControlTask/StateMachine behouden de bestaande onmiddellijke INTERLOCK-afhandeling.
Een volgende geslaagde write herstelt alleen de busbevestiging, niet de fault/reset-
vergrendeling en niet de START-autorisatie. De bus wordt niet automatisch gereset.

## Validatie

- Hostfakes voeren productie-C uit met UART-, I2C-, klok- en RTOS-mocks.
- Eenmalig verloren leesantwoord en CRC herstellen met precies één retry en
  passende herstelcounter, zowel gestopt als geautoriseerd draaiend.
- Twee mislukte leespogingen vergrendelen; herstelde bus wist de run-lock niet.
- Ruis die de quarantaine verlengt veroorzaakt een fault vóór het RUN-budget
  verstrijkt; retries leunen niet op een timeout boven 500 ms.
- Verloren/partiële RUN-echo, UART-fouten, STOP, noodstop en stale aanvraag tijdens
  retry: geen ongeautoriseerde START of replay van RUN/fault-ACK.
- I2C: mutex-busy zonder wirecall, snelle en langzame NACK, timeout, driverfout,
  eenmalig sensorherstel en 1000 opeenvolgende outputtimeouts; geen write-retries.
- Outputtests controleren ontbrekende ACK, ongeldige DO1-bevestiging, polariteiten,
  gelijktijdige writers en mislukte initialisatie. Bestaande controltests toetsen
  veiligheidsfeedback, noodstop, eerste fout en geen automatische herstart.
- Alle 25 hostexecutables slagen. Python-bronlayouttest slaagt.
- PlatformIO: waveshare_s3, waveshare_s3_sim en extra waveshare_s3_frequency bouwen.

Logs: output/communication-host-tests.log, output/communication-python-tests.log,
output/communication-build.log. Geen firmware geflasht of motor aangestuurd.
Werkelijke NACK/clock-stretch/EMI, fysieke relaisflanken en hardwarewatchdog zijn
niet op de opstelling geïnjecteerd; de meegeleverde log ondersteunt de analyse,
maar vervangt die metingen niet.

## Primaire bronnen

- Siemens V20 Operating Instructions, P2014 en Modbus-registermapping:
  https://support.industry.siemens.com/cs/attachments/67267484/v20_operating_instructions_complete_en-US_en-US.pdf
- ESP-IDF ESP32-S3 legacy I2C API, ESP_FAIL (geen ACK), ESP_ERR_TIMEOUT en mutexgebruik:
  https://docs.espressif.com/projects/esp-idf/en/release-v5.1/esp32s3/api-reference/peripherals/i2c.html
