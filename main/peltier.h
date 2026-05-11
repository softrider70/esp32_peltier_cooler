#pragma once

#include <stdbool.h>

// Initialize Peltier GPIO (digital output)
void peltier_init(void);

// Turn Peltier on (MOSFET gate high)
void peltier_on(void);

// Turn Peltier off (MOSFET gate low)
void peltier_off(void);

// Check if Peltier is currently active
bool peltier_is_on(void);
