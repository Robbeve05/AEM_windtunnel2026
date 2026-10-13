# Nederlandstalige installatiehandleiding: Raspberry Pi en Arduino Uno

Deze handleiding beschrijft de volledige eerste installatie van de windtunnel-HMI en het uploaden van de Arduino-firmware. De HMI draait als een Python-desktopprogramma op de Raspberry Pi. De Arduino Uno sluit je met USB op de Pi aan; de Pi kan de firmware daarna rechtstreeks naar de Uno uploaden.

## Wat je nodig hebt

- Een Raspberry Pi met een betrouwbare voeding.
- Een microSD-kaart en een computer om Raspberry Pi OS eenmalig op de kaart te zetten.
- Raspberry Pi OS **64-bit met desktopomgeving** en toegang tot internet.
- Een Arduino Uno en een USB-datakabel tussen de Uno en de Pi.
- De bestaande, gecontroleerde sensorbedrading. Verander de sensorbedrading niet op basis van deze softwarehandleiding.

Een laptop/computer is nodig om Raspberry Pi OS op de microSD-kaart te zetten. Nadat de Pi is opgestart en met internet is verbonden, kan de Arduino-firmware vanaf de Pi zelf worden geüpload. Je hoeft de behuizing dan niet open te maken, zolang de Arduino via USB met de Pi is verbonden.

## 1. Raspberry Pi OS installeren

1. Installeer **Raspberry Pi Imager** op je computer vanaf de officiële Raspberry Pi-website.
2. Plaats de microSD-kaart in de computer en open Raspberry Pi Imager.
3. Kies je Raspberry Pi-model.
4. Kies **Raspberry Pi OS (64-bit)** met de desktopomgeving.
5. Selecteer de microSD-kaart als opslagmedium.
6. Stel in de Imager-instellingen een gebruikersnaam en wachtwoord in en configureer zo nodig wifi.
7. Schrijf het besturingssysteem naar de kaart.
8. Plaats de kaart in de Pi, sluit toetsenbord, muis en scherm aan en start de Pi op.
9. Verbind de Pi met internet en open een terminal op de Pi.

## 2. De HMI op de Pi installeren

Voer deze volledige opdracht in de Pi-terminal uit. Er is geen GitHub-account of GitHub-wachtwoord nodig:

```bash
curl -fsSL https://raw.githubusercontent.com/Robbeve05/AEM_windtunnel2026/RaspberryPi/HMI/deploy/setup-pi-from-github.sh -o /tmp/setup-windtunnel-hmi.sh && bash /tmp/setup-windtunnel-hmi.sh
```

De installatie haalt de software van de `RaspberryPi`-branch op, installeert de Python-onderdelen en maakt de HMI-app en de lokale achtergrondservice aan. Als er al een HMI-installatie staat, maakt het script eerst een reservekopie en neemt het de lokale instellingen en meetbestanden mee.

Wacht tot het script klaar is. Start daarna **Windtunnel HMI** via het bureaubladmenu. Sluit de HMI weer af voordat je de Arduino-firmware gaat uploaden.

## 3. De Arduino met de Pi verbinden

1. Zet de windtunnelventilator uit.
2. Laat de bestaande sensorbedrading intact.
3. Verbind de USB-poort van de Arduino Uno met een USB-poort van de Pi met een **datakabel** (niet alleen een laadkabel).
4. Zorg dat de pitotbuis tijdens het opstarten stil ligt en niet in de luchtstroom zit. De firmware stelt bij het opstarten het nulpunt in.

Controleer of de Pi de Uno ziet:

```bash
ls -l /dev/serial/by-id/ /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Verschijnt er geen seriële poort, controleer dan de USB-kabel en probeer een andere USB-poort. Als de gebruiker geen toegang heeft tot de seriële poort, voer dan eenmalig uit:

```bash
sudo usermod -aG dialout "$USER"
sudo reboot
```

Na het opnieuw opstarten hoef je deze groepsinstelling niet opnieuw uit te voeren.

## 4. Arduino-uploadsoftware installeren op de Pi

De Arduino-uploadsoftware heet **Arduino CLI**. De HMI-installatie installeert die niet automatisch. Installeer de CLI en de Uno-boardbestanden eenmalig:

```bash
sudo apt update
sudo apt install -y arduino-cli
arduino-cli core update-index
arduino-cli core install arduino:avr
```

Een grafische Arduino IDE is niet nodig.

## 5. De firmware vanaf de Pi naar de Uno uploaden

Controleer eerst nogmaals dat de ventilator uit staat, de pitotbuis uit de luchtstroom is en er geen meting wordt opgenomen. Sluit het HMI-venster en voer daarna uit:

```bash
cd ~/AEM_windtunnel2026
bash HMI/deploy/flash-arduino.sh
```

Het script:

1. zoekt de aangesloten Arduino op;
2. stopt tijdelijk de HMI-service zodat die de USB-seriële poort vrijgeeft;
3. compileert de firmware uit `HMI/Arduino/Arduino_Uno_Pitot_HMI.ino`;
4. uploadt de firmware naar de Uno;
5. start de service weer als die vóór het uploaden actief was.

Laat tijdens het uploaden maar één Arduino-compatibel USB-serieel apparaat aangesloten. Bij meerdere apparaten stopt het script om te voorkomen dat het de verkeerde kiest. Bij een foutmelding over `arduino-cli` of de Uno-core: herhaal stap 4. Bij een foutmelding dat er geen seriële poort is gevonden: controleer stap 3.

## 6. De HMI starten en controleren

Start **Windtunnel HMI** opnieuw via het bureaubladmenu. De HMI-service start samen met de desktopapp. Na het openen van de USB-seriële verbinding herstart de Uno normaal gesproken; laat de pitotbuis daarom de eerste **3 seconden** stil en uit de luchtstroom.

Controleer in de HMI:

- **Overview**: de Arduino- en pitotstatus worden bijgewerkt zodra geldige sensordata binnenkomt.
- **Raw Data**: toont de binnenkomende meetwaarden wanneer de sensor online is.
- **Settings**: hier kun je de Arduino-low-passfilterfrequentie instellen. Een lagere frequentie filtert sterker, maar reageert trager.
- **Measurements**: hiermee start en stop je een meting. De gekozen samplefrequentie geldt voor het opslaan; hij maakt de Arduino zelf niet sneller.

Controleer bij problemen de servicestatus en het logboek in de Pi-terminal:

```bash
systemctl --user status windtunnel-hmi.service --no-pager
journalctl --user -u windtunnel-hmi.service -n 60 --no-pager
```

## Later de software bijwerken

Nieuwe HMI-code komt eerst op GitHub. Sluit op de Pi het HMI-venster en voer uit:

```bash
cd ~/AEM_windtunnel2026
bash HMI/deploy/update-pi.sh
```

Open daarna **Windtunnel HMI** opnieuw via het bureaubladmenu. Dit werkt de HMI-software bij; het uploaden van nieuwe Arduino-firmware is een aparte stap. Als een firmwarewijziging nodig is, herhaal dan stap 5. De lokale configuratie en meetbestanden op de Pi blijven bij een normale update behouden.

## Veiligheidsnotities

- De softwarematige stopknop is geen veiligheidsgecertificeerde noodstop.
- Upload de firmware niet terwijl de ventilator draait of een meting wordt opgenomen.
- Houd de pitotbuis stil en buiten de luchtstroom tijdens de nulkalibratie.
- Deze handleiding beschrijft niet de sensorpinnen of elektrische bedrading. Gebruik daarvoor het gecontroleerde elektrische schema van de installatie.
