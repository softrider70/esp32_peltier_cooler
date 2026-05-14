#pragma once

#include <stdint.h>
#include <stdbool.h>

// Status-LED Zustände
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_GREEN,      // Inaktiv
    LED_STATE_RED,        // Aktiv
    LED_STATE_ORANGE,     // Emergency
    LED_STATE_BLUE        // WiFi/AP (optional)
} led_state_t;

// Status-LED Funktionen
void status_led_init(void);
void status_led_set_color(led_state_t state);
void status_led_update(void);
void status_led_off(void);
led_state_t status_led_get_state(void);
