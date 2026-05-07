#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize fan PWM (Noctua 25kHz)
void fan_init(void);

// Set fan duty cycle (0-255)
void fan_set_duty(uint8_t duty);

// Get current fan duty
uint8_t fan_get_duty(void);

// FreeRTOS task: PID-regulates fan based on heatsink temp,
// controls peltier on/off based on indoor temp range
void task_fan_pid(void *pvParameters);
