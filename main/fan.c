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

static const char *TAG = "fan";
static uint8_t s_current_duty = 0;
static pid_controller_t s_fan_pid;
static bool s_was_active = false;  // Track previous active state for NVS save

// Predictive Trendanalyse Variablen
static float s_dt_history[P_PREDICTIVE_WINDOW];  // Ringbuffer für dT/dt
static int s_dt_index = 0;                       // Aktueller Index
static float s_dt_avg = 0.0f;                    // Gleitender Durchschnitt
static float s_dt_accel = 0.0f;                  // Beschleunigung (d²T/dt²)
static float s_prev_dt_avg = 0.0f;               // Vorheriger Durchschnitt
static float s_last_temp_indoor = 0.0f;          // Letzte Temperatur
static uint64_t s_last_temp_time = 0;            // Letzte Zeit

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
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = FAN_PWM_RESOLUTION,
        .timer_num = FAN_PWM_TIMER,
        .freq_hz = FAN_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .gpio_num = GPIO_FAN_PWM,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = FAN_PWM_CHANNEL,
        .timer_sel = FAN_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_conf);

    // Init PID — setpoint is heatsink_target, output drives fan PWM
    // PID error = measurement - setpoint (positive when too hot → more fan)
    app_config_t *cfg = nvs_config_get();
    pid_init(&s_fan_pid, cfg->pid_kp, cfg->pid_ki, cfg->pid_kd,
             PID_OUTPUT_MIN, PID_OUTPUT_MAX);

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

void task_fan_pid(void *pvParameters) {
    (void)pvParameters;

    const float dt = (float)PID_SAMPLE_TIME_MS / 1000.0f;
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

        // ---- Predictive Trendanalyse: Vorhersage, wann Ziel erreicht wird ----
        float dt_min = 0.0f;
        if (sd.indoor_valid && s_last_temp_time > 0) {
            uint64_t now = esp_timer_get_time() / 1000;  // ms
            float time_diff_min = (now - s_last_temp_time) / 60000.0f;
            if (time_diff_min > 0) {
                dt_min = (sd.temp_indoor - s_last_temp_indoor) / time_diff_min;
            }
        }
        s_last_temp_indoor = sd.temp_indoor;
        s_last_temp_time = esp_timer_get_time() / 1000;

        // Gleitenden Durchschnitt berechnen
        s_dt_history[s_dt_index] = dt_min;
        s_dt_index = (s_dt_index + 1) % P_PREDICTIVE_WINDOW;

        float sum = 0.0f;
        for (int i = 0; i < P_PREDICTIVE_WINDOW; i++) {
            sum += s_dt_history[i];
        }
        s_dt_avg = sum / P_PREDICTIVE_WINDOW;

        // Beschleunigung berechnen
        s_dt_accel = s_dt_avg - s_prev_dt_avg;
        s_prev_dt_avg = s_dt_avg;

        // Prädiktive Logik: Sofort boosten bei schnellem Temperaturanstieg
        float fan_boost_factor = 1.0f;  // Standard: kein Boost
#if P_PREDICTIVE_ENABLED
        if (s_dt_avg > P_PREDICTIVE_DT_THRESHOLD) {
            // Temperatur steigt schnell → sofort boosten
            // Boost proportional zur Steigungsrate
            float boost = 1.0f + (s_dt_avg - P_PREDICTIVE_DT_THRESHOLD) * 2.0f;
            if (boost > 2.0f) boost = 2.0f;  // Maximaler Boost
            fan_boost_factor = boost;
            ESP_LOGI(TAG, "Predictive: dT=%.3f°C/min, accel=%.3f, boost=%.2fx",
                     s_dt_avg, s_dt_accel, boost);
        }
#endif

        // ---- Peltier: digital on/off based on indoor temperature range ----
        if (sd.indoor_valid) {
            if (sd.temp_indoor >= cfg->temp_peltier_on) {
                peltier_on();
            } else if (sd.temp_indoor <= cfg->temp_peltier_off) {
                peltier_off();
            }
            // Between off and on: keep current state (hysteresis band)
        }

        // ---- PID: regulate fan to keep heatsink at target ----
        // For cooling: error = measurement - setpoint (positive when too hot → more fan)
        // This is an inverted PID compared to standard (where error = setpoint - measurement)
        float error = sd.temp_heatsink - cfg->temp_heatsink_target;
        float fan_output;

        if (error <= 0.0f) {
            // Heatsink below target — fan off
            fan_output = 0.0f;
            s_fan_pid.integral = 0.0f;  // Reset only integral, keep prev_error
        } else {
            // Manual PID computation with inverted error for cooling
            // error = measurement - setpoint (positive when too hot)
            float p_term = s_fan_pid.kp * error;
            s_fan_pid.integral += error * dt;

            // Anti-windup: Limit integral term
            const float integral_max = 10.0f;  // Prevent integral windup
            if (s_fan_pid.integral > integral_max) {
                s_fan_pid.integral = integral_max;
            } else if (s_fan_pid.integral < -integral_max) {
                s_fan_pid.integral = -integral_max;
            }

            float i_term = s_fan_pid.ki * s_fan_pid.integral;

            // Initialize prev_error on first run
            if (s_fan_pid.prev_error == 0.0f && error != 0.0f) {
                s_fan_pid.prev_error = error;
            }

            float derivative = (error - s_fan_pid.prev_error) / dt;
            float d_term = s_fan_pid.kd * derivative;
            s_fan_pid.prev_error = error;

            // Scale PID output to PWM range (0-255)
            // Assuming max error ~5°C should give 100% PWM
            fan_output = (p_term + i_term + d_term) * 20.0f;

            // Apply predictive fan boost
            fan_output *= fan_boost_factor;

            // Clamp output
            if (fan_output > PID_OUTPUT_MAX) {
                fan_output = PID_OUTPUT_MAX;
            } else if (fan_output < PID_OUTPUT_MIN) {
                fan_output = PID_OUTPUT_MIN;
            }

            // RPM Feedback: Increase PWM if RPM is lower than expected
#if TACHO_ENABLED
            uint16_t expected_rpm = (uint16_t)((fan_output / 255.0f) * 1700.0f);  // Expected RPM at this PWM
            if (s_current_rpm > 0 && s_current_rpm < expected_rpm * 0.8f && fan_output < 250.0f) {
                // RPM is < 80% of expected, increase PWM by 10%
                fan_output *= 1.1f;
                ESP_LOGI(TAG, "RPM feedback: expected=%u, actual=%u, boosting PWM", expected_rpm, s_current_rpm);
            }
#endif
        }

        fan_set_duty((uint8_t)fan_output);

        ESP_LOGD(TAG, "Indoor=%.1f Heatsink=%.1f fan=%d peltier=%s",
                 sd.temp_indoor, sd.temp_heatsink, s_current_duty,
                 peltier_is_on() ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
    }
}
