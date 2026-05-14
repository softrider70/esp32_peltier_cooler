#include "energy_tracker.h"
#include "sensor.h"
#include "peltier.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "energy_tracker";

// Ringbuffer für 50 Sitzungen
static energy_session_t s_sessions[ENERGY_SESSIONS_MAX];
static uint8_t s_write_index = 0;
static uint16_t s_session_count = 0;  // Anzahl der gespeicherten Sitzungen

// Tracking-Status
static bool s_tracking_active = false;
static uint32_t s_start_time = 0;
static float s_start_temp = 0.0f;
static float s_min_temp = 0.0f;
static float s_max_temp = 0.0f;
static float s_energy_accumulated = 0.0f;

// NVS-Keys
#define NVS_KEY_ENERGY_SESSIONS "energy_sessions"
#define NVS_KEY_ENERGY_INDEX    "energy_index"

// Peltier-Leistung (3.5A * 12V = 42W)
#define PELTIER_POWER_W 42.0f

void energy_tracker_init(void) {
    // Ringbuffer initialisieren
    memset(s_sessions, 0, sizeof(s_sessions));
    s_write_index = 0;
    s_session_count = 0;
    s_tracking_active = false;
    s_energy_accumulated = 0.0f;
    
    // Aus NVS laden
    energy_tracker_load_from_nvs();
    
    ESP_LOGI(TAG, "Energy tracker initialized: %d sessions", s_session_count);
}

void energy_tracker_start_session(void) {
    if (s_tracking_active) {
        ESP_LOGW(TAG, "Session already active, ignoring start request");
        return;
    }
    
    s_tracking_active = true;
    s_start_time = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    s_energy_accumulated = 0.0f;
    
    // Start-Temperatur speichern und Min/Max initialisieren
    sensor_data_t sd = sensor_get_data();
    s_start_temp = sd.indoor_valid ? sd.temp_indoor : 0.0f;
    s_min_temp = s_start_temp;
    s_max_temp = s_start_temp;
    
    ESP_LOGI(TAG, "Session started: time=%lu, start_temp=%.1f°C", s_start_time, s_start_temp);
}

void energy_tracker_stop_session(void) {
    if (!s_tracking_active) {
        ESP_LOGW(TAG, "No active session, ignoring stop request");
        return;
    }
    
    uint32_t end_time = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t duration = end_time - s_start_time;
    
    // End-Temperatur speichern
    sensor_data_t sd = sensor_get_data();
    float end_temp = sd.indoor_valid ? sd.temp_indoor : 0.0f;
    
    // Sitzung im Ringbuffer speichern
    app_config_t *cfg = nvs_config_get();
    energy_session_t session = {
        .timestamp = s_start_time,
        .duration_sec = duration,
        .start_temp = s_start_temp,
        .end_temp = end_temp,
        .min_temp = s_min_temp,
        .max_temp = s_max_temp,
        .energy_wh = s_energy_accumulated,
        .pwm_period = cfg->peltier_pwm_period,
        .auto_duty_enabled = cfg->auto_duty_en,
        .auto_duty_cycle = cfg->auto_duty_cycle,
        .target_temp = cfg->temp_heatsink_target
    };
    
    s_sessions[s_write_index] = session;
    s_write_index = (s_write_index + 1) % ENERGY_SESSIONS_MAX;
    
    if (s_session_count < ENERGY_SESSIONS_MAX) {
        s_session_count++;
    }
    
    s_tracking_active = false;
    s_energy_accumulated = 0.0f;
    
    ESP_LOGI(TAG, "Session stopped: duration=%lus, start=%.1f°C, end=%.1f°C, energy=%.3fWh",
             duration, s_start_temp, end_temp, s_energy_accumulated);
    
    // In NVS speichern
    energy_tracker_save_to_nvs();
}

void energy_tracker_update_energy(float pwm_duty_percent) {
    if (!s_tracking_active) {
        return;
    }
    
    // Min/Max Temperaturen aktualisieren
    sensor_data_t sd = sensor_get_data();
    if (sd.indoor_valid) {
        if (sd.temp_indoor < s_min_temp) {
            s_min_temp = sd.temp_indoor;
        }
        if (sd.temp_indoor > s_max_temp) {
            s_max_temp = sd.temp_indoor;
        }
    }
    
    // Energie für 1 Sekunde berechnen: P * (duty/100) * (1/3600)
    float power = PELTIER_POWER_W * (pwm_duty_percent / 100.0f);
    float energy_wh = power / 3600.0f;  // Jede Sekunde
    
    s_energy_accumulated += energy_wh;
}

bool energy_tracker_is_tracking(void) {
    return s_tracking_active;
}

const energy_session_t* energy_tracker_get_sessions(uint16_t *count) {
    *count = s_session_count;
    return s_sessions;
}

void energy_tracker_save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for saving energy sessions");
        return;
    }
    
    // Sitzungen als Blob speichern
    err = nvs_set_blob(handle, NVS_KEY_ENERGY_SESSIONS, s_sessions, sizeof(s_sessions));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save energy sessions to NVS");
        nvs_close(handle);
        return;
    }
    
    // Index speichern
    err = nvs_set_u8(handle, NVS_KEY_ENERGY_INDEX, s_write_index);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save energy index to NVS");
        nvs_close(handle);
        return;
    }
    
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit energy sessions to NVS");
    } else {
        ESP_LOGI(TAG, "Energy sessions saved to NVS (%u sessions)", s_session_count);
    }
    
    nvs_close(handle);
}

void energy_tracker_load_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No energy sessions in NVS (first boot or not saved)");
        return;
    }
    
    size_t required_size = sizeof(s_sessions);
    err = nvs_get_blob(handle, NVS_KEY_ENERGY_SESSIONS, s_sessions, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load energy sessions from NVS");
        nvs_close(handle);
        return;
    }
    
    // Index laden
    uint8_t index;
    if (nvs_get_u8(handle, NVS_KEY_ENERGY_INDEX, &index) == ESP_OK) {
        s_write_index = index;
    } else {
        s_write_index = 0;
    }
    
    // Sitzungen zählen (nicht-leere Einträge)
    s_session_count = 0;
    for (int i = 0; i < ENERGY_SESSIONS_MAX; i++) {
        if (s_sessions[i].timestamp > 0) {
            s_session_count++;
        }
    }
    
    ESP_LOGI(TAG, "Energy sessions loaded from NVS (%u sessions, index=%u)",
             s_session_count, s_write_index);
    
    nvs_close(handle);
}