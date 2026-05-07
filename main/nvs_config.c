#include "nvs_config.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_config";
static app_config_t s_config;

static void load_defaults(void) {
    memset(&s_config, 0, sizeof(s_config));
    s_config.temp_peltier_on = TEMP_PELTIER_ON_DEFAULT;
    s_config.temp_peltier_off = TEMP_PELTIER_OFF_DEFAULT;
    s_config.temp_heatsink_max = TEMP_HEATSINK_MAX;
    s_config.temp_heatsink_target = TEMP_HEATSINK_TARGET;
    s_config.pid_kp = PID_KP_DEFAULT;
    s_config.pid_ki = PID_KI_DEFAULT;
    s_config.pid_kd = PID_KD_DEFAULT;
    s_config.sched_wd_on = 8 * 60;      // 08:00
    s_config.sched_wd_off = 22 * 60;    // 22:00
    s_config.sched_we_on = 9 * 60;      // 09:00
    s_config.sched_we_off = 23 * 60;    // 23:00
}

void nvs_config_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    load_defaults();

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t len;

        len = sizeof(s_config.wifi_ssid);
        nvs_get_str(handle, NVS_KEY_WIFI_SSID, s_config.wifi_ssid, &len);

        len = sizeof(s_config.wifi_pass);
        nvs_get_str(handle, NVS_KEY_WIFI_PASS, s_config.wifi_pass, &len);

        // Float values stored as int32 (x100 for 2 decimal precision)
        int32_t val;
        if (nvs_get_i32(handle, NVS_KEY_TEMP_ON, &val) == ESP_OK)
            s_config.temp_peltier_on = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_TEMP_OFF, &val) == ESP_OK)
            s_config.temp_peltier_off = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_TEMP_MAX, &val) == ESP_OK)
            s_config.temp_heatsink_max = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_TEMP_TARGET, &val) == ESP_OK)
            s_config.temp_heatsink_target = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_PID_KP, &val) == ESP_OK)
            s_config.pid_kp = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_PID_KI, &val) == ESP_OK)
            s_config.pid_ki = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_PID_KD, &val) == ESP_OK)
            s_config.pid_kd = val / 100.0f;

        uint16_t u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_WD_ON, &u16) == ESP_OK)
            s_config.sched_wd_on = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_WD_OFF, &u16) == ESP_OK)
            s_config.sched_wd_off = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_WE_ON, &u16) == ESP_OK)
            s_config.sched_we_on = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_WE_OFF, &u16) == ESP_OK)
            s_config.sched_we_off = u16;

        nvs_close(handle);
        ESP_LOGI(TAG, "Config loaded from NVS");
    } else {
        ESP_LOGI(TAG, "No config in NVS, using defaults");
    }
}

app_config_t* nvs_config_get(void) {
    return &s_config;
}

void nvs_config_save(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }

    nvs_set_str(handle, NVS_KEY_WIFI_SSID, s_config.wifi_ssid);
    nvs_set_str(handle, NVS_KEY_WIFI_PASS, s_config.wifi_pass);

    nvs_set_i32(handle, NVS_KEY_TEMP_ON, (int32_t)(s_config.temp_peltier_on * 100));
    nvs_set_i32(handle, NVS_KEY_TEMP_OFF, (int32_t)(s_config.temp_peltier_off * 100));
    nvs_set_i32(handle, NVS_KEY_TEMP_MAX, (int32_t)(s_config.temp_heatsink_max * 100));
    nvs_set_i32(handle, NVS_KEY_TEMP_TARGET, (int32_t)(s_config.temp_heatsink_target * 100));
    nvs_set_i32(handle, NVS_KEY_PID_KP, (int32_t)(s_config.pid_kp * 100));
    nvs_set_i32(handle, NVS_KEY_PID_KI, (int32_t)(s_config.pid_ki * 100));
    nvs_set_i32(handle, NVS_KEY_PID_KD, (int32_t)(s_config.pid_kd * 100));

    nvs_set_u16(handle, NVS_KEY_SCHED_WD_ON, s_config.sched_wd_on);
    nvs_set_u16(handle, NVS_KEY_SCHED_WD_OFF, s_config.sched_wd_off);
    nvs_set_u16(handle, NVS_KEY_SCHED_WE_ON, s_config.sched_we_on);
    nvs_set_u16(handle, NVS_KEY_SCHED_WE_OFF, s_config.sched_we_off);

    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "Config saved to NVS");
}

void nvs_config_set_wifi(const char *ssid, const char *password) {
    strncpy(s_config.wifi_ssid, ssid, sizeof(s_config.wifi_ssid) - 1);
    s_config.wifi_ssid[sizeof(s_config.wifi_ssid) - 1] = '\0';
    strncpy(s_config.wifi_pass, password, sizeof(s_config.wifi_pass) - 1);
    s_config.wifi_pass[sizeof(s_config.wifi_pass) - 1] = '\0';
    nvs_config_save();
}
