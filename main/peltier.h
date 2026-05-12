#pragma once

#include <stdbool.h>
#include <stdint.h>

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

// ===== PWM Steuerung =====

// PWM aktivieren/deaktivieren
void peltier_pwm_enable(bool enable);

// Duty setzen (1-100%)
void peltier_set_duty(uint8_t duty);

// Duty lesen
uint8_t peltier_get_duty(void);

// PWM-Status lesen
bool peltier_pwm_is_enabled(void);

// ===== Auto-Duty Regelung =====

// Auto-Duty starten
void peltier_autoduty_start(void);

// Auto-Duty stoppen
void peltier_autoduty_stop(void);
