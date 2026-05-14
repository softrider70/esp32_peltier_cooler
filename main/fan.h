#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize fan PWM (Noctua 25kHz) and tacho RPM measurement
void fan_init(void);

// Set fan duty cycle (0-255)
void fan_set_duty(uint8_t duty);

// Get current fan duty
uint8_t fan_get_duty(void);

// Get current fan RPM (from tacho)
uint16_t fan_get_rpm(void);

// Get remaining seconds until next auto-duty adjustment
uint16_t fan_get_duty_timer_remaining(void);

// FreeRTOS task: Controls fan and peltier based on temperatures
void task_fan(void *pvParameters);
