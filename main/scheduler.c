#include "scheduler.h"
#include "config.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "scheduler";
static bool s_active = false;
static bool s_time_synced = false;

void scheduler_init(void) {
    // Configure SNTP for time sync
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized, waiting for time sync");
}

static bool is_weekend(int wday) {
    return (wday == 0 || wday == 6);  // Sunday=0, Saturday=6
}

static bool check_time_window(void) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Check if time is valid (year > 2020)
    if (timeinfo.tm_year < (2020 - 1900)) {
        if (!s_time_synced) {
            return true;  // Default to active until time is synced
        }
        return s_active;  // Keep last state
    }
    s_time_synced = true;

    app_config_t *cfg = nvs_config_get();
    uint16_t current_minute = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    schedule_window_t window;
    if (is_weekend(timeinfo.tm_wday)) {
        window.on_minute = cfg->sched_we_on;
        window.off_minute = cfg->sched_we_off;
    } else {
        window.on_minute = cfg->sched_wd_on;
        window.off_minute = cfg->sched_wd_off;
    }

    // Handle overnight schedules (off < on means crossing midnight)
    if (window.on_minute <= window.off_minute) {
        return (current_minute >= window.on_minute && current_minute < window.off_minute);
    } else {
        return (current_minute >= window.on_minute || current_minute < window.off_minute);
    }
}

bool scheduler_is_active(void) {
    return s_active;
}

void task_scheduler(void *pvParameters) {
    (void)pvParameters;

    // Set timezone to CET/CEST
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    while (1) {
        s_active = check_time_window();
        ESP_LOGD(TAG, "Scheduler active: %s", s_active ? "YES" : "NO");
        vTaskDelay(pdMS_TO_TICKS(30000));  // Check every 30 seconds
    }
}
