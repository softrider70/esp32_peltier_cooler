# ESP32 Peltier Cooler

Temperaturgeregelte Kuehlung eines Innenraums mittels Peltier-Element, gesteuert durch einen ESP32.

## Aufgabe

Ein geschlossener Raum (z.B. Schrank, Gehaeuse) soll aktiv gekuehlt werden. Ein Peltier-Element entzieht dem Innenraum Waerme und gibt sie ueber einen Kuehlblock mit Noctua-Luefter an die Umgebung ab. Die Steuerung uebernimmt ein ESP32, der:

1. die Innenraumtemperatur misst und das Peltier bei Bedarf ein-/ausschaltet
2. die Kuehlblock-Temperatur ueberwacht und den Luefter per PID-Regelung steuert
3. eine Sicherheitsabschaltung bei Uebertemperatur am Kuehlblock ausfuehrt
4. per Webinterface Live-Monitoring und Parametrierung ermoeglicht
5. Zeitfenster fuer den Betrieb (Wochentag/Wochenende getrennt) verwaltet

## Hardware

| Komponente | Beschreibung | Anschluss |
|---|---|---|
| ESP32 (Standard) | Mikrocontroller, Dual-Core 240 MHz | - |
| Peltier-Element (TEC) | Kuehlung, gesteuert ueber N-MOSFET | GPIO 14 (Gate) |
| Noctua 4-Pin Luefter | 25 kHz PWM, Tacho-Signal | PWM: GPIO 25, Tacho: GPIO 26 |
| DS18B20 #1 | Temperaturfuehler Innenraum | GPIO 27 (OneWire) |
| DS18B20 #2 | Temperaturfuehler Kuehlblock (heisse Seite) | GPIO 27 (OneWire) |
| N-MOSFET (z.B. IRLZ44N) | Schaltet Peltier-Stromkreis | Gate: GPIO 14 |

### Verdrahtung OneWire

Beide DS18B20 haengen am gleichen OneWire-Bus (GPIO 27) mit einem 4.7 kOhm Pull-Up nach 3.3V. Die Sensoren werden beim Start per ROM-Adresse identifiziert — Sensor 0 = Innenraum, Sensor 1 = Kuehlblock.

### Verdrahtung Peltier-MOSFET

```
ESP32 GPIO14 ---[1kOhm]--- Gate
                            |
                         MOSFET (IRLZ44N)
                            |
               Drain --- Peltier(–) --- Peltier(+) --- V+
                            |
                          Source --- GND

Gate-Pulldown: 10kOhm nach GND (sicherer Zustand bei ESP32-Reset)
```

## Regellogik

### Peltier-Steuerung (digital Ein/Aus)

- **EIN** wenn Innenraumtemperatur >= `temp_on` (Default: 25°C)
- **AUS** wenn Innenraumtemperatur <= `temp_off` (Default: 22°C)
- Dazwischen: Hysterese-Band, Zustand bleibt unveraendert
- **Notabschaltung** wenn Kuehlblock >= `temp_max` (Default: 60°C)

### Luefter-PID-Regelung

Der Luefter haelt die Kuehlblock-Temperatur am Zielwert (Default: 45°C):

- Kuehlblock unter Ziel → Luefter aus
- Kuehlblock ueber Ziel → PID regelt PWM hoch (proportional zur Uebertemperatur)
- Kuehlblock >= Max → Volle Drehzahl + Peltier aus

PID-Startwerte: Kp=2.0, Ki=0.5, Kd=1.0 — ueber Webinterface live anpassbar.

## Software-Architektur

### FreeRTOS Tasks

| Task | Prioritaet | Intervall | Funktion |
|---|---|---|---|
| `sensor` | 5 | 2s | Liest beide DS18B20 per OneWire |
| `fan_pid` | 4 | 1s | PID-Regelung Luefter + Peltier Ein/Aus |
| `scheduler` | 3 | 30s | Prueft Zeitfenster (SNTP/CET) |
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

- Live-Anzeige: Innenraum-Temperatur, Kuehlblock-Temperatur, Luefter-%, Peltier AN/AUS
- Einstellbar: Temperatur-Schwellen (on/off/max/target), PID-Parameter, Zeitfenster
- Auto-Refresh alle 3 Sekunden
- REST API: `GET /api/status`, `POST /api/config`

### Captive Portal (AP-Modus)

- Automatische Weiterleitung auf `http://10.1.1.1/`
- WiFi-SSID und Passwort eingeben → ESP32 verbindet sich mit Router
- AP-SSID: `ESP32-Cooler-Setup` (offen, kein Passwort)
- DNS-Server leitet alle Anfragen auf 10.1.1.1 → Captive-Detection der Clients greift

## Konfiguration (NVS)

Alle Einstellungen werden im Non-Volatile Storage (NVS) des ESP32 gespeichert und ueberleben Neustarts:

| Parameter | NVS-Key | Default | Beschreibung |
|---|---|---|---|
| WiFi SSID | `wifi_ssid` | (leer) | Router-SSID |
| WiFi Passwort | `wifi_pass` | (leer) | WPA2-Passwort |
| Peltier AN | `temp_on` | 25.0°C | Einschalt-Schwelle Innenraum |
| Peltier AUS | `temp_off` | 22.0°C | Ausschalt-Schwelle Innenraum |
| Kuehlblock Max | `temp_max` | 60.0°C | Sicherheits-Cutoff |
| Kuehlblock Ziel | `temp_tgt` | 45.0°C | PID-Sollwert |
| PID Kp | `pid_kp` | 2.0 | Proportional-Anteil |
| PID Ki | `pid_ki` | 0.5 | Integral-Anteil |
| PID Kd | `pid_kd` | 1.0 | Differential-Anteil |
| Wochentag AN | `sch_wd_on` | 08:00 | Betriebsstart Mo-Fr |
| Wochentag AUS | `sch_wd_off` | 22:00 | Betriebsende Mo-Fr |
| Wochenende AN | `sch_we_on` | 09:00 | Betriebsstart Sa-So |
| Wochenende AUS | `sch_we_off` | 23:00 | Betriebsende Sa-So |

## Build & Flash

Voraussetzung: ESP-IDF 5.x installiert und `IDF_PATH` gesetzt.

```bash
cd esp32_cooler
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

## PID-Tuning (Inbetriebnahme)

1. Webinterface oeffnen, Ki=0, Kd=0 setzen
2. Kp erhoehen bis Luefter bei ~5°C ueber Zieltemp auf ~80% laeuft
3. Ki langsam erhoehen (0.1-Schritte) bis stationaere Abweichung verschwindet
4. Falls Luefter pendelt: Kd erhoehen (0.5-Schritte) zur Daempfung
5. Alle Werte werden sofort in NVS persistiert

## Sicherheit

- Peltier-GPIO hat Hardware-Pulldown → AUS bei ESP32-Reset/Brownout
- Kuehlblock-Temperatur wird alle 2s geprueft
- Bei Ueberschreitung von `temp_max`: sofortige Peltier-Abschaltung + Luefter 100%
- Bei Sensorausfall (CRC-Fehler, kein Signal): System deaktiviert (fail-safe)
- Scheduler inaktiv → alles aus

## Lizenz

Privates Projekt, keine oeffentliche Lizenz.
