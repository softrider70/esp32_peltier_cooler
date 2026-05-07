#pragma once

// ===== GPIO Configuration =====
#define GPIO_FAN_PWM        25      // Noctua 4-pin PWM signal
#define GPIO_FAN_TACHO      26      // Noctua 4-pin tachometer input
#define GPIO_ONEWIRE_BUS    27      // DS18B20 OneWire data line (both sensors)
#define GPIO_PELTIER        14      // MOSFET gate for Peltier element (digital on/off)

// ===== CYD Display GPIO Configuration =====
#define CYD_TFT_CS         15      // Display Chip Select
#define CYD_TFT_SCK        14      // SPI Clock (geteilt mit Peltier!)
#define CYD_TFT_MOSI       13      // SPI MOSI
#define CYD_TFT_MISO       12      // SPI MISO
#define CYD_TFT_DC         2       // Data/Command
#define CYD_TFT_BL         27      // Backlight PWM (geteilt mit OneWire!)
#define CYD_TOUCH_SDA      33      // Touch I2C Data
#define CYD_TOUCH_SCL      32      // Touch I2C Clock
#define CYD_TOUCH_INT      36      // Touch Interrupt

// ===== PWM Configuration =====
#define FAN_PWM_FREQ_HZ     25000   // Noctua spec: 25 kHz PWM
#define FAN_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_PWM_TIMER       LEDC_TIMER_0
#define FAN_PWM_RESOLUTION  LEDC_TIMER_8_BIT  // 0-255

// ===== Temperature Thresholds =====
#define TEMP_PELTIER_ON_DEFAULT     25.0f   // Peltier ON when indoor above this
#define TEMP_PELTIER_OFF_DEFAULT    22.0f   // Peltier OFF when indoor below this
#define TEMP_HEATSINK_MAX           60.0f   // Max heatsink temp (safety cutoff)
#define TEMP_HEATSINK_TARGET        45.0f   // PID target for heatsink

// ===== PID Defaults =====
#define PID_KP_DEFAULT      2.0f
#define PID_KI_DEFAULT      0.5f
#define PID_KD_DEFAULT      1.0f
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
#define NVS_KEY_SCHED_WD_ON  "sch_wd_on"   // Weekday on  (minutes from midnight)
#define NVS_KEY_SCHED_WD_OFF "sch_wd_off"  // Weekday off
#define NVS_KEY_SCHED_WE_ON  "sch_we_on"   // Weekend on
#define NVS_KEY_SCHED_WE_OFF "sch_we_off"  // Weekend off

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
