#include "config.h"
#include "nvs_config.h"
#include "wifi.h"
#include "sensor.h"
#include "fan.h"
#include "peltier.h"
#include "webserver.h"
#include "scheduler.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "ota.h"
#include "data_logger.h"

static const char *TAG = "main";

// Reset button monitoring task
void reset_button_task(void *pvParameters) {
    (void)pvParameters;

    // Configure reset button (GPIO0 = BOOT button)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_RESET_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Reset button monitoring started (GPIO %d): Short press = ESP32 reset, Long press = WiFi reset", GPIO_RESET_BUTTON);

    uint64_t press_start_time = 0;
    bool button_pressed = false;

    while (1) {
        int button_level = gpio_get_level(GPIO_RESET_BUTTON);
        uint64_t current_time = esp_timer_get_time() / 1000;  // ms

        if (button_level == 0) {  // Button pressed (active low)
            if (!button_pressed) {
                press_start_time = current_time;
                button_pressed = true;
                ESP_LOGI(TAG, "Reset button pressed");
            } else if (current_time - press_start_time >= RESET_BUTTON_HOLD_MS) {
                // Button held for 3 seconds → reset WiFi
                ESP_LOGW(TAG, "Reset button held for %d ms → resetting WiFi credentials", RESET_BUTTON_HOLD_MS);
                wifi_reset_credentials();

                // Wait for button release
                while (gpio_get_level(GPIO_RESET_BUTTON) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                button_pressed = false;
            }
        } else {
            // Button released
            if (button_pressed) {
                uint64_t press_duration = current_time - press_start_time;
                if (press_duration < RESET_BUTTON_HOLD_MS && press_duration >= 100) {
                    // Short press (100ms to 3s) → ESP32 reset
                    ESP_LOGW(TAG, "Reset button short press (%llu ms) → restarting ESP32", press_duration);
                    esp_restart();
                }
                button_pressed = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32 Peltier Cooler Starting ===");

    // 1. Initialize NVS (must be first — WiFi and config depend on it)
    nvs_config_init();
    ESP_LOGI(TAG, "NVS initialized");

    // 2. Initialize hardware
    sensor_init();
    fan_init();
    peltier_init();
    ESP_LOGI(TAG, "Hardware initialized");

    // 2.1. Enable PWM if configured
    app_config_t *cfg_pwm = nvs_config_get();
    peltier_pwm_enable(true);  // PWM immer aktivieren

    // 2.5. Initialize data logger (ring buffer)
    data_logger_init();

    // Load graph data from NVS (if available)
    data_logger_load_from_nvs();

    // Load configured logging interval from NVS
    data_logger_set_interval(cfg_pwm->data_log_interval * 1000);  // Convert seconds to ms
    ESP_LOGI(TAG, "Data logger initialized, interval: %d seconds", cfg_pwm->data_log_interval);

    // 3. Initialize WiFi (blocks until connected or AP mode active)
    wifi_init();
    ESP_LOGI(TAG, "WiFi initialized (mode: %s)",
             wifi_is_connected() ? "STA" : "AP");

    // 4. Start webserver (monitoring + config)
    webserver_init();

    // 5. Initialize scheduler (SNTP time sync)
    scheduler_init();

    // 6. Create FreeRTOS tasks
    xTaskCreate(task_sensor, "sensor", TASK_STACK_SENSOR,
                NULL, TASK_PRIO_SENSOR, NULL);

    xTaskCreate(task_fan, "fan", TASK_STACK_FAN,
                NULL, TASK_PRIO_FAN, NULL);

    xTaskCreate(task_scheduler, "scheduler", TASK_STACK_SCHEDULER,
                NULL, TASK_PRIO_SCHEDULER, NULL);

    xTaskCreate(task_data_logger, "data_logger", 4096,
                NULL, 3, NULL);

    // 5. Start OTA task
    ota_init();

    // 6. Start reset button monitoring task
    xTaskCreate(reset_button_task, "reset_btn", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "=== System Ready ===");
}
