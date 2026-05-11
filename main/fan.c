#include "fan.h"
#include "config.h"
#include "sensor.h"
#include "peltier.h"
#include "pid.h"
#include "scheduler.h"
#include "nvs_config.h"
#include "data_logger.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <math.h>

static const char *TAG = "fan";
static uint8_t s_current_duty = 0;
static pid_controller_t s_fan_pid;
static bool s_was_active = false;  // Track previous active state for NVS save
static bool s_peltier_was_on = false;  // Track Peltier state for fan control
static int s_peltier_off_counter = 0;  // Counter for fan cooldown after peltier off

// ===== Energy Consumption Tracking =====
static uint32_t s_peltier_run_time_sec = 0;  // Total Peltier run time in seconds
static uint32_t s_last_energy_save_time = 0;  // Last time energy was saved (ms)
static float s_last_energy_wh = 0.0f;  // Last saved energy value (for change detection)

// ===== Fan Control Parameters =====
#define FAN_START_DUTY_WHEN_PELTIER_ON  127.0f  // 50% PWM when Peltier turns on (under 70%)
#define FAN_COOLDOWN_DUTY              76.0f   // 30% PWM during cooldown
#define FAN_COOLDOWN_SECONDS            30      // Cooldown time after Peltier off

// RPM measurement variables
static volatile uint32_t s_tacho_pulses = 0;
static volatile uint32_t s_tacho_interrupts = 0;  // Total interrupt count for debug
static uint16_t s_current_rpm = 0;
static uint64_t s_last_rpm_time = 0;
static volatile uint64_t s_last_pulse_time = 0;
#define TACHO_PULSES_PER_REV 2  // Standard Noctua: 2 pulses per revolution
#define RPM_UPDATE_INTERVAL_MS 1000  // Update RPM every second
#define TACHO_DEBOUNCE_US 100  // 100us debounce to filter noise
#define TACHO_ENABLED true  // Enabled - Tacho connected

// Tacho interrupt handler with debounce
static void IRAM_ATTR tacho_isr_handler(void* arg) {
    s_tacho_interrupts++;
    uint64_t now = esp_timer_get_time();
    if (now - s_last_pulse_time > TACHO_DEBOUNCE_US) {
        s_tacho_pulses++;
        s_last_pulse_time = now;
    }
}

void fan_init(void) {
    // Init LEDC for fan PWM
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = FAN_PWM_TIMER,
        .freq_hz = FAN_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ledc_conf = {
        .channel = FAN_PWM_CHANNEL,
        .duty = 0,
        .gpio_num = GPIO_FAN_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel = FAN_PWM_TIMER,
    };
    ledc_channel_config(&ledc_conf);

    // Init PID — setpoint is heatsink_target, output drives fan PWM
    // PID error = measurement - setpoint (positive when too hot → more fan)
    app_config_t *cfg = nvs_config_get();
    pid_init(&s_fan_pid, cfg->pid_kp, cfg->pid_ki, cfg->pid_kd,
             PID_OUTPUT_MIN, PID_OUTPUT_MAX);

    // Initialize last energy value with loaded value
    s_last_energy_wh = cfg->energy_wh;

#if TACHO_ENABLED
    // Configure tacho GPIO for interrupt with pull-up (Noctua is open-collector)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_FAN_TACHO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,  // Trigger on both edges
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_FAN_TACHO, tacho_isr_handler, NULL);
    ESP_LOGI(TAG, "Tacho interrupt installed on GPIO %d", GPIO_FAN_TACHO);

    ESP_LOGI(TAG, "Fan PWM initialized on GPIO %d (25kHz), Tacho on GPIO %d (pull-up, anyedge)", GPIO_FAN_PWM, GPIO_FAN_TACHO);
#else
    ESP_LOGI(TAG, "Fan PWM initialized on GPIO %d (25kHz), Tacho DISABLED (hardware not connected)", GPIO_FAN_PWM);
#endif
}

void fan_set_duty(uint8_t duty) {
    s_current_duty = duty;

    // Invert PWM for NPN transistor (if enabled)
    uint32_t actual_duty = FAN_PWM_INVERTED ? (255 - duty) : duty;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL, actual_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL);
    ESP_LOGI(TAG, "Fan duty: requested=%u, actual=%lu (inverted=%d)", duty, actual_duty, FAN_PWM_INVERTED);
}

uint8_t fan_get_duty(void) {
    return s_current_duty;
}

uint16_t fan_get_rpm(void) {
    return s_current_rpm;
}

// Aktualisiere Tages-/Wochen-/Monats-Statistiken basierend auf Datum
static void update_energy_stats(float energy_increment) {
    app_config_t *cfg = nvs_config_get();
    
    // Hole aktuelle Zeit
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Berechne aktuelles Datum als YYYYMMDD
    uint32_t current_date = (timeinfo.tm_year + 1900) * 10000 + 
                           (timeinfo.tm_mon + 1) * 100 + 
                           timeinfo.tm_mday;
    
    // Wenn Datum geändert hat → Tageszähler zurücksetzen
    if (cfg->last_date != 0 && current_date != cfg->last_date) {
        // Prüfe ob sich Woche geändert hat (Montag ist Tag 1)
        if (timeinfo.tm_wday == 1) {
            cfg->energy_week = 0.0f;
            ESP_LOGI(TAG, "New week - weekly energy reset");
        }
        
        // Prüfe ob sich Monat geändert hat
        if (timeinfo.tm_mday == 1) {
            cfg->energy_month = 0.0f;
            ESP_LOGI(TAG, "New month - monthly energy reset");
        }
        
        cfg->energy_day = 0.0f;
        ESP_LOGI(TAG, "New day - daily energy reset");
    }
    
    // Aktualisiere alle Zähler
    cfg->energy_wh += energy_increment;
    cfg->energy_day += energy_increment;
    cfg->energy_week += energy_increment;
    cfg->energy_month += energy_increment;
    cfg->last_date = current_date;
}

void task_fan_pid(void *pvParameters) {
    (void)pvParameters;

#if TACHO_ENABLED
    s_last_rpm_time = esp_timer_get_time() / 1000;  // Initial time
#endif

    while (1) {
        sensor_data_t sd = sensor_get_data();
        app_config_t *cfg = nvs_config_get();

        // Update RPM calculation every second (disabled due to hardware not connected)
#if TACHO_ENABLED
        uint64_t current_time = esp_timer_get_time() / 1000;  // milliseconds
        if (current_time - s_last_rpm_time >= RPM_UPDATE_INTERVAL_MS) {
            uint32_t pulses = s_tacho_pulses;
            uint32_t interrupts = s_tacho_interrupts;
            s_tacho_pulses = 0;  // Reset counter
            s_tacho_interrupts = 0;  // Reset interrupt counter
            uint64_t time_diff_ms = current_time - s_last_rpm_time;
            s_last_rpm_time = current_time;

            // RPM = (pulses * 60000 * calibration_factor) / (time_ms * pulses_per_rev)
            if (time_diff_ms > 0) {
                s_current_rpm = (uint16_t)(((pulses * 60000UL) * RPM_CALIBRATION_FACTOR) / (time_diff_ms * TACHO_PULSES_PER_REV));
                ESP_LOGI(TAG, "RPM: interrupts=%lu, pulses=%lu, time=%llu ms, rpm=%u", interrupts, pulses, time_diff_ms, s_current_rpm);
            } else {
                s_current_rpm = 0;
            }
        }
#else
        s_current_rpm = 0;  // Tacho disabled - always return 0
#endif

        // Update PID tunings if changed at runtime
        pid_set_tunings(&s_fan_pid, cfg->pid_kp, cfg->pid_ki, cfg->pid_kd);

        bool active = scheduler_is_active();

        // Save graph data when transitioning from active to inactive
        if (s_was_active && !active) {
            data_logger_save_to_nvs();
            ESP_LOGI(TAG, "Graph data saved to NVS on shutdown");
        }
        s_was_active = active;

        // ---- Emergency mode: sensor errors → fan full, peltier off ----
        if (sensor_get_emergency_mode()) {
            peltier_off();
            fan_set_duty(255);
            ESP_LOGW(TAG, "EMERGENCY MODE: Fan full, Peltier off (sensor errors)");
            pid_reset(&s_fan_pid);
            vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
            continue;
        }

        // ---- System inactive or no heatsink sensor: everything off ----
        if (!active || !sd.heatsink_valid) {
            fan_set_duty(0);
            peltier_off();
            pid_reset(&s_fan_pid);
            vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
            continue;
        }

        // ---- Safety: heatsink over max → emergency cutoff ----
        if (sd.temp_heatsink >= cfg->temp_heatsink_max) {
            peltier_off();
            fan_set_duty(255);
            ESP_LOGW(TAG, "SAFETY: Heatsink %.1f°C >= max %.1f°C",
                     sd.temp_heatsink, cfg->temp_heatsink_max);
            pid_reset(&s_fan_pid);
            vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
            continue;
        }

        // ---- Safety: RPM check (fan failure detection) ----
#if TACHO_ENABLED
        if (s_current_duty > 127 && s_current_rpm == 0) {
            // Fan should be running (>50% PWM) but RPM = 0 → fan failure
            ESP_LOGE(TAG, "SAFETY: Fan failure! PWM=%u but RPM=0 - SHUTTING DOWN PELTIER", s_current_duty);
            peltier_off();
            fan_set_duty(255);  // Keep fan at 100% to try to restart
            pid_reset(&s_fan_pid);
            vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
            continue;  // Skip normal PID loop
        }
#endif

        // ---- Peltier: digital on/off based on indoor temperature range ----
        bool peltier_is_on = false;

        if (sd.indoor_valid) {
            if (sd.temp_indoor >= cfg->temp_peltier_on) {
                peltier_on();
                peltier_is_on = true;
            } else if (sd.temp_indoor <= cfg->temp_peltier_off) {
                peltier_off();
                peltier_is_on = false;
            }
            // Between off and on: keep current state (hysteresis band)
        }

        // ---- Lüftersteuerung basierend auf Peltier-Zustand ----
        float fan_output = 0.0f;

        if (peltier_is_on) {
            // Peltier ist AN → Lüfter läuft, PID übernimmt
            s_peltier_off_counter = 0;  // Cooldown-Counter zurücksetzen

            // PID: regulate fan to keep heatsink at target
            float error = sd.temp_heatsink - cfg->temp_heatsink_target;

            if (error <= 0.0f) {
                // Heatsink below target → minimale Lüfterdrehzahl (nicht ganz aus, da Peltier Wärme erzeugt)
                fan_output = FAN_START_DUTY_WHEN_PELTIER_ON * 0.5f;  // 25% als Minimum
                s_fan_pid.integral = 0.0f;
            } else {
                // Heatsink über Ziel → PID regelt hoch
                float dt = PID_SAMPLE_TIME_MS / 1000.0f;

                float p_term = s_fan_pid.kp * error;
                s_fan_pid.integral += error * dt;

                // Anti-windup
                const float integral_max = 10.0f;
                if (s_fan_pid.integral > integral_max) {
                    s_fan_pid.integral = integral_max;
                } else if (s_fan_pid.integral < -integral_max) {
                    s_fan_pid.integral = -integral_max;
                }

                float i_term = s_fan_pid.ki * s_fan_pid.integral;

                if (s_fan_pid.prev_error == 0.0f && error != 0.0f) {
                    s_fan_pid.prev_error = error;
                }

                float derivative = (error - s_fan_pid.prev_error) / dt;
                float d_term = s_fan_pid.kd * derivative;
                s_fan_pid.prev_error = error;

                fan_output = (p_term + i_term + d_term) * 20.0f;

                // Mindestens Start-Drehzahl, wenn Peltier an ist
                if (fan_output < FAN_START_DUTY_WHEN_PELTIER_ON) {
                    fan_output = FAN_START_DUTY_WHEN_PELTIER_ON;
                }
            }
        } else {
            // Peltier ist AUS → Lüfter nachlaufen für Restwärme
            if (s_peltier_off_counter < FAN_COOLDOWN_SECONDS) {
                // Cooldown-Phase
                fan_output = FAN_COOLDOWN_DUTY;
                s_peltier_off_counter++;
                ESP_LOGI(TAG, "Cooldown: %d/%d seconds", s_peltier_off_counter, FAN_COOLDOWN_SECONDS);
            } else {
                // Cooldown vorbei → Lüfter aus
                fan_output = 0.0f;
                s_fan_pid.integral = 0.0f;
            }
        }

        s_peltier_was_on = peltier_is_on;

        // ---- Energy Consumption Calculation ----
        if (peltier_is_on) {
            s_peltier_run_time_sec++;  // Increment run time
        }

        // Calculate energy increment for this interval (1 second)
        float energy_increment = (1.0f / 3600.0f) * PELTIER_POWER;  // Wh per second
        if (peltier_is_on) {
            update_energy_stats(energy_increment);  // Update stats only when Peltier is on
        }

        // Save energy data only if changed OR 15 minutes elapsed (failsafe)
        cfg = nvs_config_get();
        bool energy_changed = fabs(cfg->energy_wh - s_last_energy_wh) > 0.01f;  // Changed by >0.01 Wh
        bool time_elapsed = (current_time - s_last_energy_save_time >= ENERGY_SAVE_INTERVAL_MS);
        
        if (energy_changed || time_elapsed) {
            nvs_config_save_energy();
            s_last_energy_save_time = current_time;
            s_last_energy_wh = cfg->energy_wh;
            ESP_LOGI(TAG, "Energy saved: changed=%d, time_elapsed=%d", energy_changed, time_elapsed);
        }

        // Clamp output
        if (fan_output > PID_OUTPUT_MAX) {
            fan_output = PID_OUTPUT_MAX;
        } else if (fan_output < PID_OUTPUT_MIN) {
            fan_output = PID_OUTPUT_MIN;
        }

        // RPM Feedback: Increase PWM if RPM is lower than expected
#if TACHO_ENABLED
        uint16_t expected_rpm = (uint16_t)((fan_output / 255.0f) * 1700.0f);
        if (s_current_rpm > 0 && s_current_rpm < expected_rpm * 0.8f && fan_output < 250.0f) {
            fan_output *= 1.1f;
            ESP_LOGI(TAG, "RPM feedback: expected=%u, actual=%u, boosting PWM", expected_rpm, s_current_rpm);
        }
#endif

        fan_set_duty((uint8_t)fan_output);

        ESP_LOGD(TAG, "Indoor=%.1f Heatsink=%.1f fan=%d peltier=%s",
                 sd.temp_indoor, sd.temp_heatsink, s_current_duty,
                 peltier_is_on ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
    }
}
