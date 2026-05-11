#pragma once

// ===== Build Information =====
#define BUILD_NUMBER        74      // Build counter - increment with each flash

// ===== GPIO Configuration (ESP32-D Board D2-D35) =====
#define GPIO_FAN_PWM        5       // Noctua 4-pin PWM signal (D5/GPIO5)
#define GPIO_FAN_TACHO      18      // Noctua 4-pin tachometer input (D18/GPIO18)
#define GPIO_ONEWIRE_BUS    4       // DS18B20 OneWire data line (D4/GPIO4)
#define GPIO_PELTIER        16      // MOSFET gate for Peltier element (D16/GPIO16)
#define GPIO_RESET_BUTTON   0       // BOOT/RESET button (GPIO0) for WiFi reset

// ===== PWM Configuration (NPN Transistor Inverter) =====
#define FAN_PWM_FREQ_HZ     25000   // Noctua spec: 25 kHz PWM
#define FAN_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_PWM_TIMER       LEDC_TIMER_0
#define FAN_PWM_RESOLUTION  LEDC_TIMER_8_BIT  // 0-255
#define FAN_PWM_INVERTED    true    // PWM invertierung aktiviert (Hardware invertiert)

// ===== RPM Calibration =====
#define RPM_CALIBRATION_FACTOR 0.567f  // Scale factor: 1700 target / 3000 measured

// ===== Temperature Thresholds =====
#define TEMP_PELTIER_ON_DEFAULT     25.0f   // Peltier ON when indoor above this
#define TEMP_PELTIER_OFF_DEFAULT    22.0f   // Peltier OFF when indoor below this
#define TEMP_HEATSINK_MAX           60.0f   // Max heatsink temp (safety cutoff)
#define TEMP_HEATSINK_TARGET        45.0f   // PID target for heatsink

// ===== PID Defaults =====
#define PID_KP_DEFAULT      2.0f    // Scaled by 20 in code → effective 40
#define PID_KI_DEFAULT      0.2f    // Scaled by 20 in code → effective 4
#define PID_KD_DEFAULT      1.0f    // Scaled by 20 in code → effective 20
#define PID_OUTPUT_MIN      0.0f    // Fan off
#define PID_OUTPUT_MAX      255.0f  // Fan full speed
#define PID_SAMPLE_TIME_MS  1000    // 1 second

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

// ===== Energy Consumption Configuration =====
#define PELTIER_VOLTAGE       12.0f   // Peltier voltage in Volts
#define PELTIER_CURRENT       3.0f    // Peltier current in Amps
#define PELTIER_POWER         (PELTIER_VOLTAGE * PELTIER_CURRENT)  // 36W
#define ENERGY_SAVE_INTERVAL_MS 900000  // Save energy data every 15 minutes (NVS protection)

// ===== NVS Keys =====
#define NVS_NAMESPACE        "cooler_cfg"
#define NVS_KEY_WIFI_SSID    "wifi_ssid"
#define NVS_KEY_WIFI_PASS    "wifi_pass"
#define NVS_KEY_TEMP_ON      "temp_on"
#define NVS_KEY_TEMP_OFF     "temp_off"
#define NVS_KEY_TEMP_MAX     "temp_max"
#define NVS_KEY_TEMP_TARGET  "temp_tgt"
#define NVS_KEY_PID_KP       "pid_kp"
#define NVS_KEY_PID_KI       "pid_ki"
#define NVS_KEY_PID_KD       "pid_kd"
#define NVS_KEY_SCHED_MO_ON  "sch_mo_on"   // Monday on (minutes from midnight)
#define NVS_KEY_SCHED_MO_OFF "sch_mo_off"  // Monday off
#define NVS_KEY_SCHED_DI_ON  "sch_di_on"   // Tuesday on
#define NVS_KEY_SCHED_DI_OFF "sch_di_off"  // Tuesday off
#define NVS_KEY_SCHED_MI_ON  "sch_mi_on"   // Wednesday on
#define NVS_KEY_SCHED_MI_OFF "sch_mi_off"  // Wednesday off
#define NVS_KEY_SCHED_DO_ON  "sch_do_on"   // Thursday on
#define NVS_KEY_SCHED_DO_OFF "sch_do_off"  // Thursday off
#define NVS_KEY_SCHED_FR_ON  "sch_fr_on"   // Friday on
#define NVS_KEY_SCHED_FR_OFF "sch_fr_off"  // Friday off
#define NVS_KEY_SCHED_SA_ON  "sch_sa_on"   // Saturday on
#define NVS_KEY_SCHED_SA_OFF "sch_sa_off"  // Saturday off
#define NVS_KEY_SCHED_SO_ON  "sch_so_on"   // Sunday on
#define NVS_KEY_SCHED_SO_OFF  "sch_so_off"
#define NVS_KEY_DATA_LOG_INTERVAL "data_log_interval"
#define NVS_KEY_ENERGY_WH     "energy_wh"  // Total energy in Wh
#define NVS_KEY_ENERGY_DAY    "energy_day"  // Energy today in Wh
#define NVS_KEY_ENERGY_WEEK   "energy_week"  // Energy this week in Wh
#define NVS_KEY_ENERGY_MONTH  "energy_month"  // Energy this month in Wh
#define NVS_KEY_LAST_DATE     "last_date"  // Last saved date (YYYYMMDD)
#define NVS_KEY_GRAPH_DATA   "graph_data"  // Graph data points

// ===== Task Priorities =====
#define TASK_PRIO_SENSOR     5
#define TASK_PRIO_PID        4
#define TASK_PRIO_PELTIER    4
#define TASK_PRIO_SCHEDULER  3
#define TASK_PRIO_WEBSERVER  2
#define TASK_PRIO_WIFI       3

// ===== Task Stack Sizes =====
#define TASK_STACK_SENSOR    4096
#define TASK_STACK_PID       3072
#define TASK_STACK_PELTIER   3072
#define TASK_STACK_SCHEDULER 3072
#define TASK_STACK_WEBSERVER 8192
#define TASK_STACK_WIFI      4096
