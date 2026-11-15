# Pitot-feedback in MANUAL en AUTO

De standaardfirmware waveshare_s3 kiest het Pitot-beleid op basis van de actuele bedieningsmodus. In MANUAL volgt de aandrijving het ingestelde frequentieprofiel. Een ongeldige Pitot-meting blokkeert dan de voorbereiding, fysieke START of lopende frequentieaansturing niet. De meting blijft ongeldig in de telemetrie en de I2C-fouttellers blijven zichtbaar in de HMI.

AUTO vereist een geldige, verse Pitot-meting voor voorbereiding, START en bedrijf. Bij verlies tijdens bedrijf vergrendelt FAULT_SENSOR en krijgt de ISA-regelaar geen ongeldige meting. Bij selectie van AUTO geldt deze eis meteen. Omschakelen tijdens STARTING, RUNNING of STOP is geblokkeerd. Een bestaande fout vereist de gebruikelijke fysieke reset.

Ook langdurige Pitot-uitval laat MANUAL toe. De werkelijke luchtsnelheid is dan onbekend. Een storing op de gedeelde bus die ook DO1 treft, kan de aandrijving alsnog uitschakelen. Fysieke STOP, noodstop, OFF1/OFF2, DO1-bewaking en V20-watchdog volgen het bestaande beveiligingspad. Hun werking op de installatie vraagt een fysieke beproeving.
