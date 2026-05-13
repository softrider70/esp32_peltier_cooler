#pragma once

#include <stdbool.h>
#include <stdint.h>

// Task-Statistik-Struktur
typedef struct {
    char name[16];          // Task-Name
    uint32_t runtime;       // Laufzeit in Ticks
    uint32_t runtime_pct;   // CPU-Auslastung in %
    uint16_t stack_high_water;  // Stack-High-Water-Mark (verbleibender Stack)
    uint16_t stack_size;    // Stack-Größe
    bool stack_warning;     // Stack < 20% frei
    bool cpu_warning;       // CPU > 80%
} task_stat_t;

// System-Statistik
typedef struct {
    uint32_t free_heap;     // Freier Heap in Bytes
    uint32_t min_free_heap; // Minimaler freier Heap
    uint32_t task_count;    // Anzahl der Tasks
    task_stat_t tasks[16];  // Task-Statistiken (max 16 Tasks)
} system_stat_t;

// Initialisierung
void task_monitor_init(void);

// Task starten
void task_monitor_start(void);

// System-Statistiken abrufen
system_stat_t task_monitor_get_stats(void);

// FreeRTOS Task
void task_monitor(void *pvParameters);
