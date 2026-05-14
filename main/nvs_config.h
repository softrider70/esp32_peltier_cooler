#pragma once

#include <stdint.h>

// Application configuration (persisted in NVS)
typedef struct {
    char     wifi_ssid[33];
    char     wifi_pass[65];
    float    temp_peltier_on;       // Indoor temp: peltier ON above this
    float    temp_peltier_off;      // Indoor temp: peltier OFF below this
    float    temp_heatsink_max;     // Safety cutoff temperature
    float    temp_heatsink_target;  // Target temperature for heatsink
    uint16_t sched_on[7];           // Daily on times (hours 0-23, stored as minutes)
    uint16_t sched_off[7];          // Daily off times (hours 0-23, stored as minutes)
    uint16_t data_log_interval;     // Data logging interval in seconds (default: 10s)
    float    energy_wh;             // Gesamtenergie in Wh
    float    energy_day;            // Tagesenergie in Wh
    float    energy_week;           // Wochenenergie in Wh
    float    energy_month;          // Monatsenergie in Wh
    uint32_t last_date;             // Zuletzt gespeichertes Datum (YYYYMMDD)
    uint8_t  last_week;            // Zuletzt gespeicherte Kalenderwoche (0-53)
    uint8_t  last_month;           // Zuletzt gespeicherter Monat (0-11)
    
    // Peltier PWM (langsames PWM für Stromspar-Modus)
    uint16_t peltier_pwm_period;  // PWM-Periode in Sekunden (z.B. 10s)
    uint8_t peltier_pwm_duty;     // PWM-Duty-Cycle in % (0-100)

    // Auto-Duty Regelung
    bool auto_duty_en;            // Auto-Duty Hauptschalter
    uint8_t auto_duty_duty;       // Auto-Duty Duty-Cycle in % (0-100)
    uint16_t auto_duty_cycle;     // Auto-Duty Zyklusdauer in Sekunden
} app_config_t;

// Initialize NVS and load config (or set defaults)
void nvs_config_init(void);

// Get pointer to current runtime config (thread-safe read)
app_config_t* nvs_config_get(void);

// Save current config to NVS
void nvs_config_save(void);

// Update WiFi credentials and save
void nvs_config_set_wifi(const char *ssid, const char *password);

// Delete WiFi credentials from NVS
void nvs_config_delete_wifi_credentials(void);

// Save only energy data to NVS (for frequent updates)
void nvs_config_save_energy(void);

// Reset energy data
void nvs_config_reset_energy(void);

// Factory reset: Erase all NVS data and restore defaults
void nvs_config_factory_reset(void);
