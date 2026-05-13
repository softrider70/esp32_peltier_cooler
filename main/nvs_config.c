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
    s_config.data_log_interval = 10;  // Default: 10 seconds
    s_config.energy_wh = 0.0f;  // Default: 0 Wh
    s_config.energy_day = 0.0f;  // Default: 0 Wh
    s_config.energy_week = 0.0f;
    s_config.energy_month = 0.0f;
    s_config.last_date = 0;
    s_config.peltier_pwm_period = PELTIER_PWM_PERIOD_DEFAULT;
    s_config.peltier_pwm_duty = PELTIER_PWM_DUTY_DEFAULT;
    
    // Default schedule: Alle Tage 8-23
    for (int i = 0; i < 7; i++) {
        s_config.sched_on[i] = 8 * 60;   // 8:00
        s_config.sched_off[i] = 23 * 60;  // 23:00
    }
}

void nvs_config_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGW(TAG, "NVS partition corrupted - erasing NVS");
        nvs_flash_erase();
        nvs_flash_init();
    } else if (ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS new version found - NOT erasing, attempting to read");
        // Don't erase - try to read existing data
    }

    load_defaults();

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        ESP_LOGI(TAG, "NVS opened successfully - loading config");
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

        ESP_LOGI(TAG, "Loaded from NVS: temp_peltier_on=%.1f, temp_peltier_off=%.1f",
                 s_config.temp_peltier_on, s_config.temp_peltier_off);
        if (nvs_get_i32(handle, NVS_KEY_TEMP_MAX, &val) == ESP_OK)
            s_config.temp_heatsink_max = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_TEMP_TARGET, &val) == ESP_OK)
            s_config.temp_heatsink_target = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_ENERGY_WH, &val) == ESP_OK)
            s_config.energy_wh = val / 100.0f;
        ESP_LOGI(TAG, "Loaded from NVS: energy_wh=%.2f Wh", s_config.energy_wh);
        if (nvs_get_i32(handle, NVS_KEY_ENERGY_DAY, &val) == ESP_OK)
            s_config.energy_day = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_ENERGY_WEEK, &val) == ESP_OK)
            s_config.energy_week = val / 100.0f;
        if (nvs_get_i32(handle, NVS_KEY_ENERGY_MONTH, &val) == ESP_OK)
            s_config.energy_month = val / 100.0f;
        if (nvs_get_u32(handle, NVS_KEY_LAST_DATE, &s_config.last_date) != ESP_OK)
            s_config.last_date = 0;
        ESP_LOGI(TAG, "Loaded from NVS: day=%.2f Wh, week=%.2f Wh, month=%.2f Wh",
                 s_config.energy_day, s_config.energy_week, s_config.energy_month);

        uint16_t u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_MO_ON, &u16) == ESP_OK) s_config.sched_on[0] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_MO_OFF, &u16) == ESP_OK) s_config.sched_off[0] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_DI_ON, &u16) == ESP_OK) s_config.sched_on[1] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_DI_OFF, &u16) == ESP_OK) s_config.sched_off[1] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_MI_ON, &u16) == ESP_OK) s_config.sched_on[2] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_MI_OFF, &u16) == ESP_OK) s_config.sched_off[2] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_DO_ON, &u16) == ESP_OK) s_config.sched_on[3] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_DO_OFF, &u16) == ESP_OK) s_config.sched_off[3] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_FR_ON, &u16) == ESP_OK) s_config.sched_on[4] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_FR_OFF, &u16) == ESP_OK) s_config.sched_off[4] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_SA_ON, &u16) == ESP_OK) s_config.sched_on[5] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_SA_OFF, &u16) == ESP_OK) s_config.sched_off[5] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_SO_ON, &u16) == ESP_OK) s_config.sched_on[6] = u16;
        if (nvs_get_u16(handle, NVS_KEY_SCHED_SO_OFF, &u16) == ESP_OK) s_config.sched_off[6] = u16;
        if (nvs_get_u16(handle, NVS_KEY_DATA_LOG_INTERVAL, &u16) == ESP_OK) {
            s_config.data_log_interval = u16;
            ESP_LOGI(TAG, "Loaded data_log_interval from NVS: %u", s_config.data_log_interval);
        } else {
            ESP_LOGI(TAG, "data_log_interval not found in NVS, using default: %u", s_config.data_log_interval);
        }
        if (nvs_get_u16(handle, NVS_KEY_PELTIER_PWM_PERIOD, &u16) == ESP_OK) {
            s_config.peltier_pwm_period = u16;
            ESP_LOGI(TAG, "Loaded peltier_pwm_period from NVS: %u", s_config.peltier_pwm_period);
        } else {
            ESP_LOGI(TAG, "peltier_pwm_period not found in NVS, using default: %u", s_config.peltier_pwm_period);
        }
        uint8_t u8;
        if (nvs_get_u8(handle, NVS_KEY_PELTIER_PWM_DUTY, &u8) == ESP_OK) {
            s_config.peltier_pwm_duty = u8;
            ESP_LOGI(TAG, "Loaded peltier_pwm_duty from NVS: %u", s_config.peltier_pwm_duty);
        } else {
            ESP_LOGI(TAG, "peltier_pwm_duty not found in NVS, using default: %u", s_config.peltier_pwm_duty);
        }
        ESP_LOGI(TAG, "Loaded from NVS: pwm_period=%u, pwm_duty=%u",
                 s_config.peltier_pwm_period, s_config.peltier_pwm_duty);

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
    ESP_LOGI(TAG, "nvs_config_save called: pwm_period=%u, pwm_duty=%u",
             s_config.peltier_pwm_period, s_config.peltier_pwm_duty);

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return;
    }

    nvs_set_str(handle, NVS_KEY_WIFI_SSID, s_config.wifi_ssid);
    nvs_set_str(handle, NVS_KEY_WIFI_PASS, s_config.wifi_pass);

    ESP_LOGI(TAG, "Saving to NVS: temp_peltier_on=%.1f, temp_peltier_off=%.1f",
             s_config.temp_peltier_on, s_config.temp_peltier_off);

    nvs_set_i32(handle, NVS_KEY_TEMP_ON, (int32_t)(s_config.temp_peltier_on * 100));
    nvs_set_i32(handle, NVS_KEY_TEMP_OFF, (int32_t)(s_config.temp_peltier_off * 100));
    nvs_set_i32(handle, NVS_KEY_TEMP_MAX, (int32_t)(s_config.temp_heatsink_max * 100));
    nvs_set_i32(handle, NVS_KEY_TEMP_TARGET, (int32_t)(s_config.temp_heatsink_target * 100));
    nvs_set_i32(handle, NVS_KEY_ENERGY_WH, (int32_t)(s_config.energy_wh * 100));
    nvs_set_i32(handle, NVS_KEY_ENERGY_DAY, (int32_t)(s_config.energy_day * 100));
    nvs_set_i32(handle, NVS_KEY_ENERGY_WEEK, (int32_t)(s_config.energy_week * 100));
    nvs_set_i32(handle, NVS_KEY_ENERGY_MONTH, (int32_t)(s_config.energy_month * 100));
    nvs_set_u32(handle, NVS_KEY_LAST_DATE, s_config.last_date);

    nvs_set_u16(handle, NVS_KEY_SCHED_MO_ON, s_config.sched_on[0]);
    nvs_set_u16(handle, NVS_KEY_SCHED_MO_OFF, s_config.sched_off[0]);
    nvs_set_u16(handle, NVS_KEY_SCHED_DI_ON, s_config.sched_on[1]);
    nvs_set_u16(handle, NVS_KEY_SCHED_DI_OFF, s_config.sched_off[1]);
    nvs_set_u16(handle, NVS_KEY_SCHED_MI_ON, s_config.sched_on[2]);
    nvs_set_u16(handle, NVS_KEY_SCHED_MI_OFF, s_config.sched_off[2]);
    nvs_set_u16(handle, NVS_KEY_SCHED_DO_ON, s_config.sched_on[3]);
    nvs_set_u16(handle, NVS_KEY_SCHED_DO_OFF, s_config.sched_off[3]);
    nvs_set_u16(handle, NVS_KEY_SCHED_FR_ON, s_config.sched_on[4]);
    nvs_set_u16(handle, NVS_KEY_SCHED_FR_OFF, s_config.sched_off[4]);
    nvs_set_u16(handle, NVS_KEY_SCHED_SA_ON, s_config.sched_on[5]);
    nvs_set_u16(handle, NVS_KEY_SCHED_SA_OFF, s_config.sched_off[5]);
    nvs_set_u16(handle, NVS_KEY_SCHED_SO_ON, s_config.sched_on[6]);
    nvs_set_u16(handle, NVS_KEY_SCHED_SO_OFF, s_config.sched_off[6]);
    nvs_set_u16(handle, NVS_KEY_DATA_LOG_INTERVAL, s_config.data_log_interval);
ESP_LOGI(TAG, "Saving data_log_interval to NVS: %u", s_config.data_log_interval);
    nvs_set_u16(handle, NVS_KEY_PELTIER_PWM_PERIOD, s_config.peltier_pwm_period);
    ESP_LOGI(TAG, "Saving peltier_pwm_period to NVS: %u", s_config.peltier_pwm_period);
    nvs_set_u8(handle, NVS_KEY_PELTIER_PWM_DUTY, s_config.peltier_pwm_duty);
    ESP_LOGI(TAG, "Saving peltier_pwm_duty to NVS: %u", s_config.peltier_pwm_duty);

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

void nvs_config_delete_wifi_credentials(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for deletion");
        return;
    }

    nvs_erase_key(handle, NVS_KEY_WIFI_SSID);
    nvs_erase_key(handle, NVS_KEY_WIFI_PASS);
    nvs_commit(handle);
    nvs_close(handle);

    // Clear local config
    memset(s_config.wifi_ssid, 0, sizeof(s_config.wifi_ssid));
    memset(s_config.wifi_pass, 0, sizeof(s_config.wifi_pass));

    ESP_LOGI(TAG, "WiFi credentials deleted from NVS");
}

void nvs_config_save_energy(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for energy save");
        return;
    }

    nvs_set_i32(handle, NVS_KEY_ENERGY_WH, (int32_t)(s_config.energy_wh * 100));
    nvs_set_i32(handle, NVS_KEY_ENERGY_DAY, (int32_t)(s_config.energy_day * 100));
    nvs_set_i32(handle, NVS_KEY_ENERGY_WEEK, (int32_t)(s_config.energy_week * 100));
    nvs_set_i32(handle, NVS_KEY_ENERGY_MONTH, (int32_t)(s_config.energy_month * 100));
    nvs_set_u32(handle, NVS_KEY_LAST_DATE, s_config.last_date);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Energy data saved to NVS");
}

void nvs_config_factory_reset(void) {
    ESP_LOGI(TAG, "Factory reset: Erasing NVS namespace");
    
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS flash: %s", esp_err_to_name(err));
        return;
    }
    
    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS after erase: %s", esp_err_to_name(err));
        return;
    }
    
    // Defaults neu laden
    load_defaults();
    nvs_config_save();
    
    ESP_LOGI(TAG, "Factory reset completed, defaults restored");
}
