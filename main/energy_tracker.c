#include "energy_tracker.h"
#include "config.h"
#include "sensor.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <time.h>
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "energy_tracker";

// Ringbuffer für 10 Sitzungen
static energy_session_t s_sessions[ENERGY_SESSIONS_MAX];
static uint8_t s_write_index = 0;
static uint16_t s_session_count = 0;  // Anzahl der gespeicherten Sitzungen

// Tracking-Status
static bool s_tracking_active = false;
static uint32_t s_start_time = 0;  // Unix Timestamp
static uint32_t s_start_uptime = 0; // Uptime für Dauer-Berechnung
static float s_start_temp = 0.0f;
static float s_min_temp = 0.0f;
static float s_max_temp = 0.0f;
static float s_energy_accumulated = 0.0f;

// NVS-Keys
#define NVS_KEY_ENERGY_SESSIONS "energy_sessions"
#define NVS_KEY_ENERGY_INDEX    "energy_index"

// Peltier-Leistung (3.5A * 12V = 42W)
#define PELTIER_POWER_W 42.0f

// Auto-Duty Messwert-Tracking
static float s_ad_cycle_measured = 0.0f;  // Gemessener AD-Cycle Wert

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
    
    // Versuche NTP-Zeit zu verwenden, fallback auf Uptime
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    
    if (timeinfo.tm_year >= (2020 - 1900)) {
        // NTP-Zeit verfügbar - verwende Unix-Timestamp
        s_start_time = (uint32_t)now;
        s_start_uptime = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        ESP_LOGI(TAG, "Session started: time=%lu (NTP), start_temp=%.1f°C", s_start_time, s_start_temp);
    } else {
        // Keine NTP-Zeit - verwende Uptime als Fallback
        s_start_time = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        s_start_uptime = s_start_time;
        ESP_LOGW(TAG, "Session started: time=%lu (Uptime, no NTP), start_temp=%.1f°C", s_start_time, s_start_temp);
    }
    
    s_energy_accumulated = 0.0f;
    
    // Start-Temperatur speichern und Min/Max initialisieren
    sensor_data_t sd = sensor_get_data();
    s_start_temp = sd.indoor_valid ? sd.temp_indoor : 0.0f;
    s_min_temp = s_start_temp;
    s_max_temp = s_start_temp;
}

void energy_tracker_stop_session(void) {
    if (!s_tracking_active) {
        ESP_LOGW(TAG, "No active session, ignoring stop request");
        return;
    }
    
    uint32_t end_time = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    uint32_t duration_sec = end_time - s_start_uptime;  // Korrekte Dauer-Berechnung mit Uptime
    uint16_t duration_min = (uint16_t)(duration_sec / 60);
    
    // End-Temperatur speichern und Min/Max aktualisieren
    sensor_data_t sd = sensor_get_data();
    float end_temp = sd.indoor_valid ? sd.temp_indoor : 0.0f;
    
    // Min/Max während der Session aktualisieren
    if (sd.indoor_valid && sd.temp_indoor < s_min_temp) {
        s_min_temp = sd.temp_indoor;
    }
    if (sd.indoor_valid && sd.temp_indoor > s_max_temp) {
        s_max_temp = sd.temp_indoor;
    }
    
    // Sitzung im Ringbuffer speichern (kompaktes Format)
    app_config_t *cfg = nvs_config_get();
    energy_session_t session = {
        .timestamp = s_start_time,
        .duration_min = duration_min,
        .start_temp = (int16_t)(s_start_temp * 10.0f),  // 0.1°C Schritte ohne Rundung
        .end_temp = (int16_t)(end_temp * 10.0f),        // 0.1°C Schritte ohne Rundung
        .energy_wh = (uint16_t)(s_energy_accumulated * 100.0f),  // 0.01Wh Schritte ohne Rundung
        .min_temp = (int16_t)(s_min_temp * 10.0f),      // 0.1°C Schritte ohne Rundung
        .max_temp = (int16_t)(s_max_temp * 10.0f),      // 0.1°C Schritte ohne Rundung
        .pwm_period = cfg->peltier_pwm_period,
        .auto_duty_enabled = cfg->auto_duty_en,
        .auto_duty_cycle = (uint8_t)s_ad_cycle_measured  // Gemessener AD-Cycle
    };
    
    s_sessions[s_write_index] = session;
    s_write_index = (s_write_index + 1) % ENERGY_SESSIONS_MAX;
    
    if (s_session_count < ENERGY_SESSIONS_MAX) {
        s_session_count++;
    }
    
    s_tracking_active = false;
    s_energy_accumulated = 0.0f;
    
    ESP_LOGI(TAG, "Session stopped: duration=%dmin, start=%.1f°C, end=%.1f°C, min=%.1f°C, max=%.1f°C, energy=%.3fWh",
             duration_min, s_start_temp, end_temp, s_min_temp, s_max_temp, s_energy_accumulated);
    
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
    
    // AD-Cycle Messwert aktualisieren (wenn Auto-Duty aktiv)
    if (pwm_duty_percent > 0.0f) {
        s_ad_cycle_measured = pwm_duty_percent;
    }
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
    ESP_LOGI(TAG, "Saving %d energy sessions to NVS (size: %zu bytes)", s_session_count, sizeof(s_sessions));
    err = nvs_set_blob(handle, NVS_KEY_ENERGY_SESSIONS, s_sessions, sizeof(s_sessions));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save energy sessions to NVS: %s (size: %zu bytes)", esp_err_to_name(err), sizeof(s_sessions));
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

void energy_tracker_clear_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for clearing energy sessions");
        return;
    }
    
    // Energie-Sessions löschen
    err = nvs_erase_key(handle, NVS_KEY_ENERGY_SESSIONS);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase energy sessions from NVS");
    } else {
        ESP_LOGI(TAG, "Energy sessions erased from NVS");
    }
    
    // Index löschen
    err = nvs_erase_key(handle, NVS_KEY_ENERGY_INDEX);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase energy index from NVS");
    } else {
        ESP_LOGI(TAG, "Energy index erased from NVS");
    }
    
    // Commit
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes");
    } else {
        ESP_LOGI(TAG, "NVS commit successful");
    }
    
    nvs_close(handle);
    
    // Lokalen Speicher zurücksetzen
    memset(s_sessions, 0, sizeof(s_sessions));
    s_write_index = 0;
    s_session_count = 0;
    
    ESP_LOGI(TAG, "Energy tracker reset complete");
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
    
    // Prüfen ob die geladenen Daten zur neuen Struktur passen
    if (required_size != sizeof(s_sessions)) {
        ESP_LOGW(TAG, "Energy sessions size mismatch: expected %zu, got %zu - old structure detected", 
                 sizeof(s_sessions), required_size);
        nvs_close(handle);
        ESP_LOGI(TAG, "Clearing incompatible old energy data");
        energy_tracker_clear_nvs();
        return;
    }
    
    // Prüfen ob die geladenen Sessions gültige Timestamps haben
    int valid_sessions = 0;
    for (int i = 0; i < ENERGY_SESSIONS_MAX; i++) {
        if (s_sessions[i].timestamp >= 1600000000 && s_sessions[i].timestamp <= 2200000000) { // Plausible Zeit 2020-2039
            valid_sessions++;
        } else {
            // Ungültige Session zurücksetzen
            memset(&s_sessions[i], 0, sizeof(energy_session_t));
        }
    }
    
    ESP_LOGI(TAG, "Energy sessions loaded from NVS: %d valid sessions", valid_sessions);
    
    // Index laden
    uint8_t loaded_index = 0;
    err = nvs_get_u8(handle, NVS_KEY_ENERGY_INDEX, &loaded_index);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load energy index, using 0");
        loaded_index = 0;
    }
    
    nvs_close(handle);
    
    s_write_index = loaded_index % ENERGY_SESSIONS_MAX;
    s_session_count = valid_sessions;
    
    ESP_LOGI(TAG, "Loaded %d valid energy sessions from NVS", s_session_count);
}