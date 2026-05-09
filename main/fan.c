#include "fan.h"
#include "config.h"
#include "sensor.h"
#include "peltier.h"
#include "pid.h"
#include "scheduler.h"
#include "nvs_config.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fan";
static uint8_t s_current_duty = 0;
static pid_controller_t s_fan_pid;

// RPM measurement variables
static volatile uint32_t s_tacho_pulses = 0;
static volatile uint32_t s_tacho_interrupts = 0;  // Total interrupt count for debug
static uint16_t s_current_rpm = 0;
static uint64_t s_last_rpm_time = 0;
static volatile uint64_t s_last_pulse_time = 0;
#define TACHO_PULSES_PER_REV 2  // Standard Noctua: 2 pulses per revolution
#define RPM_UPDATE_INTERVAL_MS 1000  // Update RPM every second
#define TACHO_DEBOUNCE_US 1000  // 1ms debounce to filter noise

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

    // Configure tacho GPIO for interrupt with pull-up (Noctua is open-collector)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_FAN_TACHO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Trigger on falling edge (pulse low)
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_FAN_TACHO, tacho_isr_handler, NULL);

    ESP_LOGI(TAG, "Fan PWM initialized on GPIO %d (25kHz), Tacho on GPIO %d (pull-up, negedge)", GPIO_FAN_PWM, GPIO_FAN_TACHO);
}

void fan_set_duty(uint8_t duty) {
    s_current_duty = duty;
    
    // Invert PWM for NPN transistor (if enabled)
    uint32_t actual_duty = FAN_PWM_INVERTED ? (255 - duty) : duty;
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL, actual_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_PWM_CHANNEL);
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
    s_last_rpm_time = esp_timer_get_time() / 1000;  // Initial time

    while (1) {
        sensor_data_t sd = sensor_get_data();
        app_config_t *cfg = nvs_config_get();

        // Update RPM calculation every second
        uint64_t current_time = esp_timer_get_time() / 1000;  // milliseconds
        if (current_time - s_last_rpm_time >= RPM_UPDATE_INTERVAL_MS) {
            uint32_t pulses = s_tacho_pulses;
            uint32_t interrupts = s_tacho_interrupts;
            s_tacho_pulses = 0;  // Reset counter
            s_tacho_interrupts = 0;  // Reset interrupt counter
            uint64_t time_diff_ms = current_time - s_last_rpm_time;
            s_last_rpm_time = current_time;

            // RPM = (pulses * 60000) / (time_ms * pulses_per_rev)
            if (time_diff_ms > 0) {
                s_current_rpm = (uint16_t)((pulses * 60000UL) / (time_diff_ms * TACHO_PULSES_PER_REV));
                ESP_LOGI(TAG, "RPM: interrupts=%lu, pulses=%lu, time=%llu ms, rpm=%u", interrupts, pulses, time_diff_ms, s_current_rpm);
            } else {
                s_current_rpm = 0;
            }
        }

        // Update PID tunings if changed at runtime
        pid_set_tunings(&s_fan_pid, cfg->pid_kp, cfg->pid_ki, cfg->pid_kd);

        bool active = scheduler_is_active();

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
            // Heatsink below target — fan off, reset integrator
            fan_output = PID_OUTPUT_MIN;
            pid_reset(&s_fan_pid);
        } else {
            // Manual PID computation with inverted error for cooling
            // error = measurement - setpoint (positive when too hot)
            float p_term = s_fan_pid.kp * error;
            s_fan_pid.integral += error * dt;
            float i_term = s_fan_pid.ki * s_fan_pid.integral;
            float derivative = (error - s_fan_pid.prev_error) / dt;
            float d_term = s_fan_pid.kd * derivative;
            s_fan_pid.prev_error = error;

            fan_output = p_term + i_term + d_term;

            // Clamp output
            if (fan_output > PID_OUTPUT_MAX) {
                fan_output = PID_OUTPUT_MAX;
                s_fan_pid.integral -= error * dt;  // Anti-windup
            } else if (fan_output < PID_OUTPUT_MIN) {
                fan_output = PID_OUTPUT_MIN;
                s_fan_pid.integral -= error * dt;  // Anti-windup
            }
        }

        fan_set_duty((uint8_t)fan_output);

        ESP_LOGD(TAG, "Indoor=%.1f Heatsink=%.1f fan=%d peltier=%s",
                 sd.temp_indoor, sd.temp_heatsink, s_current_duty,
                 peltier_is_on() ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
    }
}
