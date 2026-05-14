#include "status_led.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "scheduler.h"
#include "sensor.h"

static const char *TAG = "status_led";

static led_state_t s_current_state = LED_STATE_OFF;

// LED GPIO initialisieren
void status_led_init(void) {
    // GPIO konfigurieren
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_STATUS_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // LED initial AUS
    gpio_set_level(GPIO_STATUS_LED, 0);
    s_current_state = LED_STATE_OFF;
    
    ESP_LOGI(TAG, "Status LED initialized on GPIO %d", GPIO_STATUS_LED);
}

// LED-Farbe setzen (für einfache LED: 0=AUS, 1=AN)
void status_led_set_color(led_state_t state) {
    if (s_current_state == state) {
        return; // Keine Änderung nötig
    }

    switch (state) {
        case LED_STATE_OFF:
            gpio_set_level(GPIO_STATUS_LED, 0);
            ESP_LOGD(TAG, "LED OFF");
            break;
            
        case LED_STATE_GREEN:
            gpio_set_level(GPIO_STATUS_LED, 1);  // Grün für Inaktiv
            ESP_LOGD(TAG, "LED GREEN (Inactive)");
            break;
            
        case LED_STATE_RED:
            gpio_set_level(GPIO_STATUS_LED, 1);  // Rot für Aktiv
            ESP_LOGD(TAG, "LED RED (Active)");
            break;
            
        case LED_STATE_ORANGE:
            gpio_set_level(GPIO_STATUS_LED, 1);  // Orange für Emergency
            ESP_LOGD(TAG, "LED ORANGE (Emergency)");
            break;
            
        case LED_STATE_BLUE:
            gpio_set_level(GPIO_STATUS_LED, 1);  // Blau für WiFi/AP
            ESP_LOGD(TAG, "LED BLUE (WiFi/AP)");
            break;
            
        default:
            gpio_set_level(GPIO_STATUS_LED, 0);
            ESP_LOGW(TAG, "Unknown LED state: %d", state);
            break;
    }
    
    s_current_state = state;
}

// Status-LED basierend auf Systemzustand aktualisieren
void status_led_update(void) {
    // Emergency Mode hat höchste Priorität
    if (sensor_get_emergency_mode()) {
        status_led_set_color(LED_STATE_ORANGE);
        return;
    }
    
    // System aktiv/inaktiv prüfen
    bool active = scheduler_is_active();
    if (active) {
        status_led_set_color(LED_STATE_RED);    // Aktiv = Rot
    } else {
        status_led_set_color(LED_STATE_GREEN);  // Inaktiv = Grün
    }
}

// LED ausschalten
void status_led_off(void) {
    status_led_set_color(LED_STATE_OFF);
}

// Aktuellen Status abfragen
led_state_t status_led_get_state(void) {
    return s_current_state;
}
