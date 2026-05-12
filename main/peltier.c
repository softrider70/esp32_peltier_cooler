#include "peltier.h"
#include "config.h"
#include "nvs_config.h"
#include "sensor.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "peltier";
static bool s_is_on = false;        // Hardware-Zustand (GPIO)
static bool s_main_state = false;   // Hauptzustand (Temperatursteuerung)

// PWM Timer
static esp_timer_handle_t s_pwm_timer_on = NULL;   // Timer für GPIO ein
static esp_timer_handle_t s_pwm_timer_off = NULL;  // Timer für GPIO aus
static bool s_pwm_enabled = false;
static uint64_t s_pwm_period_us = 10000000;  // 10s in Mikrosekunden
static uint8_t s_pwm_duty = 10;              // Default 10%

// Auto-Duty State Machine
static esp_timer_handle_t s_autoduty_timer = NULL;
static float s_temp_start = 0.0f;
static uint8_t s_duty_step = 5;              // Basis-Schrittweite 5%
static uint8_t s_equal_temp_counter = 0;     // Zähler für "Temperatur gleich"
static uint8_t s_consecutive_increments = 0; // Aufeinanderfolgende Erhöhungen
static uint8_t s_consecutive_reductions = 0; // Aufeinanderfolgende Reduzierungen

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
    ESP_LOGI(TAG, "PWM ON: period=%llu us, duty=%u%%, on_time=%llu us", s_pwm_period_us, s_pwm_duty, on_time_us);
    esp_timer_start_once(s_pwm_timer_off, on_time_us);

    // Nächsten Zyklus starten
    esp_timer_start_once(s_pwm_timer_on, s_pwm_period_us);
}

// PWM Timer Callback: GPIO aus
static void IRAM_ATTR pwm_off_callback(void* arg) {
    gpio_set_level(GPIO_PELTIER, 0);
    s_is_on = false;
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

    ESP_LOGI(TAG, "Peltier GPIO %d initialized with PWM support", GPIO_PELTIER);
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
    ESP_LOGI(TAG, "PWM duty set to %u%%", duty);
}

uint8_t peltier_get_duty(void) {
    return s_pwm_duty;
}

bool peltier_pwm_is_enabled(void) {
    return s_pwm_enabled;
}

// ===== Auto-Duty Regelung =====

// Auto-Duty Timer Callback
static void autoduty_callback(void* arg) {
    if (!s_pwm_enabled || !nvs_config_get()->peltier_pwm_auto) {
        return;
    }

    // Aktuelle Innentemperatur holen
    sensor_data_t sensor_data = sensor_get_data();
    float temp_current = sensor_data.temp_indoor;

    if (s_temp_start == 0.0f) {
        s_temp_start = temp_current;
        return;
    }

    float temp_diff = temp_current - s_temp_start;

    // Toleranz 0.1°C
    if (temp_diff < -0.1f) {
        // Temperatur sinkt → Duty reduzieren
        if (s_pwm_duty > 5) {
            s_pwm_duty -= s_duty_step;
            s_consecutive_reductions++;
            s_consecutive_increments = 0;
            s_equal_temp_counter = 0;
            s_duty_step = 5;  // Zurück zu Basis
            ESP_LOGI(TAG, "Auto-Duty: Temp sinks (%.2f -> %.2f), duty reduced to %u%%", s_temp_start, temp_current, s_pwm_duty);
            // Duty in NVS-Config aktualisieren
            app_config_t *cfg = nvs_config_get();
            cfg->peltier_pwm_duty = s_pwm_duty;
        }
    } else if (temp_diff > 0.1f) {
        // Temperatur steigt → Duty erhöhen
        if (s_pwm_duty < 20) {
            s_pwm_duty += s_duty_step;
            s_consecutive_increments++;
            s_consecutive_reductions = 0;
            s_equal_temp_counter = 0;
            ESP_LOGI(TAG, "Auto-Duty: Temp rises (%.2f -> %.2f), duty increased to %u%%", s_temp_start, temp_current, s_pwm_duty);
            // Duty in NVS-Config aktualisieren
            app_config_t *cfg = nvs_config_get();
            cfg->peltier_pwm_duty = s_pwm_duty;
        }
    } else {
        // Temperatur gleich
        s_equal_temp_counter++;
        if (s_equal_temp_counter >= 2) {
            // 2x gleich → Duty erhöhen
            if (s_pwm_duty < 20) {
                s_pwm_duty += s_duty_step;
                s_consecutive_increments++;
                s_equal_temp_counter = 0;
                ESP_LOGI(TAG, "Auto-Duty: Temp stable 2x, duty increased to %u%%", s_pwm_duty);
                // Duty in NVS-Config aktualisieren
                app_config_t *cfg = nvs_config_get();
                cfg->peltier_pwm_duty = s_pwm_duty;
            }
        } else {
            // Stabil → Schrittweite zurück zu Basis
            s_duty_step = 5;
            s_consecutive_increments = 0;
            s_consecutive_reductions = 0;
        }
    }

    // Adaptive Schrittweite
    if (s_consecutive_increments >= 2) s_duty_step = 7;
    if (s_consecutive_increments >= 4) s_duty_step = 10;
    if (s_consecutive_reductions >= 2) s_duty_step = 7;
    if (s_consecutive_reductions >= 4) s_duty_step = 10;

    // Neue Start-Temperatur
    s_temp_start = temp_current;
}

// Auto-Duty starten
void peltier_autoduty_start(void) {
    if (s_autoduty_timer == NULL) {
        esp_timer_create_args_t timer_args = {
            .callback = &autoduty_callback,
            .name = "autoduty"
        };
        esp_timer_create(&timer_args, &s_autoduty_timer);
    }

    uint32_t interval_us = nvs_config_get()->peltier_pwm_interval * 1000000;
    esp_timer_start_periodic(s_autoduty_timer, interval_us);
    ESP_LOGI(TAG, "Auto-Duty started with interval %u seconds", nvs_config_get()->peltier_pwm_interval);
}

// Auto-Duty stoppen
void peltier_autoduty_stop(void) {
    if (s_autoduty_timer != NULL) {
        esp_timer_stop(s_autoduty_timer);
        ESP_LOGI(TAG, "Auto-Duty stopped");
    }
}
