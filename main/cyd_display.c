#include "cyd_display.h"
#include "sensor.h"
#include "fan.h"
#include "peltier.h"
#include "scheduler.h"
#include "wifi.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"

static const char *TAG = "cyd_display";

// Aktuelle Screen und Daten
static cyd_screen_t s_current_screen = CYD_SCREEN_MAIN;
static cyd_display_data_t s_display_data = {0};
static bool s_display_initialized = false;

// Button Definitionen für Hauptscreen
cyd_button_t cyd_buttons[] = {
    // x, y, w, h, label, pressed, action_id
    {10,  400, 60, 40, "MENU", false, 1},      // Hauptmenü
    {80,  400, 60, 40, "PID",  false, 2},      // PID-Tuning
    {150, 400, 60, 40, "WIFI", false, 3},      // WiFi Setup
    {220, 400, 60, 40, "TIME", false, 4},      // Zeitplan
    {290, 400, 60, 40, "EXIT", false, 5},      // Zurück
};

const int CYD_NUM_BUTTONS = sizeof(cyd_buttons) / sizeof(cyd_buttons[0]);

// Display Initialisierung (vereinfacht für ESP-IDF)
void cyd_display_init(void) {
    ESP_LOGI(TAG, "Initializing CYD 3.5\" Display");
    
    // GPIO Konfiguration für Display Pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CYD_TFT_CS) | (1ULL << CYD_TFT_DC) | (1ULL << CYD_TFT_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // Initialzustand setzen
    gpio_set_level(CYD_TFT_CS, 1);  // Chip Select inaktiv
    gpio_set_level(CYD_TFT_DC, 0);  // Command Mode
    gpio_set_level(CYD_TFT_BL, 1);  // Backlight an
    
    // I2C für Touch Controller initialisieren
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CYD_TOUCH_SDA,
        .scl_io_num = CYD_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    i2c_driver_install(I2C_NUM_0, i2c_conf.mode, 0, 0, 0);
    
    s_display_initialized = true;
    ESP_LOGI(TAG, "CYD Display initialized");
    
    // Start-Animation anzeigen
    cyd_set_screen(CYD_SCREEN_MAIN);
}

// Display Daten aktualisieren
void cyd_display_update(cyd_display_data_t* data) {
    if (!s_display_initialized) return;
    
    // Daten kopieren
    memcpy(&s_display_data, data, sizeof(cyd_display_data_t));
    
    // Abhängig vom aktuellen Screen zeichnen
    switch (s_current_screen) {
        case CYD_SCREEN_MAIN:
            // Hauptanzeige mit Temperaturen und Status
            cyd_draw_main_screen();
            break;
        case CYD_SCREEN_CONFIG:
            cyd_draw_config_screen();
            break;
        case CYD_SCREEN_PID_TUNE:
            cyd_draw_pid_screen();
            break;
        case CYD_SCREEN_WIFI_SETUP:
            cyd_draw_wifi_screen();
            break;
        case CYD_SCREEN_SCHEDULE:
            cyd_draw_schedule_screen();
            break;
    }
}

// Hauptscreen zeichnen
static void cyd_draw_main_screen(void) {
    // Hier würde die eigentliche Display-Ansteuerung erfolgen
    // Für den Moment nur Logging
    
    ESP_LOGI(TAG, "=== CYD Main Screen ===");
    ESP_LOGI(TAG, "Indoor: %.1f°C", s_display_data.temp_indoor);
    ESP_LOGI(TAG, "Heatsink: %.1f°C", s_display_data.temp_heatsink);
    ESP_LOGI(TAG, "Fan: %d%%", s_display_data.fan_duty);
    ESP_LOGI(TAG, "Peltier: %s", s_display_data.peltier_on ? "ON" : "OFF");
    ESP_LOGI(TAG, "Scheduler: %s", s_display_data.scheduler_active ? "ACTIVE" : "INACTIVE");
    ESP_LOGI(TAG, "WiFi: %s", s_display_data.wifi_connected ? "CONNECTED" : "DISCONNECTED");
    
    // Buttons zeichnen
    for (int i = 0; i < CYD_NUM_BUTTONS; i++) {
        cyd_draw_button(&cyd_buttons[i], cyd_buttons[i].pressed);
    }
}

// Config Screen zeichnen
static void cyd_draw_config_screen(void) {
    ESP_LOGI(TAG, "=== CYD Config Screen ===");
    app_config_t* cfg = nvs_config_get();
    
    ESP_LOGI(TAG, "Peltier ON: %.1f°C", cfg->temp_peltier_on);
    ESP_LOGI(TAG, "Peltier OFF: %.1f°C", cfg->temp_peltier_off);
    ESP_LOGI(TAG, "Max Temp: %.1f°C", cfg->temp_heatsink_max);
    ESP_LOGI(TAG, "Target Temp: %.1f°C", cfg->temp_heatsink_target);
}

// PID Screen zeichnen
static void cyd_draw_pid_screen(void) {
    ESP_LOGI(TAG, "=== CYD PID Screen ===");
    app_config_t* cfg = nvs_config_get();
    
    ESP_LOGI(TAG, "Kp: %.2f", cfg->pid_kp);
    ESP_LOGI(TAG, "Ki: %.2f", cfg->pid_ki);
    ESP_LOGI(TAG, "Kd: %.2f", cfg->pid_kd);
}

// WiFi Screen zeichnen
static void cyd_draw_wifi_screen(void) {
    ESP_LOGI(TAG, "=== CYD WiFi Screen ===");
    app_config_t* cfg = nvs_config_get();
    
    ESP_LOGI(TAG, "SSID: %s", cfg->wifi_ssid);
    ESP_LOGI(TAG, "Status: %s", wifi_is_connected() ? "Connected" : "Disconnected");
}

// Schedule Screen zeichnen
static void cyd_draw_schedule_screen(void) {
    ESP_LOGI(TAG, "=== CYD Schedule Screen ===");
    app_config_t* cfg = nvs_config_get();
    
    ESP_LOGI(TAG, "Weekday: %02d:%02d - %02d:%02d", 
             cfg->sched_wd_on / 60, cfg->sched_wd_on % 60,
             cfg->sched_wd_off / 60, cfg->sched_wd_off % 60);
    ESP_LOGI(TAG, "Weekend: %02d:%02d - %02d:%02d",
             cfg->sched_we_on / 60, cfg->sched_we_on % 60, 
             cfg->sched_we_off / 60, cfg->sched_we_off % 60);
}

// Screen wechseln
void cyd_set_screen(cyd_screen_t screen) {
    s_current_screen = screen;
    ESP_LOGI(TAG, "Switched to screen: %d", screen);
    
    // Screen neu zeichnen
    cyd_display_update(&s_display_data);
}

// Touch-Eingabe verarbeiten
bool cyd_process_touch(uint16_t x, uint16_t y) {
    bool button_hit = false;
    
    // Alle Buttons prüfen
    for (int i = 0; i < CYD_NUM_BUTTONS; i++) {
        bool inside = cyd_is_touch_inside(x, y, &cyd_buttons[i]);
        
        if (inside && !cyd_buttons[i].pressed) {
            // Button gedrückt
            cyd_buttons[i].pressed = true;
            cyd_draw_button(&cyd_buttons[i], true);
            
            ESP_LOGI(TAG, "Button '%s' pressed (Action: %lu)", 
                     cyd_buttons[i].label, cyd_buttons[i].action_id);
            
            // Aktion ausführen
            switch (cyd_buttons[i].action_id) {
                case 1: // MENU
                    cyd_set_screen(CYD_SCREEN_CONFIG);
                    break;
                case 2: // PID
                    cyd_set_screen(CYD_SCREEN_PID_TUNE);
                    break;
                case 3: // WIFI
                    cyd_set_screen(CYD_SCREEN_WIFI_SETUP);
                    break;
                case 4: // TIME
                    cyd_set_screen(CYD_SCREEN_SCHEDULE);
                    break;
                case 5: // EXIT
                    cyd_set_screen(CYD_SCREEN_MAIN);
                    break;
            }
            
            button_hit = true;
        } else if (!inside && cyd_buttons[i].pressed) {
            // Button losgelassen
            cyd_buttons[i].pressed = false;
            cyd_draw_button(&cyd_buttons[i], false);
            
            ESP_LOGI(TAG, "Button '%s' released", cyd_buttons[i].label);
        }
    }
    
    return button_hit;
}

// Button zeichnen (vereinfacht)
void cyd_draw_button(cyd_button_t* btn, bool pressed) {
    ESP_LOGD(TAG, "Drawing button '%s' at (%d,%d) %dx%d - %s",
             btn->label, btn->x, btn->y, btn->w, btn->h,
             pressed ? "PRESSED" : "NORMAL");
}

// Touch-Bounds prüfen
bool cyd_is_touch_inside(uint16_t x, uint16_t y, cyd_button_t* btn) {
    return (x >= btn->x && x <= (btn->x + btn->w) &&
            y >= btn->y && y <= (btn->y + btn->h));
}

// Helligkeit einstellen
void cyd_set_brightness(uint8_t brightness) {
    // PWM für Backlight anpassen
    gpio_set_level(CYD_TFT_BL, brightness > 0 ? 1 : 0);
    ESP_LOGI(TAG, "Brightness set to %d", brightness);
}

// CYD Display Task (regelmäßige Updates)
void cyd_display_task(void *pvParameters) {
    (void)pvParameters;
    
    while (1) {
        if (s_display_initialized) {
            // Sensor-Daten holen
            sensor_data_t sensors = sensor_get_data();
            
            // Display-Daten strukturieren
            cyd_display_data_t data = {
                .temp_indoor = sensors.temp_indoor,
                .temp_heatsink = sensors.temp_heatsink,
                .fan_duty = fan_get_duty(),
                .peltier_on = peltier_is_on(),
                .scheduler_active = scheduler_is_active(),
                .wifi_connected = wifi_is_connected()
            };
            
            // Display aktualisieren
            cyd_display_update(&data);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Jede Sekunde aktualisieren
    }
}
