#include "task_monitor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "task_monitor";

// Globale Statistiken
static system_stat_t s_system_stats = {0};

// Task-Statistiken sammeln (vereinfacht: nur Heap-Informationen)
static void collect_task_stats(void) {
    // Heap-Informationen
    s_system_stats.free_heap = esp_get_free_heap_size();
    s_system_stats.min_free_heap = esp_get_minimum_free_heap_size();
    
    // Task-Count kann ohne uxTaskGetSystemState nicht ermittelt werden
    s_system_stats.task_count = 0;
    
    // Warnung: Heap < 10KB
    if (s_system_stats.free_heap < 10240) {
        ESP_LOGE(TAG, "Low heap: %lu bytes", s_system_stats.free_heap);
    }
    
    ESP_LOGI(TAG, "Free Heap: %lu bytes, Min Free Heap: %lu bytes", 
             s_system_stats.free_heap, s_system_stats.min_free_heap);
}

void task_monitor_init(void) {
    ESP_LOGI(TAG, "=== task_monitor_init() ===");
    memset(&s_system_stats, 0, sizeof(system_stat_t));
}

void task_monitor_start(void) {
    xTaskCreate(task_monitor, "task_monitor", 4096, NULL, 2, NULL);
}

system_stat_t task_monitor_get_stats(void) {
    return s_system_stats;
}

void task_monitor(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "=== task_monitor() STARTED ===");
    
    // Kurze Verzögerung vor erstem Durchlauf
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    while (1) {
        collect_task_stats();
        
        // Alle 5 Sekunden Statistiken sammeln
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
