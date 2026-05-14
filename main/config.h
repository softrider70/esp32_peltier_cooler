#pragma once

// ===== Build Information =====
#define BUILD_NUMBER        318     // Build counter - increment with each flash

// ===== GPIO Configuration (ESP32-D Board D2-D35) =====
#define GPIO_FAN_PWM        5       // Noctua 4-pin PWM signal (D5/GPIO5)
#define GPIO_FAN_TACHO      18      // Noctua 4-pin tachometer input (D18/GPIO18)
#define GPIO_ONEWIRE_BUS    4       // DS18B20 OneWire data line (D4/GPIO4)
#define GPIO_PELTIER        16      // MOSFET gate for Peltier element (D16/GPIO16)
#define GPIO_RESET_BUTTON   0       // BOOT/RESET button (GPIO0) for WiFi reset
#define GPIO_STATUS_LED     2       // Status LED (Rot/Grün/Orange)

// ===== PWM Configuration (NPN Transistor Inverter) =====
#define FAN_PWM_FREQ_HZ     25000   // Noctua spec: 25 kHz PWM
#define FAN_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_PWM_TIMER       LEDC_TIMER_0
#define FAN_PWM_RESOLUTION  LEDC_TIMER_8_BIT  // 0-255
#define FAN_PWM_INVERTED    true    // PWM invertierung aktiviert (Hardware invertiert)

// ===== RPM Calibration =====
#define RPM_CALIBRATION_FACTOR 0.567f  // Scale factor: 1700 target / 3000 measured

// ===== Temperature Thresholds =====
#define TEMP_PELTIER_ON_DEFAULT     13.0f   // Peltier ON when indoor above this
#define TEMP_PELTIER_OFF_DEFAULT    11.0f   // Peltier OFF when indoor below this
#define TEMP_HEATSINK_MAX           52.0f   // Max heatsink temp (safety cutoff)
#define TEMP_HEATSINK_TARGET        46.0f   // PID target for heatsink

// ===== Peltier PWM Defaults =====
#define PELTIER_PWM_PERIOD_DEFAULT  10      // PWM period in seconds
#define PELTIER_PWM_DUTY_DEFAULT   10      // PWM duty cycle % (10 = start value for power saving)

// ===== Auto-Duty Defaults =====
#define AUTO_DUTY_EN_DEFAULT       true    // Auto-Duty enabled by default
#define AUTO_DUTY_DUTY_DEFAULT     80      // Auto-Duty duty cycle % (default 80%)
#define AUTO_DUTY_CYCLE_DEFAULT    3       // Auto-Duty cycle duration in seconds (default 3s for faster response)

// ===== Sensor Configuration =====
#define SENSOR_READ_INTERVAL_MS  2000   // Read sensors every 2s
#define SENSOR_MAX_DEVICES       2

// ===== WiFi Configuration =====
#define WIFI_MAX_RETRY       5
#define WIFI_AP_SSID         "ESP32-Cooler-Setup"
#define WIFI_AP_PASS         ""         // Open AP for setup
#define WIFI_AP_IP           "10.1.1.1"
#define WIFI_AP_GW           "10.1.1.1"
#define WIFI_AP_NETMASK      "255.255.255.0"
#define WIFI_AP_MAX_CONN     4

// ===== Reset Button Configuration =====
#define RESET_BUTTON_HOLD_MS 3000     // Button must be held for 3 seconds to reset WiFi

// ===== Peltier Configuration =====
#define PELTIER_VOLTAGE       12.0f   // Peltier voltage in Volts
#define PELTIER_POWER         36.0f   // Peltier power in Watts (12V × 3A = 36W, Marke unbekannt)
#define PELTIER_COST_PER_KWH  0.305f  // Strompreis in Euro pro kWh (30,5 Cent)
#define PELTIER_CURRENT       3.0f    // Peltier current in Amps
#define ENERGY_SAVE_INTERVAL_MS 900000  // Save energy data every 15 minutes (NVS protection)

// ===== NVS Keys =====
#define NVS_NAMESPACE        "storage"
#define NVS_KEY_WIFI_SSID    "wifi_ssid"
#define NVS_KEY_WIFI_PASS    "wifi_pass"
#define NVS_KEY_TEMP_ON      "temp_on"
#define NVS_KEY_TEMP_OFF     "temp_off"
#define NVS_KEY_TEMP_MAX     "temp_max"
#define NVS_KEY_TEMP_TARGET  "temp_target"
#define NVS_KEY_ENERGY_WH    "energy_wh"
#define NVS_KEY_ENERGY_DAY   "energy_day"
#define NVS_KEY_ENERGY_WEEK  "energy_week"
#define NVS_KEY_ENERGY_MONTH "energy_month"
#define NVS_KEY_LAST_DATE    "last_date"
#define NVS_KEY_LAST_WEEK    "last_week"
#define NVS_KEY_LAST_MONTH   "last_month"
#define NVS_KEY_SCHED_MO_ON  "sched_mo_on"
#define NVS_KEY_SCHED_MO_OFF "sched_mo_off"
#define NVS_KEY_SCHED_DI_ON  "sched_di_on"
#define NVS_KEY_SCHED_DI_OFF "sched_di_off"
#define NVS_KEY_SCHED_MI_ON  "sched_mi_on"
#define NVS_KEY_SCHED_MI_OFF "sched_mi_off"
#define NVS_KEY_SCHED_DO_ON  "sched_do_on"
#define NVS_KEY_SCHED_DO_OFF "sched_do_off"
#define NVS_KEY_SCHED_FR_ON  "sched_fr_on"
#define NVS_KEY_SCHED_FR_OFF "sched_fr_off"
#define NVS_KEY_SCHED_SA_ON  "sched_sa_on"
#define NVS_KEY_SCHED_SA_OFF "sched_sa_off"
#define NVS_KEY_SCHED_SO_ON  "sched_so_on"
#define NVS_KEY_SCHED_SO_OFF "sched_so_off"
#define NVS_KEY_DATA_LOG_INTERVAL "data_log_interval"
#define NVS_KEY_PELTIER_PWM_PERIOD "pwm_period"
#define NVS_KEY_PELTIER_PWM_DUTY   "pwm_duty"
#define NVS_KEY_GRAPH_DATA         "graph_data"
#define NVS_KEY_AUTO_DUTY_EN       "autoduty_en"
#define NVS_KEY_AUTO_DUTY_DUTY     "autoduty_duty"
#define NVS_KEY_AUTO_DUTY_CYCLE    "autoduty_cycle"

// ===== Task Priorities =====
#define TASK_PRIO_SENSOR     5
#define TASK_PRIO_FAN        4
#define TASK_PRIO_PELTIER    4
#define TASK_PRIO_SCHEDULER  3
#define TASK_PRIO_WEBSERVER  2
#define TASK_PRIO_WIFI       3

// ===== Task Stack Sizes =====
#define TASK_STACK_SENSOR    4096
#define TASK_STACK_FAN       3072
#define TASK_STACK_PELTIER   3072
#define TASK_STACK_SCHEDULER 3072
#define TASK_STACK_WEBSERVER 8192
#define TASK_STACK_WIFI      4096
