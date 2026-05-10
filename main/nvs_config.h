#pragma once

#include <stdint.h>

// Application configuration (persisted in NVS)
typedef struct {
    char     wifi_ssid[33];
    char     wifi_pass[65];
    float    temp_peltier_on;       // Indoor temp: peltier ON above this
    float    temp_peltier_off;      // Indoor temp: peltier OFF below this
    float    temp_heatsink_max;     // Safety cutoff temperature
    float    temp_heatsink_target;  // PID target for heatsink
    float    pid_kp;
    float    pid_ki;
    float    pid_kd;
    uint16_t sched_on[7];           // Daily on times (hours 0-23, stored as minutes)
    uint16_t sched_off[7];          // Daily off times (hours 0-23, stored as minutes)
} app_config_t;

// Initialize NVS and load config (or set defaults)
void nvs_config_init(void);

// Get pointer to current runtime config (thread-safe read)
app_config_t* nvs_config_get(void);

// Save current config to NVS
void nvs_config_save(void);

// Update WiFi credentials and save
void nvs_config_set_wifi(const char *ssid, const char *password);
