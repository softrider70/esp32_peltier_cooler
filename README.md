# ESP32 Peltier Cooler

Temperaturgeregelte Kuehlung eines Innenraums mittels Peltier-Element, gesteuert durch einen ESP32.

## Systemverständnis

**Wichtiges Konzept:**
- **Peltier-Element:** Kuehlelement (erzeugt Kaelte auf der kalten Seite, Waerme auf der heissen Seite)
- **Luefter:** Wärmeabfuhr (fuehrt die Waerme vom Peltier/Kuehlblock an die Umgebung ab)
- **Zusammenwirken:** Wenn das Peltier aktiviert wird, muss der Luefter sofort starten, um die Waerme effizient abzufuehren

**Lüftersteuerung:**
- Der Lüfter ist direkt an den Peltier-Zustand gekoppelt
- **Peltier AN →** Lüfter startet sofort (min. 50% PWM), PID übernimmt Feinregulierung
- **Peltier AUS →** Lüfter läuft 30 Sekunden nach (Cooldown bei 30% PWM), dann aus
- Ziel: Vermeidung von Temperaturüberschreitung und Lärmreduktion (unter 70% PWM)

## Aufgabe

Ein geschlossener Raum (z.B. Schrank, Gehaeuse) soll aktiv gekuehlt werden. Ein Peltier-Element entzieht dem Innenraum Waerme und gibt sie ueber einen Kuehlblock mit Noctua-Luefter an die Umgebung ab. Die Steuerung uebernimmt ein ESP32, der:

1. die Innenraumtemperatur misst und das Peltier bei Bedarf ein-/ausschaltet
2. die Kuehlblock-Temperatur ueberwacht und den Luefter per PID-Regelung steuert
3. eine Sicherheitsabschaltung bei Uebertemperatur am Kuehlblock ausfuehrt
4. per Webinterface Live-Monitoring und Parametrierung ermoeglicht
5. Zeitfenster fuer den Betrieb (jeder Wochentag individuell) verwaltet
6. RPM-Messung des Luefters mit Kalibrierungsmoeglichkeit

## Hardware

| Komponente | Beschreibung | Anschluss |
|---|---|---|
| ESP32-D (ESP32-D0WD-V3) | Mikrocontroller, Dual-Core 240 MHz, 30-Pin Board | - |
| Peltier-Element (TEC) | Kühlung, gesteuert über N-MOSFET | D16 (GPIO16, Gate) |
| Noctua 4-Pin Lüfter | 25 kHz PWM, Tacho-Signal | PWM: D5 (GPIO5), Tacho: D18 (GPIO18) |
| DS18B20 #1 | Temperatursensor Innenraum | D4 (GPIO4, OneWire) |
| DS18B20 #2 | Temperatursensor Kühlblock (heisse Seite) | D4 (GPIO4, OneWire) |
| BOOT-Button | WiFi-Reset (3 Sekunden langdrücken) | GPIO0 (integrierter Button) |

### Verdrahtung OneWire

Beide DS18B20 haengen am gleichen OneWire-Bus (D4/GPIO4) mit einem 4.7 kOhm Pull-Up nach 3.3V. Die Sensoren werden beim Start per ROM-Adresse identifiziert — Sensor 0 = Innenraum, Sensor 1 = Kuehlblock.

### Verdrahtung Peltier

**N-MOSFET Modul (z.B. IRF520N Modul):**
```
ESP32 D16 (GPIO16) --- IN
Modul VCC --- 5V
Modul GND --- GND
Modul OUT --- Peltier(–)
Peltier(+) --- V+ (12V)
```

**Alternativ: Einzelner MOSFET (IRLZ44N):**
```
ESP32 D16 (GPIO16) ---[1kOhm]--- Gate
                                |
                             MOSFET (IRLZ44N)
                                |
                   Drain --- Peltier(–) --- Peltier(+) --- V+
                                |
                              Source --- GND

Gate-Pulldown: 10kOhm nach GND (sicherer Zustand bei ESP32-Reset)
```

### Verdrahtung Noctua Lüfter (PWM + Tacho)

**PWM mit NPN Transistor Inverter (3.3V → 5V):**
```
          5V (Noctua)
             │
           [10kΩ] Pull-up
             │
             ├── Noctua PWM Pin
             │
ESP32 D5 ──┬── [1kΩ] ──┬── NPN-Transistor (2N2222/BC547)
           │           │
          GND         ├── Collector
                       │
                      Emitter ─── GND
```

**Tacho (Open-Collector mit Pull-Up):**
```
Noctua Tacho (grün) ──┬── ESP32 D18 (GPIO18)
                      │
                   (interner Pull-Up auf 3.3V)
                      │
                     GND (gemeinsam mit ESP32)
```

**Hinweis:** Der NPN Transistor invertiert das PWM-Signal, wird im Software automatisch korrigiert.

## Regellogik

### Peltier-Steuerung (digital Ein/Aus)

- **EIN** wenn Innenraumtemperatur >= `temp_on` (Default: 25°C)
- **AUS** wenn Innenraumtemperatur <= `temp_off` (Default: 22°C)
- Dazwischen: Hysterese-Band, Zustand bleibt unveraendert
- **Notabschaltung** wenn Kuehlblock >= `temp_max` (Default: 60°C)

### PWM Auto-Duty Regelung

Die Auto-Duty Regelung passt den PWM Duty Cycle automatisch basierend auf der Innentemperatur an, um die Kühlleistung zu optimieren.

**Funktionsweise:**
- Alle `peltier_pwm_interval` Sekunden wird die aktuelle Innentemperatur mit der Referenztemperatur verglichen
- **Temperatur sinkt (>=0.1°C):** Duty reduzieren (um `duty_step`%, min 5%)
- **Temperatur steigt (>=0.1°C):** Duty erhöhen (um `duty_step`%, max 20%)
- **Temperatur stabil (<0.1°C):** Counter erhöht → bei 2x stabil Duty erhöhen

**Adaptive Schrittweite:**
- Basis-Schrittweite: 5%
- 2x aufeinanderfolgende Änderungen → 7%
- 4x aufeinanderfolgende Änderungen → 10%
- Stabile Temperatur → zurück zu 5%

**Parameter (NVS):**
- `peltier_pwm_period`: PWM Period (z.B. 10s)
- `peltier_pwm_duty`: Aktueller Duty (5-20%)
- `peltier_pwm_auto`: Auto-Duty aktiv/inaktiv
- `peltier_pwm_interval`: Interval in Sekunden zwischen Kontrollen

## Software-Architektur

### FreeRTOS Tasks

| Task | Prioritaet | Intervall | Funktion |
|---|---|---|---|
| `sensor` | 5 | 2s | Liest beide DS18B20 per OneWire |
| `fan_pid` | 4 | 1s | PID-Regelung Luefter + Peltier Ein/Aus |
| `scheduler` | 3 | 30s | Prueft Zeitfenster (SNTP/CET) |
| `reset_btn` | 5 | 100ms | Ueberwacht BOOT-Button für WiFi-Reset |
| `dns_captive` | 2 | - | DNS-Redirect im AP-Modus |

### Module

```
main/
├── main.c          App-Entry, Init-Sequenz, Task-Start
├── config.h        GPIOs, Defaults, NVS-Keys, Task-Konfig
├── sensor.c/.h     DS18B20 OneWire Bit-Bang + CRC8
├── pid.c/.h        PID-Regler mit Anti-Windup
├── peltier.c/.h    Digitaler GPIO-Schalter fuer MOSFET
├── fan.c/.h        Noctua 25kHz LEDC-PWM + PID-Task
├── wifi.c/.h       STA-Verbindung mit AP-Fallback
├── webserver.c/.h  HTTP-Server + Captive-DNS
├── scheduler.c/.h  Zeitfenster-Pruefung (SNTP, CET/CEST)
├── nvs_config.c/.h NVS-Persistenz aller Einstellungen
├── index.html      Monitor-Webseite (Dark Theme, Live-Refresh)
└── captive.html    WiFi-Setup Captive Portal
```

## Webinterface

### Monitor-Seite (STA-Modus)

- Live-Anzeige: Innenraum-Temperatur, Kuehlblock-Temperatur, Luefter-%, Luefter-RPM, Peltier AN/AUS, Notmodus-Status, PWM Duty, Auto-Duty Countdown
- Einstellbar: Temperatur-Schwellen (on/off/max), PWM-Parameter (Period, Duty, Auto-Duty, Interval), Zeitfenster (7-Tage-Tabelle mit Stundenwerten)
- WiFi-Reset: Rot markierter Button zum Löschen der WiFi-Credentials und Starten des AP-Modus
- Auto-Refresh alle 3 Sekunden
- REST API: `GET /api/status`, `POST /api/config`, `POST /api/wifi/reset`

### Captive Portal (AP-Modus)

- Automatische Weiterleitung auf `http://10.1.1.1/`
- WiFi-SSID und Passwort eingeben → ESP32 verbindet sich mit Router
- AP-SSID: `ESP32-Cooler-Setup` (offen, kein Passwort)
- DNS-Server leitet alle Anfragen auf 10.1.1.1 → Captive-Detection der Clients greift

### WiFi Reset

Der ESP32 bietet zwei Möglichkeiten zum Zurücksetzen der WiFi-Credentials:

**Physischer Button:**
- BOOT-Button auf dem ESP32-Board 3 Sekunden langdrücken
- Löscht WiFi-Credentials aus NVS
- Startet AP-Modus für neue Konfiguration

**Web-Button:**
- Im Settings-Tab unter "Firmware Update (OTA)"
- Rot markierter "WiFi Reset" Button
- Bestätigungsdialog vor dem Reset
- Gleiche Funktion wie physischer Button

## OTA (Over-The-Air Update)

Die OTA-Funktion ermoeglicht Firmware-Updates ueber HTTP ohne physischen Zugriff auf den ESP32.

**Funktionsweise:**
- Firmware wird von konfigurierbarer URL heruntergeladen (Default: `http://192.168.1.191:8080/firmware.bin`)
- Download in 4KB-Blöcken in die zweite OTA-Partition
- Nach Download: Reboot und Boot-Partition umschalten
- A/B-Update mit Rollback-Schutz (zwei Partitionen: ota_0, ota_1)

**Self-Check nach Update:**
- Nach erfolgreichen Boot: 30 Sekunden Wartezeit
- **Self-Check 1:** Sensoren verfügbar (3 Wiederholungen à 2 Sekunden)
- **Self-Check 2:** WiFi verbunden (optional, nur Warnung)
- Bei Erfolg: Firmware als gueltig markieren
- Bei Fehler: Automatischer Rollback zur vorherigen Firmware

**Parameter:**
- HTTP-Timeout: 30 Sekunden
- Buffer-Größe: 4096 Bytes
- OTA-URL konfigurierbar über Webinterface (in NVS gespeichert)

**Webinterface:**
- OTA-URL konfigurierbar im Settings-Tab
- Status-Anzeige: IDLE, IN_PROGRESS, SUCCESS, FAILED
- Fehlermeldung bei Fehlschlag

## Konfiguration (NVS)

Alle Einstellungen werden im Non-Volatile Storage (NVS) des ESP32 gespeichert und ueberleben Neustarts:

| Parameter | NVS-Key | Default | Beschreibung |
|---|---|---|---|
| WiFi SSID | `wifi_ssid` | (leer) | Router-SSID |
| WiFi Passwort | `wifi_pass` | (leer) | WPA2-Passwort |
| Peltier AN | `temp_on` | 25.0°C | Einschalt-Schwelle Innenraum |
| Peltier AUS | `temp_off` | 22.0°C | Ausschalt-Schwelle Innenraum |
| Kuehlblock Max | `temp_max` | 60.0°C | Sicherheits-Cutoff |
| PWM Period | `pwm_period` | 10s | PWM Period (Dauer eines Zyklus) |
| PWM Duty | `pwm_duty` | 10% | PWM Duty Cycle (5-20%) |
| PWM Auto-Duty | `pwm_auto` | false | Automatische Duty-Anpassung |
| PWM Interval | `pwm_interval` | 60s | Interval zwischen Auto-Duty-Kontrollen |
| OTA URL | `ota_url` | http://192.168.1.191:8080/firmware.bin | Firmware-Update Server URL |
| Mo-Fr AN | `sch_mo_on` ... `sch_do_on` | 11:00 | Betriebsstart Mo-Do (Stunden 0-23) |
| Mo-Fr AUS | `sch_mo_off` ... `sch_do_off` | 19:00 | Betriebsende Mo-Do |
| Fr-So AN | `sch_fr_on` ... `sch_so_on` | 11:00 | Betriebsstart Fr-So |
| Fr-So AUS | `sch_fr_off` ... `sch_so_off` | 21:00 | Betriebsende Fr-So |

## NVS-Schreibzugriffe

Der Non-Volatile Storage (NVS) wird bei folgenden Aktionen beschrieben:

**Hauptkonfiguration (nvs_config_save):**
- Konfigurationsänderungen über Webinterface
- WiFi-Setup (SSID/Passwort)
- Factory Reset (Defaults speichern)

**Energiedaten (nvs_config_save_energy):**
- Nur wenn Peltier von AN → AUS wechselt
- Nur wenn Energiedifferenz > 0.1 Wh
- Reduziert NVS-Schreibzyklen für Flash-Lebensdauer

**OTA-URL:**
- Änderung der OTA-URL über Webinterface

**Graph-Daten (data_logger_save_to_nvs):**
- Bei Scheduler-Deaktivierung (720 Datenpunkte als Blob)

**Optimierung:**
- Energiedaten werden nur beim Peltier-Ausschalten gespeichert
- Schwellenwert von 0.1 Wh reduziert Schreibzugriffe um Faktor 10

## Build & Flash

Voraussetzung: ESP-IDF 5.x installiert und `IDF_PATH` gesetzt.

```bash
cd esp32_cooler
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

## RPM-Kalibrierung

Die RPM-Messung des Luefters kann kalibriert werden, falls die angezeigten Werte von den tatsaechlichen Umdrehungen abweichen.

**Voraussetzung:** Tacho muss korrekt angeschlossen sein (direkt mit GPIO18, kein Spannungsteiler, GND gemeinsam).

Kalibrierung:
1. Luefter auf 100% PWM setzen (z.B. über Webinterface)
2. RPM-Wert im Serial Monitor ablesen (`interrupts > 0` pruefen)
3. Kalibrierungsfaktor berechnen: `Faktor = Ziel-RPM / Gemessene-RPM`
4. Faktor in `config.h` anpassen: `#define RPM_CALIBRATION_FACTOR X.Xf`
5. Neu flashen

Standard: `RPM_CALIBRATION_FACTOR = 1.0f` (nicht kalibriert)

## Sicherheit

- Peltier-GPIO hat Hardware-Pulldown → AUS bei ESP32-Reset/Brownout
- Kuehlblock-Temperatur wird alle 2s geprueft
- Bei Ueberschreitung von `temp_max`: sofortige Peltier-Abschaltung + Luefter 100%
- Bei Sensorfehlern: Vorheriger gueltiger Wert wird behalten
- Bei 5 aufeinanderfolgenden Sensorfehlern: Notmodus aktiv (Luefter 100%, Peltier AUS)
- Notmodus wird bei naechster gueltigen Messung automatisch deaktiviert
- Scheduler inaktiv → alles aus

## Lizenz

Privates Projekt, keine oeffentliche Lizenz.
