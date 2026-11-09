# Pitot-diagnostiek en Arduino-vergelijking (9 november 2026)

## Bevinding en begrenzing

De oorzaak van de geringe reactie in de tunnel is nog niet fysiek vastgesteld.
Een TCP-connectiepoging naar de geconfigureerde ESP32 (192.168.50.2:5000) eindigde
in een timeout. Er is geen firmware geflasht, motor gestart of commissioning-flag
gewijzigd. De volledige fysieke keten moet nog op de opstelling worden gemeten.

De huidige conversie komt overeen met commit
eee018890ce5c7e2d7603f393b26576eafc182b0,
HMI/Arduino/Arduino_Uno_Pitot_HMI.ino: adres 0x28, vier bytes,
status uit de hoogste twee bits, 14-bit druk, 10-90% overdracht,
-1 tot +1 psi (6894.757 Pa/psi), snelheid sqrt(2*abs(dp)/rho).
De ESP32 verwerpt bovendien counts buiten 1638..14746.
Negatieve druk levert door de bestaande absolute waarde ook positieve snelheid.

5 Pa deadband correspondeert bij rho=1.225 met 2.857 m/s.
Daardoor is 0 naar ongeveer 3 m/s een plausibel gevolg van ruis rond de drempel;
dit bewijst geen defect. Een drukcount is ongeveer 1.052 Pa.
Vijfpuntsmiddeling kan meer ruis doorlaten dan tienpuntsmiddeling (voor onafhankelijke
ruis circa sqrt(2) verschil); dit verklaart op zichzelf geen vrijwel afwezige
stationaire respons op een grotere druk.

Arduino: tien reads met 2 ms pauze, daarna 20 ms hoofdluspauze; circa 25 Hz of lager.
ESP32: vijf geldige reads op 100 Hz, dus circa 20 Hz zonder fouten. Het 1 Hz-filter
gebruikt op beide platforms werkelijke verstreken tijd. Arduino doet 200
kalibratiepogingen en middelt de gelukte reads; ESP32 wacht op 200 geldige reads.
De ESP32-kalibratie duurt zonder fouten ongeveer twee seconden.
Kalibreren in luchtstroming neemt de aanwezige druk op in de nuloffset.
Alle bestaande instellingen, druk/snelheidsberekeningen en foutreacties zijn behouden.

## Gegevensroute

ASPD -> I2CBus_TryRead -> SensorLib_PitotStep -> vijfpuntsgemiddelde -> SensorTask
queue (Sample.sequence) -> PiCommunicationTask -> STATUS over TCP ->
backend/esp32.py getypeerde parser -> Controller._esp32_status / status ->
desktop._render_pitot op Raw Data.

De diagnose loopt onafhankelijk van sensor_ok, zodat nulkalibratie en fouten
zichtbaar zijn. De regelaar blijft zijn bestaande Sample en interlocks gebruiken.
Een korte portMUX-kritieke sectie beschermt ieder diagnostiek-snapshot; TCP doet
geen I2C en leest geen onbeveiligde sensorvariabelen.

pitot_raw_count/status zijn de laatste leestransactie; drukstadia horen bij het
laatst voltooide vijfpuntsgemiddelde. pitot_sample_seq telt die gemiddelden,
ook tijdens kalibratie. sample_seq uit de bestaande queue kan door scheduling
een frame afwijken van de apart genomen diagnostiek-snapshot.
Bij ontbrekend I2C-antwoord is status 255 (geen sensorstatus), read_ok=0 en valid=0;
de vorige waarden blijven zichtbaar. Status 0..3 komt rechtstreeks uit de sensor.
zero_samples wordt per afgeronde batch van vijf geldige reads gepubliceerd.
Tijdens kalibratie zijn filter/snelheid nog niet geldig.

Raw Data toont alle gevraagde velden plus read_ok, valid en age_ms. De drie
drukcurves hebben een gedeelde, automatisch passende as inclusief negatieve druk.
De bestaande snelheidsplot wordt ook op Raw Data gebruikt. Duplicaten van een
volgnummer voegen geen grafiekpunt toe; oudere data (>=150 ms, bestaande firmwaregrens), stilstaand
volgnummer (>=300 ms zonder voortgang) en transportuitval worden gemarkeerd. Na een meetgat worden lijnen
onderbroken. Software-simulatie en sim=1 firmwareframes leveren geen fysieke
Pitot-diagnostiek in dit paneel. De UI ververst op zijn bestaande polltempo;
het toont dus een selectie van de circa 20 Hz meetstroom.

## Gewijzigde bestanden voor deze opdracht

- ESP32/include/PressureSensors.h: diagnostiekstruct en snapshot-getter.
- ESP32/src/PressureSensors.c: drukstadia, ruwe bytes, geldigheid en atomic snapshots.
- ESP32/src/PiCommunicationTask.c: diagnostiekvelden in STATUS.
- HMI/backend/esp32.py: numerieke parser voor Pitot-velden.
- HMI/main.py: diagnostiek beschikbaar tijdens kalibratie en fouten.
- HMI/desktop.py: scrollbare Raw Data, waarden, druk- en snelheidsgrafieken.
- ESP32/tests/test_sensors.c: nul, beide druktekens, stap, exacte 1 Hz-respons,
  200 geldige kalibratiemetingen, leesuitval/herstel en status 2.
- HMI/tests/test_pitot_diagnostics.py: parser, echte lokale TCP-socket met
  gefragmenteerde frames, doorgifte naar Controller tijdens kalibratie,
  dubbele/oplopende volgnummerreeksen, grafieken en invalid/stale-weergave.
- docs/PITOT_DIAGNOSTICS.md: dit rapport en meetprotocol.

Andere aanwezige lokale wijzigingen zijn behouden en horen niet bij deze opdracht.

## Praktisch meetprotocol

1. Gebruik de bestaande veilige commissioningprocedure. Houd de tunnel stil en
   de Pitot uit de luchtstroom bij opstarten. Controleer in Raw Data: status 0,
   read_ok=1, zero_samples naar 200, calibrated=1 en daarna valid=1.
2. Controleer dat pitot_sample_seq en sample_seq ongeveer twintig keer per seconde
   oplopen. De desktop tekent alleen nieuw ontvangen volgnummerwaarden. Ontbrekende
   velden wijzen op oude firmware/backend. Een stilstaand volgnummer met werkend
   transport wijst op een acquisitie/publicatieprobleem; oplopende firmwarewaarden
   zonder HMI-updates richten de diagnose op TCP/parser/backend/UI.
3. Noteer tien seconden stilstand: raw_count, raw_pressure, zero_offset, corrected,
   filtered, pressure en snelheid. Druk hoort rond de opgeslagen offset te liggen.
   Sprongen rond +-5 Pa verklaren nul/ongeveer 3 m/s door de bestaande deadband.
4. Start uitsluitend volgens de bestaande bedieningsprocedure. Vergelijk bij enkele
   stabiele, toegestane standen de drie curves. Bij rho=1.225 hoort 10 m/s bij
   ongeveer 61.25 Pa en 20 m/s bij 245 Pa. Het filter hoort de drukstap binnen
   ongeveer 0.48 s voor 95% te volgen, bovenop averaging/telemetrievertraging.
5. Raw count reageert niet: controleer pneumatische aansluitingen, blokkade/lek,
   positie/richting van Pitot en juiste sensor/adres. Raw pressure reageert wel maar
   corrected blijft klein: vergelijk nuloffset en herhaal opstart bij stilstaande
   lucht. Corrected reageert maar filtered niet: leg waarden en volgnummers vast.
   Filtered reageert maar snelheid niet: vergelijk pressure na deadband en pv.
6. Controleer connectoren, voeding en I2C-status bij stilstaande, veilig geïsoleerde
   opstelling. Gebruik geen mond- of persluchtdruk; gebruik desgewenst een geschikte
   begrensde drukbron/referentiemeter binnen het sensorbereik. Verifieer herstel na
   een geautoriseerde sensoronderbreking: invalid/stale zichtbaar, offset behouden,
   nieuwe volgnummers en geldig signaal na herstel. Bestaande fouten vereisen de
   bestaande resetprocedure; er is geen automatische herstart toegevoegd.

## Validatie

- Productiefirmware waveshare_s3: PlatformIO-build geslaagd; geen upload.
- Volledige bestaande C-hostsuite: 25/25 executables geslaagd.
- Uitgebreide sensortest vervolgens opnieuw gecompileerd en geslaagd.
- Volledige HMI-suite inclusief drie nieuwe diagnostiektests: 111 tests geslaagd.
- Live Ethernet/hardware: nog niet geverifieerd, TCP timeout op het ingestelde adres.
- Hosttests gebruiken deterministische busantwoorden; de lokale TCP-test en Qt-test
  bewijzen de softwaredoorgifte, geen werking van fysieke I2C/pneumatiek/Ethernet.
