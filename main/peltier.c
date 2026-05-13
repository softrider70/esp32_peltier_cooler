#include "peltier.h"
#include "sensor.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "peltier";
static bool s_is_on = false;        // Hardware-Zustand (GPIO)
static bool s_main_state = false;   // Hauptzustand (Temperatursteuerung)

// PWM Timer
static esp_timer_handle_t s_pwm_timer_on = NULL;   // Timer für GPIO ein
static esp_timer_handle_t s_pwm_timer_off = NULL;  // Timer für GPIO aus
static bool s_pwm_enabled = false;
static uint64_t s_pwm_period_us = 0;  // Wird aus Config geladen
static uint8_t s_pwm_duty = 0;        // Wird aus Config geladen

// Auto-Duty Variablen
static bool s_autoduty_enabled = false;
static esp_timer_handle_t s_autoduty_timer = NULL;
static float s_autoduty_temp_ref = 0.0f;  // Temperatur-Referenz am Zyklusstart
static uint8_t s_autoduty_duty = 0;       // Aktueller Duty-Wert (0-100%)
static uint8_t s_autoduty_constant_counter = 0;  // Zähler für konstante Temperatur (2 Zyklen)
static uint64_t s_autoduty_cycle_us = 0;  // Zyklusdauer in Mikrosekunden
static int64_t s_autoduty_last_callback_us = 0;  // Zeitpunkt des letzten Callbacks
static uint32_t s_autoduty_callback_count = 0;  // Callback-Zähler für Test

// PWM Timer Callback: GPIO ein
static void IRAM_ATTR pwm_on_callback(void* arg) {
    if (!s_main_state || !s_pwm_enabled) {
        gpio_set_level(GPIO_PELTIER, 0);
        s_is_on = false;
        return;
    }

    gpio_set_level(GPIO_PELTIER, 1);
    s_is_on = true;

    // Timer für GPIO aus starten
    uint64_t on_time_us = (s_pwm_period_us * s_pwm_duty) / 100;
    esp_timer_start_once(s_pwm_timer_off, on_time_us);

    // Nächsten Zyklus starten
    esp_timer_start_once(s_pwm_timer_on, s_pwm_period_us);
}

// PWM Timer Callback: GPIO aus
static void IRAM_ATTR pwm_off_callback(void* arg) {
    gpio_set_level(GPIO_PELTIER, 0);
    s_is_on = false;
}

// Auto-Duty Timer Callback
static void autoduty_callback(void* arg) {
    s_autoduty_callback_count++;
    
    if (!s_autoduty_enabled) {
        return;
    }

    // Zeitpunkt des Callbacks speichern
    s_autoduty_last_callback_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Auto-Duty callback: duty=%u, temp_ref=%.1f", s_autoduty_duty, s_autoduty_temp_ref);

    sensor_data_t sd = sensor_get_data();
    if (!sd.indoor_valid) {
        ESP_LOGW(TAG, "Auto-Duty: Indoor sensor invalid, skipping");
        return;
    }

    float temp_diff = sd.temp_indoor - s_autoduty_temp_ref;

    ESP_LOGI(TAG, "Auto-Duty: temp_indoor=%.1f, temp_diff=%.2f", sd.temp_indoor, temp_diff);

    // Regellogik
    if (temp_diff < 0) {
        // Temperatur sinkt → duty - 2
        s_autoduty_duty -= 2;
        s_autoduty_constant_counter = 0;
        ESP_LOGI(TAG, "Auto-Duty: Temp falling, duty decreased to %u", s_autoduty_duty);
    } else {
        // Temperatur steigt oder gleich → duty + 4
        s_autoduty_constant_counter++;
        if (s_autoduty_constant_counter >= 2) {
            s_autoduty_duty += 4;
            s_autoduty_constant_counter = 0;
            ESP_LOGI(TAG, "Auto-Duty: Temp rising/constant, duty increased to %u", s_autoduty_duty);
        }
    }

    // Grenzen prüfen (duty: 0-100%)
    if (s_autoduty_duty > 100) s_autoduty_duty = 100;
    if (s_autoduty_duty < 1) s_autoduty_duty = 1;  // Untere Grenze hinzugefügt

    // Duty anwenden
    peltier_set_duty(s_autoduty_duty);

    // Neue Referenz setzen
    s_autoduty_temp_ref = sd.temp_indoor;
}

void peltier_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_PELTIER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(GPIO_PELTIER, 0);
    s_is_on = false;

    // PWM Timer erstellen
    esp_timer_create_args_t on_timer_args = {
        .callback = &pwm_on_callback,
        .name = "pwm_on"
    };
    esp_timer_create_args_t off_timer_args = {
        .callback = &pwm_off_callback,
        .name = "pwm_off"
    };
    esp_timer_create(&on_timer_args, &s_pwm_timer_on);
    esp_timer_create(&off_timer_args, &s_pwm_timer_off);

    // Auto-Duty Timer erstellen
    esp_timer_create_args_t autoduty_timer_args = {
        .callback = &autoduty_callback,
        .name = "autoduty"
    };
    esp_timer_create(&autoduty_timer_args, &s_autoduty_timer);

    // PWM-Werte aus Config laden
    app_config_t *cfg = nvs_config_get();
    s_pwm_period_us = cfg->peltier_pwm_period * 1000000;
    s_pwm_duty = cfg->peltier_pwm_duty;
    ESP_LOGI(TAG, "Peltier GPIO %d initialized with PWM support (period=%llu us, duty=%u%%)", GPIO_PELTIER, s_pwm_period_us, s_pwm_duty);
}

void peltier_on(void) {
    if (s_pwm_enabled) {
        // PWM aktiv → über Hauptzustand steuern
        return;
    }
    gpio_set_level(GPIO_PELTIER, 1);
    s_is_on = true;
}

void peltier_off(void) {
    if (s_pwm_enabled) {
        // PWM aktiv → über Hauptzustand steuern
        return;
    }
    gpio_set_level(GPIO_PELTIER, 0);
    s_is_on = false;
}

bool peltier_is_on(void) {
    return s_is_on;
}

void peltier_set_main_state(bool state) {
    bool old_state = s_main_state;
    s_main_state = state;

    if (s_pwm_enabled) {
        if (state && !old_state) {
            // Hauptzustand wechselt von aus auf an → PWM starten
            esp_timer_start_once(s_pwm_timer_on, 0);  // Sofort starten
            ESP_LOGI(TAG, "Main state ON, PWM started");
        } else if (!state && old_state) {
            // Hauptzustand wechselt von an auf aus → PWM stoppen
            esp_timer_stop(s_pwm_timer_on);
            esp_timer_stop(s_pwm_timer_off);
            gpio_set_level(GPIO_PELTIER, 0);
            s_is_on = false;
            ESP_LOGI(TAG, "Main state OFF, PWM stopped");
        }
    }
}

bool peltier_get_main_state(void) {
    return s_main_state;
}

// ===== PWM Steuerung =====

void peltier_pwm_enable(bool enable) {
    s_pwm_enabled = enable;
    if (enable) {
        // Konfiguration aus NVS laden
        app_config_t *cfg = nvs_config_get();
        s_pwm_period_us = cfg->peltier_pwm_period * 1000000;  // Sekunden zu Mikrosekunden
        s_pwm_duty = cfg->peltier_pwm_duty;
        ESP_LOGI(TAG, "PWM enabled: period=%llu us, duty=%u%%", s_pwm_period_us, s_pwm_duty);
    } else {
        // PWM stoppen
        esp_timer_stop(s_pwm_timer_on);
        esp_timer_stop(s_pwm_timer_off);
        gpio_set_level(GPIO_PELTIER, 0);
        s_is_on = false;
        ESP_LOGI(TAG, "PWM disabled");
    }
}

void peltier_set_duty(uint8_t duty) {
    if (duty < 1) duty = 1;
    if (duty > 100) duty = 100;
    s_pwm_duty = duty;
    ESP_LOGI(TAG, "PWM duty set to %u%% (pwm_enabled=%d, main_state=%d)", duty, s_pwm_enabled, s_main_state);
}

uint8_t peltier_get_duty(void) {
    return s_pwm_duty;
}

bool peltier_pwm_is_enabled(void) {
    return s_pwm_enabled;
}

// ===== Auto-Duty Regelung =====

void peltier_autoduty_start(void) {
    // Prüfen, ob Auto-Duty bereits läuft
    if (s_autoduty_enabled) {
        ESP_LOGW(TAG, "Auto-Duty already enabled, skipping start");
        return;
    }
    
    sensor_data_t sd = sensor_get_data();
    
    s_autoduty_enabled = true;
    app_config_t *cfg = nvs_config_get();
    // Aktuellen PWM Duty übernehmen, nicht den gespeicherten AD Duty
    s_autoduty_duty = peltier_get_duty();
    s_autoduty_cycle_us = cfg->auto_duty_cycle * 1000000;  // Sekunden zu Mikrosekunden
    s_autoduty_constant_counter = 0;
    
    if (sd.indoor_valid) {
        s_autoduty_temp_ref = sd.temp_indoor;
    } else {
        s_autoduty_temp_ref = 20.0f;  // Fallback-Temperatur wenn Sensor invalid
    }
    s_autoduty_last_callback_us = esp_timer_get_time();  // Initialer Zeitpunkt
    s_autoduty_callback_count = 0;  // Zähler zurücksetzen

    peltier_set_duty(s_autoduty_duty);
    esp_timer_start_periodic(s_autoduty_timer, s_autoduty_cycle_us);

    ESP_LOGI(TAG, "Auto-Duty started: duty=%u%%, cycle=%llu us, temp_ref=%.1f", s_autoduty_duty, s_autoduty_cycle_us, s_autoduty_temp_ref);
}

void peltier_autoduty_start_with_temp(float temp_indoor) {
    // Prüfen, ob Auto-Duty bereits läuft
    if (s_autoduty_enabled) {
        ESP_LOGW(TAG, "Auto-Duty already enabled, skipping start");
        return;
    }
    
    s_autoduty_enabled = true;
    app_config_t *cfg = nvs_config_get();
    // Aktuellen PWM Duty übernehmen, nicht den gespeicherten AD Duty
    s_autoduty_duty = peltier_get_duty();
    s_autoduty_cycle_us = cfg->auto_duty_cycle * 1000000;  // Sekunden zu Mikrosekunden
    s_autoduty_constant_counter = 0;
    s_autoduty_temp_ref = temp_indoor;
    s_autoduty_last_callback_us = esp_timer_get_time();  // Initialer Zeitpunkt
    s_autoduty_callback_count = 0;  // Zähler zurücksetzen

    peltier_set_duty(s_autoduty_duty);
    
    esp_err_t ret = esp_timer_start_periodic(s_autoduty_timer, s_autoduty_cycle_us);
    ESP_LOGI(TAG, "Auto-Duty timer start result: %d (0=success)", ret);

    ESP_LOGI(TAG, "Auto-Duty started with temp: duty=%u%%, cycle=%llu us, temp_ref=%.1f", s_autoduty_duty, s_autoduty_cycle_us, s_autoduty_temp_ref);
}

void peltier_autoduty_stop(void) {
    s_autoduty_enabled = false;
    s_autoduty_last_callback_us = 0;  // Zeitpunkt zurücksetzen
    esp_timer_stop(s_autoduty_timer);
    ESP_LOGI(TAG, "Auto-Duty stopped");
}

void peltier_autoduty_update_cycle(uint16_t cycle_seconds) {
    s_autoduty_cycle_us = cycle_seconds * 1000000;  // Sekunden zu Mikrosekunden
    
    // Timer neu starten, falls Auto-Duty aktiv ist
    if (s_autoduty_enabled) {
        esp_timer_stop(s_autoduty_timer);
        esp_timer_start_periodic(s_autoduty_timer, s_autoduty_cycle_us);
        ESP_LOGI(TAG, "Auto-Duty cycle updated to %llu us", s_autoduty_cycle_us);
    }
}

bool peltier_autoduty_is_enabled(void) {
    return s_autoduty_enabled;
}

uint8_t peltier_get_autoduty_duty(void) {
    return s_autoduty_duty;
}

uint16_t peltier_get_autoduty_cycle(void) {
    return (uint16_t)(s_autoduty_cycle_us / 1000000);  // Mikrosekunden zu Sekunden
}

uint16_t peltier_get_autoduty_countdown(void) {
    if (!s_autoduty_enabled) {
        return 0;
    }

    // Verbleibende Zeit basierend auf letztem Callback berechnen
    if (s_autoduty_last_callback_us == 0) {
        return (uint16_t)(s_autoduty_cycle_us / 1000000);  // Fallback: Zykluszeit
    }

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - s_autoduty_last_callback_us;
    int64_t remaining_us = s_autoduty_cycle_us - elapsed_us;

    if (remaining_us < 0) remaining_us = 0;
    return (uint16_t)(remaining_us / 1000000);  // Mikrosekunden zu Sekunden
}
