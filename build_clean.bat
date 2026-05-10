@echo off
REM ESP32 Cooler Build Script with Cache Clean
REM Loescht build-Verzeichnis vor dem Build, um index.html Aenderungen zu erzwingen

echo ========================================
echo ESP32 Cooler Build Script
echo ========================================
echo.

REM ESP-IDF Umgebung aktivieren
call c:\Users\win4g\Downloads\GitHub\VS-Projekte\CascadeProjects\esp-idf\export.bat
if %errorlevel% neq 0 (
    echo Fehler: ESP-IDF konnte nicht aktiviert werden
    exit /b 1
)

echo Schritt 1: Build-Verzeichnis loeschen...
if exist build (
    rmdir /s /q build
    echo Build-Verzeichnis geloescht
) else (
    echo Build-Verzeichnis existiert nicht (uebersprungen)
)

echo.
echo Schritt 2: Build ausfuehren...
idf.py build
if %errorlevel% neq 0 (
    echo Fehler: Build fehlgeschlagen
    exit /b 1
)

echo.
echo ========================================
echo Build erfolgreich!
echo ========================================
echo.
echo Zum Flashen:
echo   idf.py -p COM3 flash
echo.
echo Oder OTA:
echo   copy build\esp32_cooler.bin firmware.bin
echo   python -m http.server 8080
echo   curl -X POST -d "url=http://192.168.1.191:8080/firmware.bin" http://192.168.1.227/api/ota
