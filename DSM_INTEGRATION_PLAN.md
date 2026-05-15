# DSM DS918+ Integration Plan

Integration der Synology DSM DS918+ (192.168.1.10) als zentraler Speicherort für Logs und Daten, die auf dem ESP32 zu viel Platz belegen.

## Anforderungen

### Daten
- **Aktuell:** Verbrauchsdaten
- **Zukunft:** Ohne Limit (erweiterbar für Logs, Sensor-Daten, etc.)

### Übertragung
- **Protokoll:** HTTP POST
- **Trigger:** Bestimmtes Ereignis (z.B. wenn Daten im ESP32 im NVS gespeichert werden)
- **Format:** JSON
- **Ziel:** Webverzeichnis auf DSM DS918+ (192.168.1.10)

### ESP32 Verhalten
- Keine Änderung des aktuellen Vorgangs auf dem ESP32
- Daten werden weiterhin lokal im NVS gespeichert
- Zusätzliche HTTP POST Übertragung zur DSM als Backup/Archiv

## Architektur

### ESP32 Seite
1. **HTTP POST Funktion** für DSM Übertragung
   - Endpoint: `http://192.168.1.10/web/api/data`
   - JSON Payload mit Verbrauchsdaten
   - Fehlerbehandlung bei Übertragungsfehlern

2. **Trigger Mechanismus**
   - Aufruf nach NVS Speicherung
   - Optional: Periodische Übertragung (z.B. stündlich/täglich)
   - Retry-Logik bei Übertragungsfehlern

### DSM Seite
1. **Web-Verzeichnis** für Empfang der Daten
   - Pfad: `/web/espcooler_data/`
   - PHP oder Python Script für Empfang und Speicherung
   - Dateibenennung: `YYYYMMDD_HHMMSS_consumption.json`

2. **Datenstruktur**
   - Speicherung in Dateisystem oder Datenbank
   - Automatische Bereinigung alter Daten (optional)

## JSON Format

```json
{
  "timestamp": "2026-05-15T10:00:00Z",
  "device_id": "espcooler_001",
  "consumption_data": {
    "total_liters": 123.45,
    "valve_open_count": 42,
    "total_open_time_ms": 765432
  },
  "metadata": {
    "version": "1.0.0",
    "build_number": 12
  }
}
```

## Implementierungsschritte

### Phase 1: DSM Setup
1. Web-Verzeichnis auf DSM erstellen
2. PHP/Python Script für Datenempfang erstellen
3. Schreibrechte konfigurieren
4. Test-Endpoint erstellen

### Phase 2: ESP32 HTTP POST
1. HTTP Client Funktion implementieren
2. JSON Payload erstellen
3. Integration mit NVS Speicherungs-Trigger
4. Fehlerbehandlung und Retry-Logik
5. Logging für Übertragungsstatus

### Phase 3: Testing
1. Unit Tests für HTTP POST Funktion
2. Integration Tests mit DSM
3. Fehlerfall-Tests (DSM offline, Netzwerkprobleme)
4. Performance Tests (große Datenmengen)

## Sicherheitsaspekte

- Authentifizierung für DSM Endpoint (API Key oder Basic Auth)
- HTTPS für verschlüsselte Übertragung (optional)
- Validierung der eingehenden Daten auf DSM Seite
- Rate Limiting auf DSM Seite

## Konfiguration

### ESP32 Konfiguration (sdkconfig)
```
CONFIG_DSM_ENABLED=y
CONFIG_DSM_HOST="192.168.1.10"
CONFIG_DSM_PORT=80
CONFIG_DSM_ENDPOINT="/web/api/data"
CONFIG_DSM_API_KEY="your_api_key_here"
CONFIG_DSM_USE_HTTPS=n
```

### DSM Konfiguration
- Web-Server aktivieren
- PHP oder Python Runtime installieren
- Web-Verzeichnis erstellen und konfigurieren

## Backup & Archivierung

- DSM als primäres Archiv für Langzeit-Daten
- ESP32 NVS als lokaler Cache für aktuelle Daten
- Optionale Cloud-Backup von DSM Daten
