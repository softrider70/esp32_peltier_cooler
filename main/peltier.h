#pragma once

#include <stdbool.h>

// Initialize Peltier GPIO (digital output)
void peltier_init(void);

// Turn Peltier on (MOSFET gate high)
void peltier_on(void);

// Turn Peltier off (MOSFET gate low)
void peltier_off(void);

// Check if Peltier is currently active (hardware state)
bool peltier_is_on(void);

// Main state control (temperature-based, not PWM)
void peltier_set_main_state(bool state);
bool peltier_get_main_state(void);
