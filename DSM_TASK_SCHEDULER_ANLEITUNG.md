# DSM Task Scheduler Anleitung für ESP32 Cooler API

## Ziel
Python HTTP-Server automatisch beim DSM-Start starten (Port 8080)

## Voraussetzungen
- Python 3.8.12 ist auf DSM installiert
- Server-Datei: `/volume1/web/espcooler_data/server.py`
- Server läuft bereits manuell (getestet)

## Schritte

### 1. Synology Task Scheduler öffnen
- Öffne DSM-Admin-Interface
- Gehe zu: Systemsteuerung → Aufgabenplaner
- Klicke auf "Erstellen" → "Geplante Aufgabe" → "Benutzerdefiniertes Skript"

### 2. Aufgabe konfigurieren
**Allgemein:**
- Aufgabe: `ESP32 Cooler API Server`
- Benutzer: `ds918admin` (oder der User mit Zugriff auf /volume1/web/espcooler_data)
- Aktiviert: Ja

**Zeitplan:**
- Häufigkeit: "Beim Hochfahren"
- Zeitpunkt: "Aufgestartet" (5 Sekunden nach dem Hochfahren)

**Aufgabeneinstellungen:**
- Benutzerdefiniertes Skript: `python3 /volume1/web/espcooler_data/server.py`

### 3. Server bei Bedarf stoppen
Wenn der Server bereits läuft (manuell gestartet), zuerst stoppen:
```bash
ssh ds918root@192.168.1.10
kill $(ps aux | grep 'server.py' | grep -v grep | awk '{print $2}')
```

### 4. Aufgabe testen
- In Aufgabenplaner: Aufgabe auswählen → "Ausführen"
- Prüfen, ob Server läuft: `ps aux | grep server.py`
- Test: `curl http://192.168.1.10:8080/?action=list`

## Manuelles Starten (falls Task Scheduler nicht funktioniert)
```bash
ssh ds918root@192.168.1.10
nohup python3 /volume1/web/espcooler_data/server.py > /tmp/server.log 2>&1 &
```

## Server testen
```bash
# GET-Test
curl http://192.168.1.10:8080/?action=list

# POST-Test
curl -X POST -H "Content-Type: application/json" -d @test_dsm.json http://192.168.1.10:8080/
```

## ESP32 Konfiguration
Port: 8080 (bereits konfiguriert)
IP: 192.168.1.10
Endpoint: `/` (POST für Upload, `/?action=list` für GET)

## Fehlersuche
- Log-Datei prüfen: `/tmp/server.log`
- Prozess prüfen: `ps aux | grep server.py`
- Port prüfen: `netstat -tlnp | grep 8080`
