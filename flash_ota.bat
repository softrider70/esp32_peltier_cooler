@echo off
REM OTA Flash Script
REM Startet OTA-Server auf 192.168.1.191 und triggert ESP32 auf 192.168.1.227

echo Starte OTA-Server auf 192.168.1.191:8080...
cd build
start /B python -m http.server 8080 --bind 192.168.1.191
timeout /t 2 /nobreak > nul

echo Trigger OTA auf ESP32 (192.168.1.227)...
curl -X POST http://192.168.1.227/api/ota -d "url=http://192.168.1.191:8080/firmware.bin"

echo OTA gestartet! Server laeuft im Hintergrund.
