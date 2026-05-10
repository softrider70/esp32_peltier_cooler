#pragma once

#include "driver/gpio.h"

// DS18B20 sensor readings (shared state)
typedef struct {
    float temp_indoor;      // Indoor/cooling temperature (sensor 1)
    float temp_heatsink;    // Heatsink temperature (sensor 2)
    bool  indoor_valid;
    bool  heatsink_valid;
} sensor_data_t;

// Initialize OneWire bus and discover DS18B20 sensors
void sensor_init(void);

// FreeRTOS task: periodically reads both sensors
void task_sensor(void *pvParameters);

// Get current sensor readings (thread-safe)
sensor_data_t sensor_get_data(void);

// Get emergency mode status (activated after 5 consecutive read errors)
bool sensor_get_emergency_mode(void);
