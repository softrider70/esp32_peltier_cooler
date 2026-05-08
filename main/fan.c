#include "fan.h"
#include "config.h"
#include "sensor.h"
#include "peltier.h"
#include "pid.h"
#include "scheduler.h"
#include "nvs_config.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "fan";
static uint8_t s_current_duty = 0;
static pid_controller_t s_fan_pid;

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

    ESP_LOGI(TAG, "Fan PWM initialized on GPIO %d (25kHz)", GPIO_FAN_PWM);
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

void task_fan_pid(void *pvParameters) {
    (void)pvParameters;

    const float dt = (float)PID_SAMPLE_TIME_MS / 1000.0f;

    while (1) {
        sensor_data_t sd = sensor_get_data();
        app_config_t *cfg = nvs_config_get();

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
        // We compute error as (measurement - setpoint) so that positive error
        // means "too hot" → increase fan speed. This is an inverted PID
        // (output rises with positive error).
        float error = sd.temp_heatsink - cfg->temp_heatsink_target;
        float fan_output;

        if (error <= 0.0f) {
            // Heatsink below target — fan off, reset integrator
            fan_output = PID_OUTPUT_MIN;
            pid_reset(&s_fan_pid);
        } else {
            // Use pid_compute with swapped args so error = measurement - setpoint > 0
            // pid_compute: error = setpoint - measurement, so pass (measurement, setpoint)
            // to get error = measurement - setpoint (positive → output grows)
            fan_output = pid_compute(&s_fan_pid, 0.0f, -error, dt);
            // pid error = 0 - (-error) = +error → positive → output increases
        }

        fan_set_duty((uint8_t)fan_output);

        ESP_LOGD(TAG, "Indoor=%.1f Heatsink=%.1f fan=%d peltier=%s",
                 sd.temp_indoor, sd.temp_heatsink, s_current_duty,
                 peltier_is_on() ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
    }
}
