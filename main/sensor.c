#include "sensor.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "rom/ets_sys.h"
#include <string.h>

static const char *TAG = "sensor";

// ===== OneWire Protocol (bit-bang) =====
static SemaphoreHandle_t s_data_mutex;
static sensor_data_t s_sensor_data = {0};

// DS18B20 ROM addresses (discovered at init)
static uint8_t s_rom_indoor[8];
static uint8_t s_rom_heatsink[8];
static int s_sensor_count = 0;

// Error handling
static int s_error_count = 0;
static bool s_emergency_mode = false;

// OneWire timing (microseconds)
static inline void ow_delay_us(uint32_t us) {
    ets_delay_us(us);
}

static void ow_pin_output(void) {
    gpio_set_direction(GPIO_ONEWIRE_BUS, GPIO_MODE_OUTPUT_OD);
}

static void ow_pin_input(void) {
    gpio_set_direction(GPIO_ONEWIRE_BUS, GPIO_MODE_INPUT);
}

static void ow_write_low(void) {
    gpio_set_level(GPIO_ONEWIRE_BUS, 0);
}

static void ow_write_high(void) {
    gpio_set_level(GPIO_ONEWIRE_BUS, 1);
}

static int ow_read_pin(void) {
    return gpio_get_level(GPIO_ONEWIRE_BUS);
}

static bool ow_reset(void) {
    ow_pin_output();
    ow_write_low();
    ow_delay_us(480);
    ow_pin_input();
    ow_delay_us(70);
    bool presence = (ow_read_pin() == 0);
    ow_delay_us(410);
    return presence;
}

static void ow_write_bit(uint8_t bit) {
    ow_pin_output();
    ow_write_low();
    if (bit) {
        ow_delay_us(6);
        ow_write_high();
        ow_delay_us(64);
    } else {
        ow_delay_us(60);
        ow_write_high();
        ow_delay_us(10);
    }
}

static uint8_t ow_read_bit(void) {
    ow_pin_output();
    ow_write_low();
    ow_delay_us(6);
    ow_pin_input();
    ow_delay_us(9);
    uint8_t bit = ow_read_pin();
    ow_delay_us(55);
    return bit;
}

static void ow_write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        ow_write_bit((byte >> i) & 0x01);
    }
}

static uint8_t ow_read_byte(void) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (ow_read_bit() << i);
    }
    return byte;
}

// ===== DS18B20 Commands =====
#define DS18B20_CMD_CONVERT     0x44
#define DS18B20_CMD_READ_SCRATCH 0xBE
#define DS18B20_CMD_MATCH_ROM   0x55
#define DS18B20_CMD_SEARCH_ROM  0xF0
#define DS18B20_CMD_SKIP_ROM    0xCC

static uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

// Simple ROM search (finds up to 2 devices)
static int ow_search_rom(uint8_t roms[][8], int max_devices) {
    // Simplified: use SEARCH ROM algorithm
    int found = 0;
    uint8_t last_discrepancy = 0;
    uint8_t rom[8] = {0};

    for (int dev = 0; dev < max_devices; dev++) {
        if (!ow_reset()) break;

        ow_write_byte(DS18B20_CMD_SEARCH_ROM);
        uint8_t new_discrepancy = 0;

        for (int bit_idx = 1; bit_idx <= 64; bit_idx++) {
            uint8_t id_bit = ow_read_bit();
            uint8_t cmp_bit = ow_read_bit();

            if (id_bit && cmp_bit) {
                // No devices responding
                return found;
            }

            uint8_t search_dir;
            if (id_bit != cmp_bit) {
                search_dir = id_bit;
            } else {
                // Conflict
                if (bit_idx == last_discrepancy) {
                    search_dir = 1;
                } else if (bit_idx > last_discrepancy) {
                    search_dir = 0;
                    new_discrepancy = bit_idx;
                } else {
                    int byte_idx = (bit_idx - 1) / 8;
                    int bit_pos = (bit_idx - 1) % 8;
                    search_dir = (rom[byte_idx] >> bit_pos) & 0x01;
                    if (search_dir == 0) {
                        new_discrepancy = bit_idx;
                    }
                }
            }

            int byte_idx = (bit_idx - 1) / 8;
            int bit_pos = (bit_idx - 1) % 8;
            if (search_dir) {
                rom[byte_idx] |= (1 << bit_pos);
            } else {
                rom[byte_idx] &= ~(1 << bit_pos);
            }
            ow_write_bit(search_dir);
        }

        if (crc8(rom, 7) == rom[7]) {
            memcpy(roms[found], rom, 8);
            found++;
        }

        last_discrepancy = new_discrepancy;
        if (last_discrepancy == 0) break;
    }
    return found;
}

static float ds18b20_read_temp(const uint8_t *rom) {
    if (!ow_reset()) return -127.0f;

    ow_write_byte(DS18B20_CMD_MATCH_ROM);
    for (int i = 0; i < 8; i++) {
        ow_write_byte(rom[i]);
    }
    ow_write_byte(DS18B20_CMD_CONVERT);

    // Wait for conversion (1000ms for reliable conversion)
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!ow_reset()) return -127.0f;

    ow_write_byte(DS18B20_CMD_MATCH_ROM);
    for (int i = 0; i < 8; i++) {
        ow_write_byte(rom[i]);
    }
    ow_write_byte(DS18B20_CMD_READ_SCRATCH);

    uint8_t scratch[9];
    for (int i = 0; i < 9; i++) {
        scratch[i] = ow_read_byte();
    }

    if (crc8(scratch, 8) != scratch[8]) {
        return -127.0f;  // CRC error
    }

    int16_t raw = (scratch[1] << 8) | scratch[0];
    return (float)raw / 16.0f;
}

// ===== Public API =====

void sensor_init(void) {
    ESP_LOGI(TAG, "=== sensor_init() START ===");
    
    s_data_mutex = xSemaphoreCreateMutex();
    if (s_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_ONEWIRE_BUS),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG, "GPIO %d configured as OneWire (open-drain, pull-up)", GPIO_ONEWIRE_BUS);

    // Discover sensors
    uint8_t roms[SENSOR_MAX_DEVICES][8];
    ESP_LOGI(TAG, "Starting ROM search for DS18B20 sensors...");
    s_sensor_count = ow_search_rom(roms, SENSOR_MAX_DEVICES);
    ESP_LOGI(TAG, "ROM search completed, found %d sensor(s)", s_sensor_count);

    ESP_LOGI(TAG, "Found %d DS18B20 sensor(s)", s_sensor_count);

    if (s_sensor_count >= 1) {
        memcpy(s_rom_indoor, roms[0], 8);
        ESP_LOGI(TAG, "Sensor 0 (indoor): %02X%02X%02X%02X%02X%02X%02X%02X",
                 roms[0][0], roms[0][1], roms[0][2], roms[0][3],
                 roms[0][4], roms[0][5], roms[0][6], roms[0][7]);
    }
    if (s_sensor_count >= 2) {
        memcpy(s_rom_heatsink, roms[1], 8);
        ESP_LOGI(TAG, "Sensor 1 (heatsink): %02X%02X%02X%02X%02X%02X%02X%02X",
                 roms[1][0], roms[1][1], roms[1][2], roms[1][3],
                 roms[1][4], roms[1][5], roms[1][6], roms[1][7]);
    }
    
    ESP_LOGI(TAG, "=== sensor_init() COMPLETE ===");
}

sensor_data_t sensor_get_data(void) {
    sensor_data_t data;
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    data = s_sensor_data;
    xSemaphoreGive(s_data_mutex);
    return data;
}

bool sensor_get_emergency_mode(void) {
    return s_emergency_mode;
}

void task_sensor(void *pvParameters) {
    (void)pvParameters;
    ESP_LOGI(TAG, "=== task_sensor() STARTED ===");
    ESP_LOGI(TAG, "Sensor count: %d, Interval: %dms", s_sensor_count, SENSOR_READ_INTERVAL_MS);

    while (1) {
        sensor_data_t new_data = {0};
        bool read_error = false;

        // Real sensor reading
        if (s_sensor_count >= 1) {
            float t = ds18b20_read_temp(s_rom_indoor);
            if (t > -126.0f) {
                new_data.temp_indoor = t;
                new_data.indoor_valid = true;
            } else {
                read_error = true;
                // Keep previous value on error
                new_data.temp_indoor = s_sensor_data.temp_indoor;
                new_data.indoor_valid = s_sensor_data.indoor_valid;
            }
        }

        if (s_sensor_count >= 2) {
            float t = ds18b20_read_temp(s_rom_heatsink);
            if (t > -126.0f) {
                new_data.temp_heatsink = t;
                new_data.heatsink_valid = true;
            } else {
                read_error = true;
                // Keep previous value on error
                new_data.temp_heatsink = s_sensor_data.temp_heatsink;
                new_data.heatsink_valid = s_sensor_data.heatsink_valid;
            }
        }

        // Error counting and emergency mode
        if (read_error) {
            s_error_count++;
            ESP_LOGW(TAG, "Sensor read error #%d", s_error_count);
            
            if (s_error_count >= 5) {
                s_emergency_mode = true;
                ESP_LOGE(TAG, "EMERGENCY MODE ACTIVATED: 5 consecutive errors!");
            }
        } else {
            s_error_count = 0;
            if (s_emergency_mode) {
                s_emergency_mode = false;
                ESP_LOGI(TAG, "EMERGENCY MODE DEACTIVATED: sensors recovered");
            }
        }

        xSemaphoreTake(s_data_mutex, portMAX_DELAY);
        s_sensor_data = new_data;
        xSemaphoreGive(s_data_mutex);

        ESP_LOGI(TAG, "Read: Indoor=%.1f°C%s, Heatsink=%.1f°C%s",
                 new_data.temp_indoor, new_data.indoor_valid ? "" : "(invalid)",
                 new_data.temp_heatsink, new_data.heatsink_valid ? "" : "(invalid)");

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}
