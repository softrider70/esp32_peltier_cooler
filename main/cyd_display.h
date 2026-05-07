#pragma once

#include <stdint.h>
#include <stdbool.h>

// CYD 3.5-inch Display GPIO Konfiguration
#define CYD_TFT_CS    15  // Chip Select
#define CYD_TFT_SCK   14  // SPI Clock
#define CYD_TFT_MOSI  13  // MOSI (Data)
#define CYD_TFT_MISO  12  // MISO
#define CYD_TFT_DC    2   // Data/Command
#define CYD_TFT_BL    27  // Backlight PWM

// Touch (GT911) Pins
#define CYD_TOUCH_SDA 33  // I2C Data
#define CYD_TOUCH_SCL 32  // I2C Clock
#define CYD_TOUCH_INT 36  // Interrupt Pin

// Display Konfiguration
#define CYD_DISPLAY_WIDTH   320
#define CYD_DISPLAY_HEIGHT  480
#define CYD_TOUCH_I2C_ADDR  0x5D

// CYD Cooler Display States
typedef enum {
    CYD_SCREEN_MAIN,        // Hauptanzeige mit Temperaturen
    CYD_SCREEN_CONFIG,      // Konfigurationsmenü
    CYD_SCREEN_PID_TUNE,    // PID-Tuning Interface
    CYD_SCREEN_WIFI_SETUP,  // WiFi Konfiguration
    CYD_SCREEN_SCHEDULE     // Zeitplan-Einstellungen
} cyd_screen_t;

// Touch Button Struktur
typedef struct {
    uint16_t x, y, w, h;
    const char* label;
    bool pressed;
    uint32_t action_id;
} cyd_button_t;

// Display Datenstruktur
typedef struct {
    float temp_indoor;
    float temp_heatsink;
    uint8_t fan_duty;
    bool peltier_on;
    bool scheduler_active;
    bool wifi_connected;
} cyd_display_data_t;

// CYD Display Funktionen
void cyd_display_init(void);
void cyd_display_update(cyd_display_data_t* data);
void cyd_set_screen(cyd_screen_t screen);
bool cyd_process_touch(uint16_t x, uint16_t y);
void cyd_set_brightness(uint8_t brightness);

// Touch und Button Funktionen
void cyd_draw_button(cyd_button_t* btn, bool pressed);
bool cyd_is_touch_inside(uint16_t x, uint16_t y, cyd_button_t* btn);

// Externe Variablen für Touch-Events
extern cyd_button_t cyd_buttons[];
extern const int CYD_NUM_BUTTONS;
