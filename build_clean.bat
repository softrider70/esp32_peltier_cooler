@echo off
REM ESP32 Cooler Build Script
REM Normaler Build - wenn index.html nicht aktualisiert wird: build-Verzeichnis manuell loeschen

echo ========================================
echo ESP32 Cooler Build Script
echo ========================================
echo.
echo HINWEIS: Wenn index.html Aenderungen nicht uebernommen werden,
echo           build-Verzeichnis manuell loeschen: rmdir /s /q build
echo.

REM ESP-IDF Umgebung aktivieren
call c:\Users\win4g\Downloads\GitHub\VS-Projekte\CascadeProjects\esp-idf\export.bat
if %errorlevel% neq 0 (
    echo Fehler: ESP-IDF konnte nicht aktiviert werden
    exit /b 1
)

echo Build ausfuehren...
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
