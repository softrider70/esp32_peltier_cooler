#include "fan.h"
#include "config.h"
#include "sensor.h"
#include "peltier.h"
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
static bool s_was_active = false;  // Track previous active state for NVS save
static bool s_peltier_was_on = false;  // Track Peltier state for fan control
static bool s_peltier_main_was_on = false;  // Track Peltier main state for NVS save
static int s_peltier_off_counter = 0;  // Counter for fan cooldown after peltier off

// ===== Energy Consumption Tracking =====
static uint32_t s_peltier_run_time_sec = 0;  // Total Peltier run time in seconds
static float s_last_energy_wh = 0.0f;  // Last saved energy value (for change detection)

// ===== Peltier PWM (langsames PWM für Stromspar-Modus) =====
static uint32_t s_peltier_pwm_timer = 0;  // PWM-Timer in Sekunden
static bool s_peltier_pwm_state = false;  // Aktueller PWM-Zustand (AN/AUS)
static uint32_t s_duty_adjust_timer = 0;  // Timer für Duty-Anpassung

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

    // Initialize last energy value with loaded value
    app_config_t *cfg = nvs_config_get();
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

// Get remaining seconds until next auto-duty adjustment
uint16_t fan_get_duty_timer_remaining(void) {
    app_config_t *cfg = nvs_config_get();
    if (cfg->peltier_pwm_interval > s_duty_adjust_timer) {
        return cfg->peltier_pwm_interval - s_duty_adjust_timer;
    }
    return 0;
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
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // ---- System inactive or no heatsink sensor: everything off ----
        if (!active || !sd.heatsink_valid) {
            fan_set_duty(0);
            peltier_off();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // ---- Safety: heatsink over max → emergency cutoff ----
        if (sd.temp_heatsink >= cfg->temp_heatsink_max) {
            peltier_off();
            fan_set_duty(255);
            ESP_LOGW(TAG, "SAFETY: Heatsink %.1f°C >= max %.1f°C",
                     sd.temp_heatsink, cfg->temp_heatsink_max);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // ---- Safety: RPM check (fan failure detection) ----
#if TACHO_ENABLED
        if (s_current_duty > 127 && s_current_rpm == 0) {
            // Fan should be running (>50% PWM) but RPM = 0 → fan failure
            ESP_LOGE(TAG, "SAFETY: Fan failure! PWM=%u but RPM=0 - SHUTTING DOWN PELTIER", s_current_duty);
            peltier_off();
            fan_set_duty(255);  // Keep fan at 100% to try to restart
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;  // Skip normal PID loop
        }
#endif

        // ---- Peltier: digital on/off based on indoor temperature range ----
        bool peltier_main_state = false;  // Hauptzustand (für NVS-Schreiben)

        if (sd.indoor_valid) {
            if (sd.temp_indoor >= cfg->temp_peltier_on) {
                peltier_main_state = true;
            } else if (sd.temp_indoor <= cfg->temp_peltier_off) {
                peltier_main_state = false;
            }
            // Between off and on: keep current state (hysteresis band)
            ESP_LOGD(TAG, "Peltier: indoor=%.1f, on=%.1f, off=%.1f, main_state=%d",
                     sd.temp_indoor, cfg->temp_peltier_on, cfg->temp_peltier_off, peltier_main_state);
        } else {
            ESP_LOGW(TAG, "Peltier: indoor sensor invalid");
        }

        // ---- Peltier PWM (langsames PWM für Stromspar-Modus) ----
        bool peltier_hw_on = false;  // Hardware-Zustand

        if (peltier_main_state) {
            // Hauptzustand AN → Hardware wird durch PWM gesteuert
            uint32_t on_time = (cfg->peltier_pwm_period * cfg->peltier_pwm_duty) / 100;
            
            s_peltier_pwm_timer++;
            if (s_peltier_pwm_timer >= cfg->peltier_pwm_period) {
                s_peltier_pwm_timer = 0;  // Periode zurücksetzen
            }
            
            // Hardware wird durch PWM gesteuert: AN wenn timer < on_time
            s_peltier_pwm_state = (s_peltier_pwm_timer < on_time);
            peltier_hw_on = s_peltier_pwm_state;  // Hardware folgt PWM-Status
            
            ESP_LOGD(TAG, "Peltier PWM: timer=%u/%u, on_time=%u, pwm_state=%d, hw_state=%d",
                     s_peltier_pwm_timer, cfg->peltier_pwm_period, on_time, s_peltier_pwm_state, peltier_hw_on);
            
            // ---- Automatische Duty-Anpassung basierend auf Innentemperatur ----
            s_duty_adjust_timer++;
            if (s_duty_adjust_timer >= cfg->peltier_pwm_interval) {  // Konfigurierbares Intervall
                s_duty_adjust_timer = 0;
                
                if (sd.indoor_valid && cfg->peltier_pwm_auto) {
                    uint8_t new_duty = cfg->peltier_pwm_duty;
                    
                    // Logik basierend auf absoluter Temperatur mit Formfaktor
                    // Peltier-Kühlleistung sinkt bei größerer Temperaturdifferenz
                    // Daher: Mehr Duty bei größerer Differenz zum Ziel
                    float temp_error = sd.temp_indoor - cfg->temp_peltier_on;  // Fehler zum Ziel (13°C)

                    // Formfaktor: Je größer der Fehler, desto mehr Duty wird benötigt
                    // Faktor 0.2 bedeutet: Bei 1°C über Ziel → 20% mehr Duty (erhöht für stärkere Reaktion)
                    float duty_factor = 1.0f + (temp_error * 0.2f);

                    // Begrenzung des Formfaktors auf sinnvolle Werte (0.5 bis 3.0)
                    if (duty_factor < 0.5f) duty_factor = 0.5f;
                    if (duty_factor > 3.0f) duty_factor = 3.0f;

                    // Basis-Duty berechnen (40% bei genauem Ziel, angepasst durch Formfaktor)
                    uint8_t base_duty = 40;
                    uint8_t target_duty = (uint8_t)(base_duty * duty_factor);
                    
                    ESP_LOGI(TAG, "Auto-Duty check: Indoor=%.1f°C, Target=%.1f°C, Error=%.1f°C, Factor=%.2f, Base=%u%%, Target=%u%%",
                             sd.temp_indoor, cfg->temp_peltier_on, temp_error, duty_factor, base_duty, target_duty);
                    
                    // Sanfte Annäherung an Ziel-Duty (max ±5% pro Schritt)
                    if (target_duty > new_duty + 5) {
                        new_duty += 5;
                    } else if (target_duty < new_duty - 5) {
                        new_duty -= 5;
                    } else {
                        new_duty = target_duty;
                    }
                    
                    // Begrenzung auf 10-100%
                    if (new_duty < 10) new_duty = 10;
                    if (new_duty > 100) new_duty = 100;
                    
                    ESP_LOGI(TAG, "Auto-Duty: Adjusting duty to %u%% (target was %u%%)", new_duty, target_duty);
                    
                    // Duty speichern, wenn geändert
                    if (new_duty != cfg->peltier_pwm_duty) {
                        cfg->peltier_pwm_duty = new_duty;
                        nvs_config_save();
                    }
                }
            }
        } else {
            // Hauptzustand AUS → Hardware AUS
            peltier_hw_on = false;
            s_peltier_pwm_timer = 0;
            s_peltier_pwm_state = false;
        }

        // ---- Lüftersteuerung basierend auf Peltier-Hauptzustand (nicht PWM-Zustand) ----
        float fan_output = 0.0f;

        if (peltier_main_state) {
            // Peltier-Hauptzustand ist AN → Lüfter regelt basierend auf Kühlblock-Temp
            // Hardware wird separat gesteuert (siehe unten)
            
            // Einfache P-Steuerung ohne Deadband oder Glättung
            float error = sd.temp_heatsink - cfg->temp_heatsink_target;
            float temp_diff_to_max = cfg->temp_heatsink_max - sd.temp_heatsink;
            
            float fan_output_percent = 40.0f;
            
            // Bis 3 Grad unter max: Linear bis 68%
            if (temp_diff_to_max > 3.0f) {
                // Linearer Bereich: 40% + (error * 8%) pro °C
                fan_output_percent = 40.0f + (error * 8.0f);
                if (fan_output_percent > 68.0f) fan_output_percent = 68.0f;
            } else {
                // Exponentieller Bereich bei Temperaturen nahe max
                // Exponentialfunktion: 68% * exp((3 - temp_diff) * 0.5)
                float exp_factor = expf((3.0f - temp_diff_to_max) * 0.5f);
                fan_output_percent = 68.0f * exp_factor;
                if (fan_output_percent > 100.0f) fan_output_percent = 100.0f;
            }
            
            // Clamp zwischen 30% und 100%
            if (fan_output_percent < 30.0f) fan_output_percent = 30.0f;
            
            ESP_LOGI(TAG, "Fan control: temp=%.1f°C, error=%.1f°C, diff_to_max=%.1f°C, fan=%.0f%%", 
                     sd.temp_heatsink, error, temp_diff_to_max, fan_output_percent);
            
            fan_output = fan_output_percent * 2.55f;  // 0-100% → 0-255
        } else {
            // Peltier-Hauptzustand ist AUS → Lüfter nachlaufen für Restwärme
            if (s_peltier_off_counter < FAN_COOLDOWN_SECONDS) {
                // Cooldown-Phase
                fan_output = FAN_COOLDOWN_DUTY;
                s_peltier_off_counter++;
                ESP_LOGI(TAG, "Cooldown: %d/%d seconds", s_peltier_off_counter, FAN_COOLDOWN_SECONDS);
            } else {
                // Cooldown vorbei → Lüfter aus
                fan_output = 0.0f;
            }
        }

        // ---- Hardware-Steuerung basierend auf Hauptzustand ----
        if (peltier_hw_on) {
            peltier_on();  // Hardware einschalten
            s_peltier_off_counter = 0;  // Cooldown-Counter zurücksetzen
        } else {
            peltier_off();  // Hardware ausschalten
        }

        s_peltier_was_on = peltier_hw_on;

        // ---- Energy Consumption Calculation ----
        if (peltier_hw_on) {
            s_peltier_run_time_sec++;  // Increment run time
        }

        // Calculate energy increment for this interval (1 second)
        float energy_increment = (1.0f / 3600.0f) * PELTIER_POWER;  // Wh per second
        if (peltier_hw_on) {
            update_energy_stats(energy_increment);  // Update stats only when Peltier is on
        }

        // Save energy data only when Peltier main state turns OFF and value changed
        if (s_peltier_main_was_on && !peltier_main_state) {
            cfg = nvs_config_get();
            bool energy_changed = fabs(cfg->energy_wh - s_last_energy_wh) > 0.01f;  // Changed by >0.01 Wh
            
            if (energy_changed) {
                nvs_config_save_energy();
                s_last_energy_wh = cfg->energy_wh;
                ESP_LOGI(TAG, "Energy saved on Peltier main OFF: %.2f Wh", cfg->energy_wh);
            }
        }
        
        // Track previous main state
        s_peltier_main_was_on = peltier_main_state;

        // Clamp output
        if (fan_output > 255.0f) {
            fan_output = 255.0f;
        } else if (fan_output < 0.0f) {
            fan_output = 0.0f;
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
                 peltier_hw_on ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
