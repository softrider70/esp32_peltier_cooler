#pragma once

#include <stdbool.h>
#include <stdint.h>

// DSM Konfiguration
#define DSM_HOST        "192.168.1.10"
#define DSM_PORT        8080
#define DSM_ENDPOINT    "/espcooler_data/api.php"
#define DSM_DEVICE_ID   "espcooler_001"

// DSM Uploader initialisieren
void dsm_uploader_init(void);

// Daten zur DSM hochladen (nach NVS-Save)
void dsm_uploader_sync(void);

// DSM-Status prüfen
bool dsm_uploader_is_enabled(void);
