#pragma once

#include <stdint.h>
#include <stdbool.h>

// Energie-Sitzungsstruktur (optimiert für minimale Größe)
typedef struct {
    uint32_t timestamp;       // Unix Timestamp (Startzeit) - zurück wegen uint16_t Überlauf
    uint16_t duration_min;    // Dauer der Sitzung in Minuten
    int16_t start_temp;        // Indoor-Temperatur zu Beginn (0.1°C Schritte)
    int16_t end_temp;          // Indoor-Temperatur am Ende (0.1°C Schritte)
    uint16_t energy_wh;        // Energieverbrauch in 0.01Wh Schritten
    int16_t min_temp;          // Min-Temperatur während Session (0.1°C Schritte)
    int16_t max_temp;          // Max-Temperatur während Session (0.1°C Schritte)
    uint8_t pwm_period;       // PWM Period in Sekunden
    uint8_t auto_duty_cycle;  // Auto-Duty Zyklus in Sekunden
    bool auto_duty_enabled;   // Auto-Duty war aktiviert
} energy_session_t;

// Konfiguration
#define ENERGY_SESSIONS_MAX 10   // 10 Sitzungen im Ringbuffer (NV Platz-Optimierung)

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

// Löscht alle Energie-Daten aus NVS und lokalem Speicher
void energy_tracker_clear_nvs(void);