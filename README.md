# ESP32 Peltier Cooler

Temperaturgeregelte Kuehlung eines Innenraums mittels Peltier-Element, gesteuert durch einen ESP32.

## Systemverständnis

**Wichtiges Konzept:**
- **Peltier-Element:** Kuehlelement (erzeugt Kaelte auf der kalten Seite, Waerme auf der heissen Seite)
- **Luefter:** Wärmeabfuhr (fuehrt die Waerme vom Peltier/Kuehlblock an die Umgebung ab)
- **Zusammenwirken:** Wenn das Peltier aktiviert wird, muss der Luefter sofort starten, um die Waerme effizient abzufuehren

**Lüftersteuerung:**
- Der Lüfter ist direkt an den Peltier-Zustand gekoppelt
- **Peltier AN →** Lüfter startet sofort (min. 40% PWM), PID übernimmt Feinregulierung
- **Peltier AUS →** Lüfter läuft bei 40% PWM nach, bis Kühlblock unter 30°C fällt
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

## Regellogik und Sicherheit

### Peltier-Steuerung und Kühlblocksicherheit

**Temperaturbereich (Innenraum):**
- **EIN** wenn Innenraumtemperatur >= `temp_on` (Default: 13°C)
- **AUS** wenn Innenraumtemperatur <= `temp_off` (Default: 11°C)
- Dazwischen: Hysterese-Band, Zustand bleibt unveraendert

**Kühlblocksicherheit:**
- **Notabschaltung** wenn Kuehlblock >= `temp_max` (Default: 52°C)
- Bei Ueberschreitung: sofortige Peltier-Abschaltung + Luefter 100%
- Kuehlblock-Temperatur wird alle 2s geprueft

**Sicherheitsmechanismen:**
- Peltier-GPIO hat Hardware-Pulldown → AUS bei ESP32-Reset/Brownout
- Bei Sensorfehlern: Vorheriger gueltiger Wert wird behalten
- Bei 1 Sensorfehler: Notmodus aktiv (Luefter 100%, Peltier AUS) - sofortige Reaktion
- **Cooldown**: Lüfter läuft bei 40% weiter bis Kühlblocktemp <= 30°C (nicht zeitlich begrenzt)
- **Manuelle Notabschaltung**: 3x kurzer Druck auf BOOT/RESET Button innerhalb von 2 Sekunden → Lüfter 100% (nur wenn Peltier AN ist)
- Notmodus wird bei naechster gueltigen Messung automatisch deaktiviert
- Scheduler inaktiv → Peltier sofort AUS, Lüfter Cooldown bis 30°C

### Auto-Duty Regelung (PWM)

Automatische Anpassung des PWM Duty-Cycles basierend auf Temperaturverlauf.

**Voraussetzung:** Nur aktiv, wenn Peltier aktiv ist.

**Konfiguration (Webinterface):**
- Hauptschalter unter Konfig, wird gleich im NVS gespeichert
- Zyklus auf Konfigseite (default 5s), per Return und Speicherbutton in NVS
- Duty wird beim Aktivieren vom aktuellen PWM Duty übernommen (keine separate Konfiguration)

**Status-Anzeige:**
- Hauptschalterzustand bunt dargestellt
- PWM Zyklus
- PWM Duty%
- PWM Duty Step %
- Aktuelle Leistung in Watt (basierend auf PWM Duty und PELTIER_POWER)

**Regellogik:**
- Toleranz: 0.1°C (Temperatur muss sich um mindestens 0.1°C ändern)
- Zyklus: 3s (schnellere Reaktion auf Temperaturänderungen)
- Innen temp sinkt im Zyklus (diff < -0.1°C) → duty - step, step auf 1 setzen (weniger aggressiv reduzieren, energiesparend)
- Innen temp steigt oder gleich (diff >= -0.1°C) für 2 Zyklen → duty + step, step auf 6 setzen, step verdoppeln (6→12→24, max 32%)
- Startwert Step: 6%
- Startwert Duty: Aktueller PWM Duty beim Aktivieren

## Software-Architektur

### FreeRTOS Tasks

| Task | Prioritaet | Intervall | Funktion |
|---|---|---|---|
| `sensor` | 5 | 2s | Liest beide DS18B20 per OneWire |
| `fan` | 4 | 1s | Luefter-Steuerung + Peltier Ein/Aus |
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

- Live-Anzeige: Innenraum-Temperatur, Kuehlblock-Temperatur, Luefter-%, Luefter-RPM, Peltier AN/AUS, Notmodus-Status, PWM Duty, PWM Step, Aktuelle Leistung (W)
- Einstellbar: Temperatur-Schwellen (on/off/max), PWM-Parameter (Period, Duty), Auto-Duty (Hauptschalter, Zyklus), Zeitfenster (7-Tage-Tabelle mit Stundenwerten)
- Energiedaten: Gesamt, Heute, Woche, Monat (Wh) mit Kostenberechnung (€)
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
| Kuehlblock Target | `temp_target` | 45.0°C | Zieltemperatur für Lüfter-PID |
| PWM Period | `peltier_pwm_period` | 10s | PWM Period (Dauer eines Zyklus) |
| PWM Duty | `peltier_pwm_duty` | 10% | PWM Duty Cycle (5-20%) |
| Auto-Duty Hauptschalter | `auto_duty_en` | true | Auto-Duty Regelung aktivieren |
| Auto-Duty Zyklus | `auto_duty_cycle` | 5s | Zyklusdauer für Auto-Duty Regelung |
| OTA URL | `ota_url` | http://192.168.1.191:8080/firmware.bin | Firmware-Update Server URL |
| Daten-Log Intervall | `data_log_interval` | 10s | Intervall für Daten-Logger |
| Mo-Fr AN | `sched_mo_on` ... `sched_do_on` | 11:00 | Betriebsstart Mo-Do (Stunden 0-23) |
| Mo-Fr AUS | `sched_mo_off` ... `sched_do_off` | 19:00 | Betriebsende Mo-Do |
| Fr-So AN | `sched_fr_on` ... `sched_so_on` | 11:00 | Betriebsstart Fr-So |
| Fr-So AUS | `sched_fr_off` ... `sched_so_off` | 21:00 | Betriebsende Fr-So |
| Energie Gesamt | `energy_wh` | 0.0 Wh | Gesamtenergieverbrauch |
| Energie Heute | `energy_day` | 0.0 Wh | Tagesenergieverbrauch |
| Energie Woche | `energy_week` | 0.0 Wh | Wochenenergieverbrauch |
| Energie Monat | `energy_month` | 0.0 Wh | Monatsenergieverbrauch |
| Letztes Datum | `last_date` | 0 | Zuletzt gespeichertes Datum (YYYYMMDD) |
| Letzte Woche | `last_week` | 0 | Zuletzt gespeicherte Kalenderwoche (0-53) |
| Letzter Monat | `last_month` | 0 | Zuletzt gespeicherter Monat (0-11) |

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

## Lizenz

Privates Projekt, keine oeffentliche Lizenz.
