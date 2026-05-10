#pragma once

#include <stdint.h>
#include <stdbool.h>

// Data point structure for time series
typedef struct {
    uint32_t timestamp;      // Unix timestamp
    float temp_indoor;        // Indoor temperature
    float temp_heatsink;      // Heatsink temperature
    uint8_t fan_duty;        // Fan PWM (0-255)
    bool peltier_on;         // Peltier status
} data_point_t;

// Configuration
#define DATA_POINTS_MAX 720   // 2h at 10s interval (7200s / 10s = 720 points)
#define DATA_LOGGER_INTERVAL_MS 10000  // 10 seconds

// Initialize data logger (allocate ring buffer in RAM)
void data_logger_init(void);

// FreeRTOS task: records sensor data every 10 seconds
void task_data_logger(void *pvParameters);

// Get all data points for graph (returns pointer to ring buffer)
const data_point_t* data_logger_get_data(uint16_t *count);

// Get latest data point
data_point_t data_logger_get_latest(void);

// Set logging interval (in milliseconds)
void data_logger_set_interval(uint32_t interval_ms);

// Get current logging interval (in milliseconds)
uint32_t data_logger_get_interval(void);
