#pragma once

#include <stdbool.h>
#include <stdint.h>

// Schedule entry (minutes from midnight)
typedef struct {
    uint16_t on_minute;     // 0-1439 (e.g. 480 = 08:00)
    uint16_t off_minute;    // 0-1439 (e.g. 1320 = 22:00)
} schedule_window_t;

// Initialize scheduler (loads schedule from NVS)
void scheduler_init(void);

// Check if cooling should be active right now based on schedule
bool scheduler_is_active(void);

// FreeRTOS task: checks time window and enables/disables system
void task_scheduler(void *pvParameters);
