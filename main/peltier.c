#include "peltier.h"
#include "sensor.h"
#include "nvs_config.h"
#include "config.h"
#include "esp_log.h"
#include <math.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"

static const char *TAG = "peltier";
static bool s_is_on = false;        // Hardware-Zustand (GPIO)
static bool s_main_state = false;   // Hauptzustand (Temperatursteuerung)

// PWM Timer
static esp_timer_handle_t s_pwm_timer_on = NULL;   // Timer für GPIO ein
static esp_timer_handle_t s_pwm_timer_off = NULL;  // Timer für GPIO aus
static bool s_pwm_enabled = false;
static uint64_t s_pwm_period_us = 0;  // Wird aus Config geladen
static uint8_t s_pwm_duty = 0;        // Wird aus Config geladen

// PWM Timer Callback: GPIO ein
static void IRAM_ATTR pwm_on_callback(void* arg) {
    if (!s_main_state || !s_pwm_enabled) {
        gpio_set_level(GPIO_PELTIER, 0);
        s_is_on = false;
        return;
    }

    gpio_set_level(GPIO_PELTIER, 1);
    s_is_on = true;

    // Konfiguration neu laden (für Auto-Duty Updates)
    app_config_t *cfg = nvs_config_get();
    s_pwm_period_us = cfg->peltier_pwm_period * 1000000;
    s_pwm_duty = cfg->peltier_pwm_duty;

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
    ESP_LOGI(TAG, "PWM duty set to %u%%", duty);
}

uint8_t peltier_get_duty(void) {
    return s_pwm_duty;
}

bool peltier_pwm_is_enabled(void) {
    return s_pwm_enabled;
}
