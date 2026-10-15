# Windtunnel PID-controller — ESP32 / FreeRTOS

Basisproject naar de **structuur** van EMPD2026-DeltaRobot: `ApplicationTasks`, losse taken, een zelfstandige `Regelaar`-library en een hardwarelaag. De ESP32 voert lokaal de regelcyclus uit; de Raspberry Pi geeft opdrachten en ontvangt status.

PlatformIO, `espressif32@6.12.0`, Arduino-ESP32 2.0.17 en de ingebouwde FreeRTOS-kernel. Arduino verzorgt alleen het opstarten in `main.cpp`; de applicatie en libraries zijn C. De scheduler draait al: geen eigen `vTaskStartScheduler()` of meegekopieerde FreeRTOS-kernel nodig. Voorlopig board: **klassieke ESP32 DevKit (`esp32dev`)**.

## Opbouw

```text
include/AppConfig.h              Klok-/I/O-timing, voorlopige pinnen en simulatiekeuze
include/ApplicationTasks.h       Startfunctie
include/TaskData.h               Queue-berichten en gedeelde RTOS-handles
src/main.cpp                    Opstartlaag
src/ApplicationTasks.c          Maakt queues en taken aan
src/ControlTask.c               Tustin PI(D) + START/STOP + foutafhandeling, externe 1 kHz-klok
src/SensorTask.c                Sensor uitlezen, 100 Hz
src/ActuatorTask.c              Motoropdracht uitvoeren, voorlopig 20 Hz
src/PiCommunicationTask.c       UART-commando's en status op 5 Hz
lib/Regelaar/                  Hardware-onafhankelijke PID
lib/PiProtocol/                Tekstcommando-parser
lib/WindtunnelHAL/             Sensor- en motordriverinterface + simulatie
tools/pi_client.py             Pi-testclient met periodieke PING
tests/                         Uitvoerbare C-hosttests
```

Datastroom: externe klokflank → GPIO-ISR → ControlTask-notificatie → Tustin-regelaar.
Pi → command queue → ControlTask → motor-mailbox → ActuatorTask → motor.
SensorTask → sensor-mailbox → ControlTask → status-mailbox → Pi.
De mailboxes hebben lengte één en bewaren de nieuwste waarde. Alleen ControlTask beheert de PID en regeltoestand. I²C/Modbus-I/O draait buiten de regelcyclus. Stapels zijn in bytes opgegeven zoals verwacht door ESP32 FreeRTOS.

## Huidige werking

**`SIMULATION_MODE=1` staat standaard aan. Er wordt geen echte motor aangestuurd en geen I²C-sensor uitgelezen.** Het model simuleert een eerste-ordeproces (tijdconstante 0,5 s, 100% uitgang geeft 30 m/s). Een opgestarte controller staat stil totdat START is ontvangen. De simulatie draait op de ESP32; de hosttests testen daarnaast dezelfde PID-code met een simulatiemodel op de pc.

De GPIO-ISR op GPIO4 wordt geactiveerd op iedere opgaande flank van de externe 1 kHz-klok. De ISR wekt alleen ControlTask met een FreeRTOS-tasknotificatie; alle commandaverwerking en regeling gebeurt in task-context. De Tustin-regelaar gebruikt daarom `Ts = 1 ms`. Als een kloktik uitblijft (50 ms timeout) of meerdere tikken zijn samengevoegd, stopt de regeltoestand en wordt nul uitgang aangevraagd; START is daarna opnieuw nodig. SensorTask leest op 100 Hz en ActuatorTask werkt de motoruitgang voorlopig op 20 Hz bij. Sensor- en actuator-I/O blokkeren de regeltaak dus niet.

De regelaar heeft instelbare Kp/Ki/Kd, Tustin-discretisatie voor integratie en gefilterde afgeleide van de meting, uitgangsbegrenzing 0–100% en conditional-integration anti-windup. PID-historie wordt gewist bij stoppen. Dit volgt de Tustin-opbouw uit DeltaRobot, maar gebruikt geen robot-specifieke resonantieparameters, overbrengingen of feedforward. Standaardgains zijn alleen voorbeeldwaarden. Het theoretische windtunnel-PI-model gebruikt `Ts = 50 ms`, terwijl de externe klok deze implementatie op `Ts = 1 ms` laat rekenen; de berekende gains horen bovendien bij een andere, aangenomen plant en een incrementele VFD-frequentieopdracht. Neem die gains daarom niet rechtstreeks over. Begin op echte hardware met P/PI-inregeling en `Kd=0`.

Bij ongeldige/verouderde sensorfeedback, een motordriverfout of 2 seconden zonder verwerkt geldig Pi-commando stopt de regeltoestand en wordt nul uitgang aangevraagd. Na herstel is een nieuwe START nodig. De motortaak vraagt zelf nul aan wanneer de regelopdracht ouder is dan 100 ms. Sensor/motordriver-initfouten vereisen momenteel herstart.

Nul aanvragen garandeert niet dat een onbereikbare echte drive stopt. Configureer de communicatiewatchdog in de frequentieregelaar en houd een fysieke noodstop onafhankelijk van deze firmware. De 1 kHz-loop, GPIO-interruptlatentie en taakdeadline moeten op de uiteindelijke ESP32 en klokbron worden gevalideerd. De sensorfilter, sensorconversietijd, Modbus-transactietijd, drive-updatefrequentie en time-outs moeten nog op de hardware worden afgestemd.

## Bouwen en uploaden

Open deze map in VS Code met PlatformIO:

```sh
pio run
pio run -t upload --upload-port COM5
pio device monitor --port COM5 --baud 115200
```

Vervang COM5 door de ESP32-poort. USB/UART0 is voor programmeren/debug; de Pi-commandopoort is **UART1**, niet de USB-monitor. Er is nog niets geflasht.

Verbind de externe **1 kHz-klokuitgang met 3,3 V-logicaniveau** met voorlopig GPIO4 en verbind de signaal-GND met ESP32-GND. Iedere opgaande flank triggert één regelstap. Controleer de pin en elektrische niveaus voor het definitieve ESP32-board; stuur geen 5 V rechtstreeks naar een GPIO. Zonder kloksignaal stopt de controller na de kloktimeout.

Hosttests op Windows met Visual Studio C++ tools:

```powershell
./tests/run_host_tests.ps1
```

Op een systeem met GCC kan dezelfde testcode rechtstreeks worden gebouwd:

```sh
gcc -std=c11 -Wall -Wextra -Ilib/Regelaar -Ilib/PiProtocol tests/test_core.c lib/Regelaar/Regelaar.c lib/PiProtocol/PiProtocol.c -lm -o core_tests
./core_tests
```

## Raspberry Pi-protocol v1

Voorlopige keuze: UART1, 115200 baud, 8N1, ASCII met LF of CRLF, maximaal 95 tekens per regel. Commands zijn hoofdlettergevoelig. Pi TX → ESP RX16, Pi RX ← ESP TX17, gemeenschappelijke GND en **3,3 V-signalen**. Controleer pinbeschikbaarheid voor jouw ESP32; sommige varianten gebruiken 16/17 voor PSRAM. Schakel de seriële loginconsole op de Pi uit voordat die poort wordt gebruikt.

| Commando | Betekenis |
| --- | --- |
| `PING` | Houdt de verbinding actief; verstuur bijvoorbeeld elke 500 ms |
| `SETPOINT 10` | Doelwaarde in m/s, voorlopig 0–30 |
| `PID 2 1 0` | Kp Ki Kd, elk 0–1000; wordt alleen toegepast terwijl gestopt |
| `START` | Start alleen bij geldige sensor- en motorstatus |
| `STOP` | Stopverzoek buiten de command queue; wint van START in dezelfde regelcyclus |

`OK QUEUED` bevestigt alleen plaatsing in de queue. `OK STOP_REQUESTED` bevestigt alleen het stopverzoek. `ERR SYNTAX`, `ERR RANGE`, `ERR BUSY` en `ERR LINE` wijzen invoer af. De periodieke STATUS bevat `running`, `sensor_ok`, `motor_ok`, `sp`, `pv`, `out`, `reason` en `sim`. `out` is de gevraagde motoruitgang, geen gemeten motortoerental. Controleer `running` en `reason` voor het resultaat van START/PID/STOP. Het eenvoudige v1-protocol heeft nog geen sequence-ID's of afzonderlijke bevestiging van toegepaste parameters.

STOP en daarna START moeten in afzonderlijke regelcycli plaatsvinden (voorbeeldclient wacht 100 ms). Stuur PING ook tijdens langdurig bedrijf; STOP houdt de watchdog niet actief. Queue-overflow en ongeldige commando's vernieuwen de watchdog niet.

Pi-voorbeeld, nadat pyserial beschikbaar is:

```sh
python -m pip install pyserial
python tools/pi_client.py /dev/serial0 --setpoint 10 --seconds 20
```

## Echte hardware aansluiten: nog in te vullen

1. Bevestig ESP32-variant en pinnen in `AppConfig.h`/`platformio.ini`.
2. Implementeer `SensorLib_Init/Read` voor het gekozen I²C-sensortype: adres, busdriver, registermap, conversietijd, kalibratie, foutdetectie en omrekening naar m/s. Retourneer false bij mislukte transacties. I²C-pinnen 21/22 zijn voorlopig gereserveerd.
3. Implementeer `MotorLib_Init/Write` voor de motoraansturing. **Modbus RTU over RS485 is een voorlopige aanname**; Modbus TCP vraagt een andere transportdriver. UART2 (25/26, DE27), slave-adres en baudrate zijn alvast benoemd. Voeg RS485-transceiver, CRC/response-validatie, begrensde time-outs, registermapping, schaalfactoren, drive-enable en stopcommando toe. Deze basis bevat nog geen werkende Modbus-stack.
4. Pas deadlines en sampletijd aan; een seriële Modbus-write per 10 ms is niet voor iedere drive haalbaar.
5. Zet pas daarna `SIMULATION_MODE=0` en stem de PID af. Met de huidige hardwarestubs retourneert deze stand fouten en kan START niet inschakelen.

Nog nodig: sensortype(n), drive/frequentieregelaar + Modbus-registerlijst, ESP32-variant en definitieve Pi-verbinding. Aanvullende sensoren kunnen als aparte meetvelden aan Sample/Telemetry worden toegevoegd.

API-referentie voor de gebruikte UART-driver: [Espressif UART, ESP-IDF 4.4](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/uart.html).
