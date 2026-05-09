#pragma once

#include <stdbool.h>

// Default OTA firmware URL
#define OTA_DEFAULT_URL "http://192.168.1.191:8080/firmware.bin"

// NVS key for OTA server URL
#define NVS_KEY_OTA_URL  "ota_url"

// Maximum URL length
#define OTA_URL_MAX_LEN  256

// OTA status codes for web UI
typedef enum {
    OTA_IDLE,
    OTA_IN_PROGRESS,
    OTA_SUCCESS,
    OTA_FAILED,
} ota_status_t;

// Initialize OTA subsystem: validate current firmware after successful boot
void ota_init(void);

// Start OTA update from configured URL (runs in background task)
void ota_start_update(const char *url);

// Get current OTA status
ota_status_t ota_get_status(void);

// Get last OTA error message (empty if none)
const char* ota_get_error(void);

// Get OTA URL from NVS (or default)
const char* ota_get_url(void);

// Set OTA URL and persist to NVS
void ota_set_url(const char *url);
