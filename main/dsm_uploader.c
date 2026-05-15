#include "dsm_uploader.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "energy_tracker.h"
#include "nvs_config.h"
#include <string.h>

static const char *TAG = "DSM_UPLOADER";
static bool s_dsm_enabled = true;

// DSM Uploader initialisieren
void dsm_uploader_init(void) {
    ESP_LOGI(TAG, "DSM Uploader initialisiert");
    ESP_LOGI(TAG, "Host: %s:%d", DSM_HOST, DSM_PORT);
    ESP_LOGI(TAG, "Endpoint: %s", DSM_ENDPOINT);
    ESP_LOGI(TAG, "Device ID: %s", DSM_DEVICE_ID);
}

// DSM-Status prüfen
bool dsm_uploader_is_enabled(void) {
    return s_dsm_enabled;
}

// Daten zur DSM hochladen (nach NVS-Save)
void dsm_uploader_sync(void) {
    if (!s_dsm_enabled) {
        return;
    }

    ESP_LOGI(TAG, "Starte DSM Sync...");

    // Sessions abrufen
    uint16_t session_count = 0;
    const energy_session_t *sessions = energy_tracker_get_sessions(&session_count);
    
    if (session_count == 0) {
        ESP_LOGW(TAG, "Keine Sessions zum Hochladen");
        return;
    }

    // JSON Payload erstellen
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", DSM_DEVICE_ID);
    cJSON_AddNumberToObject(root, "build", BUILD_NUMBER);
    
    // Sessions Array
    cJSON *sessions_array = cJSON_CreateArray();
    for (int i = 0; i < session_count; i++) {
        cJSON *session = cJSON_CreateObject();
        cJSON_AddNumberToObject(session, "timestamp", sessions[i].timestamp);
        cJSON_AddNumberToObject(session, "duration_min", sessions[i].duration_min);
        cJSON_AddNumberToObject(session, "start_temp", sessions[i].start_temp / 10.0f);
        cJSON_AddNumberToObject(session, "end_temp", sessions[i].end_temp / 10.0f);
        cJSON_AddNumberToObject(session, "min_temp", sessions[i].min_temp / 10.0f);
        cJSON_AddNumberToObject(session, "max_temp", sessions[i].max_temp / 10.0f);
        cJSON_AddNumberToObject(session, "energy_wh", sessions[i].energy_wh / 100.0f);
        cJSON_AddNumberToObject(session, "pwm_period", sessions[i].pwm_period);
        cJSON_AddNumberToObject(session, "auto_duty_cycle", sessions[i].auto_duty_cycle);
        cJSON_AddBoolToObject(session, "auto_duty_enabled", sessions[i].auto_duty_enabled);
        cJSON_AddItemToArray(sessions_array, session);
    }
    cJSON_AddItemToObject(root, "sessions", sessions_array);
    
    // Stats
    app_config_t *cfg = nvs_config_get();
    cJSON *stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats, "energy_wh", cfg->energy_wh);
    cJSON_AddNumberToObject(stats, "energy_day", cfg->energy_day);
    cJSON_AddNumberToObject(stats, "energy_week", cfg->energy_week);
    cJSON_AddNumberToObject(stats, "energy_month", cfg->energy_month);
    cJSON_AddItemToObject(root, "stats", stats);
    
    // JSON to String
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "JSON Erstellung fehlgeschlagen");
        return;
    }

    ESP_LOGI(TAG, "JSON Payload: %s", json_string);

    // HTTP POST Request
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d%s", DSM_HOST, DSM_PORT, DSM_ENDPOINT);
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP Client Initialisierung fehlgeschlagen");
        free(json_string);
        return;
    }
    
    // Headers setzen
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    // Body setzen
    esp_http_client_set_post_field(client, json_string, strlen(json_string));
    
    // Request ausführen
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status: %d", status);
        if (status == 200) {
            ESP_LOGI(TAG, "Daten erfolgreich zur DSM hochgeladen");
        } else {
            ESP_LOGW(TAG, "HTTP Status nicht 200: %d", status);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST fehlgeschlagen: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    free(json_string);
}
