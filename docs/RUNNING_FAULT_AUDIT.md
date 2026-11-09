# Windtunnel: RUNNING/STARTING naar FAULT â€” audit en correcties

Datum: 9 november 2026. Repository: AEM_windtunnel2026. Onderzoek en tests zijn uitgevoerd zonder upload naar de ESP32 en zonder motor- of live-V20-aansturing.

## Vastgestelde oorzaken en grenzen van het bewijs

### MODBUS, transactie WRITE_CONTROL (laatste melding)

De snapshot toont een onbevestigde procesdatawrite: laatste geldige echo 573835 ms, nieuwe write gequeued rond 573854 ms, FAULT gedetecteerd 573945 ms. De laatste write is ongeveer 91 ms voor de fout aangevraagd; het vorige antwoord was toen ongeveer 110 ms oud. `TIMEOUT=4` en `WRITE_CONTROL=4` verwijzen naar de eigen enums, niet naar een V20-foutnummer.

`receive_pending()` ontvangt nonblocking en verzamelt fragmenten. Wanneer geen complete reactie beschikbaar is bij de timeoutcontrole, roept het `fail_transaction()` aan. Die maakt transportgezondheid ongeldig, disarmt, vergrendelt RUN en gaat naar een late-response-quarantaine. `Control_UpdateDiagnostics()` genereert vervolgens dezelfde cyclus MODBUS. Dat gebeurt dus ook na Ã©Ã©n onbevestigde write, terwijl een ouder antwoord nog recent was. Een write kan al zijn uitgevoerd als alleen de echo verloren gaat. Een automatische RUN-retry of een nog steeds gezonde status op basis van een oude echo zou die onzekerheid maskeren; deze veilige reactie is behouden.

Aantoonbaar timingdefect: de oorspronkelijke antwoordtimer begon bij binnenkomst in de service, vÃ³Ã³r de UART flush/TX-aanroepen. Mutex-/buffertijd ging daardoor van het antwoordvenster af. De timer start nu nadat de TX-API het frame heeft geaccepteerd. De 80 ms antwoordtimeout is niet verhoogd. Dit is enqueue-tijd, geen gemeten fysieke TX-eindflank. De exacte reden waarom de fysieke echo van sequence 6 ontbrak kan niet worden bewezen met de oude snapshot. Het timingdefect is geen bewezen volledige verklaring van die specifieke uitval.

Nieuwe metingen geven per transactie identiteit/type, start/deadline/einde, verwachte/ontvangen lengte, eerste/laatste waargenomen RX, RTT, maximale RTT, enqueue-duur, maximale serviceafstand, deadlineoverschrijding, UART-foutflags, vÃ³Ã³r TX weggegooide bytes en TX/RX-hex. UART-flags: 1=FIFO-overflow, 2=driverbuffer vol, 4=pariteit, 8=framing. Bij zo'n fout tijdens een lopende transactie wordt die ongeldig verklaard en veilig afgeschakeld. De eventqueue wordt door dezelfde eigenaar met maximaal 16 nonblocking reads per service geleegd; er is geen nieuwe driver- of callbacktask.

RTT en RX-tijden zijn waarnemingen op servicemomenten en bevatten polling-/schedulertijd. Ze zijn geen ISR- of logic-analyzer-timestamps. Een compleet antwoord dat bij een vertraagde poll beschikbaar is wordt volgens de bestaande ontvangstvolgorde gecontroleerd vÃ³Ã³r de absentie-timeout; `deadline_lateness_us` maakt die late observatie zichtbaar. De tests zijn geen fysieke latency-meting.

### CONFIGURATION tijdens STARTING

De oude `Control_UpdateStateMachine()` deed eerst READY -> STARTING en controleerde daarna het gekozen AUTO-model, vÃ³Ã³r `V20Modbus_Arm()` en vÃ³Ã³r publicatie van een RUN-verzoek. Daarom bleef het laatste controlword 0x047E bij de fout. Dit was geen afkeuring van 0xEB31 en geen verwachting dat operation-enabled al vÃ³Ã³r RUN aanwezig moest zijn.

In de onderzochte configuratie is de frequentie-PI geselecteerd, met gain=0 en tau=0. `FrequencyModel_Design()` keurt deze waarden terecht af; `frequency_controller_ready` blijft false. VALIDATED/COMMISSIONED=1 maakt nulwaarden niet bruikbaar. De oude generieke fout maakte dit onzichtbaar. Het aangeleverde oude snapshot bevatte geen modus/modelparameters; de exacte ondervoorwaarde van het destijds geflashte image kan daarmee niet achteraf afzonderlijk worden bewezen.

`Control_UpdateConfiguration()` centraliseert nu de concrete redenen. De first-faultsnapshot bevat `configuration_failure_reason`, modus, controltype, controllergereedheid, kalibratie-/commissioningstatus en modelparameters. Voor de huidige nulwaarden verschijnt `FREQUENCY_MODEL_GAIN_INVALID`; ook tau, delay, lambda, kalibratie en torquecommissioning hebben eigen redenen.

De state machine controleert de mode-afhankelijke startconfiguratie atomair bij een toegestane fysieke START in READY. Ongeldige configuratie geeft CONFIGURATION vanuit READY, zonder STARTING of arm. READY blijft de bestaande hardware-/safetygereedheid betekenen, zodat MANUAL in READY gekozen kan worden; `configuration_start_ready` toont afzonderlijk of de gekozen bedrijfsmodus kan starten. MANUAL vereist geen AUTO-model of fan-LUT. Een geldig AUTO-model blijft alle bestaande voorwaarden vereisen. Er zijn geen modelwaarden of commissioningflags automatisch ingevuld.

`watchdog_config_known=0` en `watchdog_config_valid=0` zijn niet blokkerend wanneer de P2014-registermapping niet als bevestigd is geconfigureerd. Bij een vereiste mapping blijven verificatie en geldige configuratie verplicht. 0xEB31 is een geldige stilstaande ready-status; operation-enabled wordt pas na RUN verwacht en met een nieuw statusantwoord bevestigd.

### PI_CONNECTION

De oorspronkelijke ControlTask gebruikte een kloktick van vÃ³Ã³r het lezen van de heartbeatqueue. Een publicatie op de andere core kon nieuwer zijn dan die tick. `1000 - 1001` in unsigned ticks wordt 4294967295, zodat een verse heartbeat onterecht oud leek. De klok wordt nu na het lezen van de producentgegevens genomen; dezelfde correctie geldt voor sensor-, safety- en motortijdvergelijkingen.

De Pi-heartbeat heeft nu een eigen PING-antwoordpad en wacht niet op de HMI-commandolock. De socketreader wacht niet meer op de statuscallback; een begrensde latest-statusqueue verzorgt de overdracht. Een langzame callback kan tussenliggende statusframes overslaan; dit voorkomt een onbegrensde backlog. Het is geen lossless meettransport. Synchrone blokkering van de gehele Python-eventloop/het OS kan nog steeds een echte heartbeatonderbreking veroorzaken en wordt niet gemaskeerd.

Alleen een geldig PING ververst de heartbeat. TCP-connect, laatste bericht en heartbeat zijn afzonderlijke gegevens. Een disconnectgeneratie blijft zichtbaar wanneer disconnect/reconnect tussen twee controlreads plaatsvindt. Een herstelde verbinding heft FAULT niet op. De monotone timeout blijft 2000 ms.

## Modbus-flow, mapping en periodieke aansturing

Er is Ã©Ã©n RTU-eigenaar: ControlTask -> V20Modbus_Service(). Er zijn geen parallelle v20.c/v20Modbus.c-drivers. Het motorverzoek is een overwritequeue met het laatste gewenste setpoint, geen onbeperkte transactiewachtrij. PH_WAIT_RESPONSE verhindert overlap. Read-only referentie-/watchdogdiagnose gebeurt tijdens bootstrap/recheck; normale cyclische reads zijn statusfeedback en geen willekeurige parameterpolls.

Procesdatawrites zijn onafhankelijk van setpointveranderingen. Het ontwerpdoel is nu 100 ms (`V20_COMMAND_PERIOD_MS`), gemeten vanaf TX-acceptatie, en due writes krijgen voorrang op normale statuspolling. Tijdens STARTING/RUNNING worden STW/HSW cyclisch verzonden. Tijdens gecontroleerd STOP, met behouden operational permissive, worden OFF1/zero-telegrammen eveneens circa 100 ms herhaald. Een uitgeschakelde permissive gebruikt de bestaande stopped-heartbeatgrens. Spoed-STOP, begrensde fault-ack en bootstrap houden hun bestaande prioriteit en voorwaarden.

De 10 ms servicestappen, framepauze en een al lopende read kunnen het doel overschrijden: in de mockduurtest was de maximale RUN- en STOP-afstand 110 ms. Een lopende transactie wordt nooit afgebroken om een write voor te trekken. Bij vertraagde antwoorden zijn grotere afstanden mogelijk; de 80 ms transactietimeout en veilige uitschakeling blijven leidend. Er wordt geen fysieke absolute 100 ms-garantie geclaimd.

FC16 schrijft twee registers vanaf PDU-adres 99 (0x0063): 40100=STW, 40101=HSW. De 8-byte echo wordt gecontroleerd op slave, functie, startadres, aantal en CRC. Status/snelheid komen uit 40110/40111 (PDU 109/110). UART1 gebruikt TX17, RX18 en RTS/DE21 in ESP-IDF RS485 half-duplex; die pinnen komen overeen met Waveshare. Richting wordt door de UART-driver afgehandeld, niet door een tweede applicatietaak.

De nieuwe transactie-ID is lokale diagnostiek: Modbus RTU transporteert geen transaction-ID. Matching blijft daarom Ã©Ã©n lopende transactie plus type/lengte/adres/count/CRC en de bestaande quarantaine. Een willekeurig veel te laat duplicaat van een identieke write-echo kan zonder fysieke maximale latency niet uniek worden onderscheiden. Dit blijft een commissioningvoorwaarde.

Geldige reads, geldige procesdata-echo's, exceptions, CRC-/framefouten, timeouts en ontvangen bytes tijdens quarantaine worden afzonderlijk geteld. `late_replies` telt nonempty UART-readchunks tijdens quarantaine, niet bewezen complete telegrammen. Een protocolgeldige exception telt niet als geslaagde read/write. Een procesdata-echo is bevestiging van de protocolwrite, geen fysiek bewijs van motorwerking. Statusantwoorden krijgen eigen sampletijden.

Cumulatieve maximale gaps, ook zeer grote waarden na stilstand/recheck, zijn uitsluitend diagnostisch. Zij worden nergens als actuele watchdogvoorwaarde gebruikt. Het zendritme gebruikt de laatste TX-tijd; gezondheid gebruikt recente status/antwoorden en de transactie-uitkomst. P2014[0]=500 ms is niet gewijzigd of uitgeschakeld. Siemens beschrijft P2014 als telegrambewaking; de extra teller voor STW/HSW-echo's is een conservatieve, afzonderlijke procesdatameting.

## First-fault en behouden veiligheid

De snapshot wordt in RAM Ã©Ã©n keer per foutepisode vastgelegd voordat ControlTask disarm/shutdown publiceert. Hij bevat veiligheid/raw DI, heartbeat/TCP/link, motor- en transactiedata, gevraagde/bevestigde DO1 en de laatste acht veranderde toestand/status/outputwaarnemingen. Vervolgfouten en zelfs een latere E-stop-prioriteit herschrijven deze eerste diagnostische oorzaak niet; de actieve E-stop-afhandeling blijft wel voorrang houden.

De snapshot blijft na fysieke RESET als historisch bewijs beschikbaar tot een volgende foutepisode. Formatting/serial logging gebeurt in main, TCP-logging in PiCommunicationTask; niet in een ISR of het controlpad. Er wordt een apart FIRST_FAULT-frame gestuurd, eenmaal per sequence per TCP-verbinding. De HMI exposeert het via `/api/status` onder `esp32.first_fault`. De HMI-framelimiet is gelijkgetrokken met de bestaande ESP32-buffer van 6144 bytes.

De latere `fault_do1_off_observed_ms` en `fault_off2_observed_ms` zijn samplewaarnemingen. De snapshot toont of OFF2 al gemeld was terwijl DO1 nog bevestigd aan stond. Exacte fysieke relais-/OFF2-volgorde kan niet worden afgeleid bij gelijktijdige of ontbrekende samples. De core-0 veiligheidstaak kan door een onafhankelijke safetyfout al hebben afgeschakeld vÃ³Ã³r ControlTask die waarneemt.

Behoud: fysieke START/RESET, geen remote START, geen automatisch herstarten of resetten, E-stop/safety-feedback/OFF2, gezonde gecontroleerde OFF1-ramp met behouden permissive, onmiddellijke afschakeling bij echte onzekerheid/fout en begrensde stopretries. Geen aangepaste besturingswoorden om een blokkering te omzeilen.

## Betrokken bestanden

- ESP32/src/ControlTask.c: tijdregistratie, configuratiepreflight, eerste-foutcapture en historie.
- ESP32/src/StateMachine.c en include/StateMachine.h: atomaire geldige fysieke START naar STARTING.
- ESP32/src/V20Modbus.c: TX-tijdbasis, cyclische prioriteit, transactie-/UART-diagnostiek.
- ESP32/include/TaskData.h: gedeelde diagnose- en snapshotstructuren.
- ESP32/src/FaultSnapshot.c en include/FaultSnapshot.h: gedeelde deferred formatter.
- ESP32/src/PiCommunicationTask.c en main.c: transport en serial uitvoer.
- HMI/backend/esp32.py: onafhankelijk heartbeat-/antwoordpad, callbackoverdracht en diagnoseparser.
- ESP32/include/AppConfig.h: uitsluitend de cyclische commandoperiode 10 -> 100 ms is onderdeel van deze correctie. De al aanwezige servicebudget- en gelijktijdige handmatige rampwijzigingen zijn behouden.

## Verificatie

De automatische tests gebruiken mocks/simulatie, zonder live V20.

- PlatformIO: `waveshare_s3`, `waveshare_s3_sim` en `waveshare_s3_frequency` alle drie SUCCESS op de definitieve driver. Hardwarevariant: RAM 30660/327680 bytes; flash 559637/3145728 bytes.
- `ESP32/tests/run_host_tests.ps1`: 23/23 executables geslaagd. Na de laatste uitsluitend Modbus-RX-diagnosecorrectie zijn alle vijf betrokken driver-/simulatie-executables opnieuw geslaagd (v20, v20_inhibited, v20_recovery, v20_fault_ack, v20_simulation).
- HMI: `../.venv-hmi/Scripts/python.exe -m unittest discover -s tests`, vanuit HMI: 108 tests, OK. De gerichte TCP/parser-suite bevat 15 tests.
- Source-layouttest: 1/1 geslaagd. I/O-configuratiecompiletest: alle zeven geldige/ongeldige overlays conform verwachting gecompileerd of afgekeurd.
- `git diff --check`: schoon. De toolchaincompiletests liepen met toegang tot de bestaande PlatformIO-cache; er is geen upload uitgevoerd.
- Duurtests: 60000 stappen van 10 ms (10 gesimuleerde minuten) met constant setpoint/heartbeat; maximale normale procesdata-TX-gap 110 ms. Gecontroleerde STOP: 10 gesimuleerde seconden, eveneens 110 ms maximale OFF1-gap. Dit zijn gemeten mockresultaten, geen hardwaregaranties.
- Latencytest: 7000 us enqueuewachttijd en 70000 us vertraagd antwoord; de antwoorddeadline blijft 80 ms ná enqueue. Partiële 3/8-byte echo loopt veilig in timeout. Vier UART-foutklassen worden apart geïnjecteerd en blijven fail-closed. Volledige pre-faulthistorie met raw frames past in de serial snapshotbuffer van 4096 bytes.

De HMI-suite meldt bestaande niet-fatale dependency-/fontcachewaarschuwingen en de verwachte tekst van de gemockte bezette-poorttest; de uiteindelijke unittestresultaten zijn OK.

Gedekt: 10 gesimuleerde minuten RUN met periodieke heartbeat en constant 50 Hz; unsigned tickrace en wrap; 1999/2000 ms grens; disconnect/reconnect en simultane Modbus/Pi-fout; eerste oorzaak vÃ³Ã³r outputshutdown; geen herstart na herstel; RESET/WAIT/PREPARE/READY; exact 0xEB31 -> geautoriseerd 0x047F/zero -> enabled; geldige MANUAL/AUTO en ongeldige model-/LUTconfiguratie; onzekere/partiÃ«le write-echo; UART parity/framing/overflow; vertraagd antwoord en gemeten enqueuewachttijd; quarantaine; gecontroleerde STOP met cyclische OFF1; E-stop; DO1- en configuratieasserties.

## Nog fysiek te verifiÃ«ren

Leg bij een volgende gecontroleerde proef de nieuwe FIRST_FAULT-regel en voorafgaande STATUS/latencywaarden vast. Gebruik bij ontbrekende bytes een RS485-logic-analyzer om TX, DE, RX, 8E1, stilte, CRC, werkelijke responslatency en elektrische storingen te onderscheiden. Controleer de ingestelde P2022-responstijd en bekabeling/terminatie/bias volgens de installatie. Wijzig geen watchdog of timeout op basis van alleen een cumulatieve foutteller.

Controleer stack-watermerken, daadwerkelijke maximale service-/looptijden en heartbeat op de ESP32 onder netwerkbelasting. AUTO vereist echte geÃ¯dentificeerde modelparameters en de vereiste fan-LUT/commissioning; deze patch levert die fysieke metingen niet. Historische F72 is zonder tijdcorrelatie geen bewijs voor de laatste uitval.

Bronnen: [Siemens V20 cyclic PZD/registermapping](https://support.industry.siemens.com/cs/attachments/109806055/109806055_LOGO_CIM_ModbusRTU_SinamicsV20_DOC_en.pdf), [Siemens V20 handleiding/P2014](https://cache.industry.siemens.com/dl/files/897/109977897/att_1309600/v1/V20_op_instr_1224_en-US.pdf), [Waveshare RS485-pinnen](https://docs.waveshare.com/ESP32-S3-POE-ETH-8DI-8DO/FAQ).
