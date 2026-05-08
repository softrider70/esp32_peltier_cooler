#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "config.h"
#include "nvs_config.h"
#include "sensor.h"
#include "fan.h"
#include "peltier.h"
#include "scheduler.h"
#include "wifi.h"
#include "webserver.h"
#include "ota.h"

static const char *TAG = "main";

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

    xTaskCreate(task_fan_pid, "fan_pid", TASK_STACK_PID,
                NULL, TASK_PRIO_PID, NULL);

    xTaskCreate(task_scheduler, "scheduler", TASK_STACK_SCHEDULER,
                NULL, TASK_PRIO_SCHEDULER, NULL);

    // 7. OTA: validate firmware after stable boot (all tasks running)
    //    If this is first boot after OTA, mark as valid → cancels rollback
    //    If firmware crashes before this point, bootloader auto-rolls back
    vTaskDelay(pdMS_TO_TICKS(5000));
    ota_init();

    ESP_LOGI(TAG, "=== All tasks started ===");
}
