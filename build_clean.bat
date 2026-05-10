@echo off
REM ESP32 Cooler Build Script with Embed Files Cache Clean
REM Loescht nur .S Dateien (eingebettete Dateien), nicht gesamten Cache

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

echo Schritt 1: Eingebettete Dateien (.S) loeschen...
if exist build\*.S del /q build\*.S
if exist build\esp-idf\main\CMakeFiles\__idf_main.dir\__\*.S.obj del /q build\esp-idf\main\CMakeFiles\__idf_main.dir\__\*.S.obj
echo Eingebettete Dateien geloescht

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
