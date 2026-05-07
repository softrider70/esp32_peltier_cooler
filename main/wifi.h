#pragma once

#include <stdbool.h>

// WiFi operation mode
typedef enum {
    WIFI_MODE_STA,      // Station mode (connected to router)
    WIFI_MODE_AP,       // AP mode (captive portal)
} wifi_op_mode_t;

// Initialize WiFi subsystem and attempt STA connection
// Falls back to AP mode if credentials missing or connection fails
void wifi_init(void);

// Get current WiFi mode
wifi_op_mode_t wifi_get_mode(void);

// Check if connected to router (STA mode)
bool wifi_is_connected(void);

// Force switch to AP mode (for config)
void wifi_start_ap(void);

// Force reconnect STA with current credentials
void wifi_reconnect_sta(void);
