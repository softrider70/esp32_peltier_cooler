#include "peltier.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "peltier";
static bool s_is_on = false;        // Hardware-Zustand (PWM)
static bool s_main_state = false;   // Hauptzustand (Temperatursteuerung)

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

    ESP_LOGI(TAG, "Peltier GPIO %d initialized (digital on/off)", GPIO_PELTIER);
}

void peltier_on(void) {
    gpio_set_level(GPIO_PELTIER, 1);
    s_is_on = true;
}

void peltier_off(void) {
    gpio_set_level(GPIO_PELTIER, 0);
    s_is_on = false;
}

bool peltier_is_on(void) {
    return s_is_on;
}

void peltier_set_main_state(bool state) {
    s_main_state = state;
}

bool peltier_get_main_state(void) {
    return s_main_state;
}
