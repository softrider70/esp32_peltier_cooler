#include "data_logger.h"
#include "sensor.h"
#include "fan.h"
#include "peltier.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "data_logger";

// Ring buffer in RAM
static data_point_t s_ring_buffer[DATA_POINTS_MAX];
static volatile uint16_t s_ring_index = 0;  // Current write position
static volatile bool s_initialized = false;
static volatile uint32_t s_log_interval_ms = DATA_LOGGER_INTERVAL_MS;  // Configurable interval

void data_logger_init(void) {
    // Clear ring buffer
    memset(s_ring_buffer, 0, sizeof(s_ring_buffer));
    s_ring_index = 0;
    s_initialized = true;
    
    ESP_LOGI(TAG, "Data logger initialized: %d points, %d KB RAM", 
             DATA_POINTS_MAX, 
             (sizeof(data_point_t) * DATA_POINTS_MAX) / 1024);
}

void task_data_logger(void *pvParameters) {
    (void)pvParameters;
    
    ESP_LOGI(TAG, "Data logger task started");
    
    while (1) {
        if (!s_initialized) {
            vTaskDelay(pdMS_TO_TICKS(DATA_LOGGER_INTERVAL_MS));
            continue;
        }
        
        // Get current sensor data
        sensor_data_t sd = sensor_get_data();
        
        // Get current fan and peltier status
        uint8_t fan_duty = fan_get_duty();
        bool peltier_on = peltier_is_on();
        
        // Use relative time (seconds since boot) instead of Unix timestamp
        uint32_t relative_time = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        
        // Create data point
        data_point_t point = {
            .timestamp = relative_time,  // Relative time in seconds since boot
            .temp_indoor = sd.indoor_valid ? sd.temp_indoor : 0.0f,
            .temp_heatsink = sd.heatsink_valid ? sd.temp_heatsink : 0.0f,
            .fan_duty = fan_duty,
            .peltier_on = peltier_on
        };
        
        // Write to ring buffer
        s_ring_buffer[s_ring_index] = point;
        
        // Increment index with wrap-around
        s_ring_index = (s_ring_index + 1) % DATA_POINTS_MAX;
        
        ESP_LOGD(TAG, "Logged: t_ind=%.1f, t_heat=%.1f, fan=%u, peltier=%d (index=%u)",
                 point.temp_indoor, point.temp_heatsink, point.fan_duty, 
                 point.peltier_on, s_ring_index);
        
        vTaskDelay(pdMS_TO_TICKS(s_log_interval_ms));
    }
}

const data_point_t* data_logger_get_data(uint16_t *count) {
    *count = DATA_POINTS_MAX;
    return s_ring_buffer;
}

data_point_t data_logger_get_latest(void) {
    if (s_ring_index == 0) {
        return s_ring_buffer[DATA_POINTS_MAX - 1];
    }
    return s_ring_buffer[s_ring_index - 1];
}

void data_logger_set_interval(uint32_t interval_ms) {
    if (interval_ms < 1000) interval_ms = 1000;  // Minimum 1 second
    if (interval_ms > 3600000) interval_ms = 3600000;  // Maximum 1 hour
    s_log_interval_ms = interval_ms;
    ESP_LOGI(TAG, "Logging interval set to %lu ms", interval_ms);
}

uint32_t data_logger_get_interval(void) {
    return s_log_interval_ms;
}
