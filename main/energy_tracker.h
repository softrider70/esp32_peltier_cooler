#pragma once

#include <stdint.h>
#include <stdbool.h>

// Energie-Sitzungsstruktur
typedef struct {
    uint32_t timestamp;       // Unix Timestamp (Startzeit)
    uint32_t duration_sec;    // Dauer der Sitzung in Sekunden
    float start_temp;         // Indoor-Temperatur zu Beginn (°C)
    float end_temp;           // Indoor-Temperatur am Ende (°C)
    float min_temp;           // Minimale Indoor-Temperatur während Sitzung (°C)
    float max_temp;           // Maximale Indoor-Temperatur während Sitzung (°C)
    float energy_wh;          // Energieverbrauch in Wh
    uint8_t pwm_period;       // PWM Period in Sekunden
    bool auto_duty_enabled;   // Auto-Duty war aktiviert
    uint8_t auto_duty_cycle;  // Auto-Duty Zyklus in Sekunden
    float target_temp;        // Zieltemperatur für Lüfter-PID (°C)
} energy_session_t;

// Konfiguration
#define ENERGY_SESSIONS_MAX 50   // 50 Sitzungen im Ringbuffer

// Initialize energy tracker
void energy_tracker_init(void);

// Start tracking einer neuen Sitzung (wenn Peltier main_state true wird)
void energy_tracker_start_session(void);

// Stop tracking und speichert die Sitzung (wenn Peltier main_state false wird)
void energy_tracker_stop_session(void);

// Aktualisiert die Energie während einer aktiven Sitzung
void energy_tracker_update_energy(float pwm_duty_percent);

// Prüft, ob gerade tracking aktiv ist
bool energy_tracker_is_tracking(void);

// Liefert alle Sitzungen (sortiert nach Datum, neueste zuerst)
const energy_session_t* energy_tracker_get_sessions(uint16_t *count);

// Speichert alle Sitzungen in NVS
void energy_tracker_save_to_nvs(void);

// Lädt alle Sitzungen aus NVS
void energy_tracker_load_from_nvs(void);