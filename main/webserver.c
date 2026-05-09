#include "webserver.h"
#include "config.h"
#include "sensor.h"
#include "fan.h"
#include "peltier.h"
#include "scheduler.h"
#include "nvs_config.h"
#include "wifi.h"
#include "ota.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "webserver";
static httpd_handle_t s_server = NULL;
static TaskHandle_t s_dns_task = NULL;

// Embedded HTML files (linked via CMake EMBED_FILES)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t captive_html_start[] asm("_binary_captive_html_start");
extern const uint8_t captive_html_end[]   asm("_binary_captive_html_end");

// ===== Handlers =====

static esp_err_t handler_index(httpd_req_t *req) {
    if (wifi_get_mode() == WIFI_MODE_AP) {
        // In AP mode, serve captive portal
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, (const char *)captive_html_start,
                        captive_html_end - captive_html_start);
    } else {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, (const char *)index_html_start,
                        index_html_end - index_html_start);
    }
    return ESP_OK;
}

static esp_err_t handler_api_status(httpd_req_t *req) {
    sensor_data_t sd = sensor_get_data();
    app_config_t *cfg = nvs_config_get();

    char buf[768];
    // Get current time
    char time_str[32] = "No time";
    bool time_synced = false;
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year >= (2020 - 1900)) {
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        time_synced = true;
    }

    // Get OTA URL
    const char *ota_url = ota_get_url();

    int len = snprintf(buf, sizeof(buf),
        "{\"indoor\":%.1f,\"heatsink\":%.1f,"
        "\"indoor_valid\":%s,\"heatsink_valid\":%s,"
        "\"fan_duty\":%d,\"fan_rpm\":%d,\"peltier_on\":%s,"
        "\"active\":%s,\"time_synced\":%s,\"time\":\"%s\","
        "\"temp_on\":%.1f,\"temp_off\":%.1f,"
        "\"temp_max\":%.1f,\"temp_target\":%.1f,"
        "\"pid_kp\":%.1f,\"pid_ki\":%.1f,\"pid_kd\":%.1f,"
        "\"sched_wd_on\":%d,\"sched_wd_off\":%d,"
        "\"sched_we_on\":%d,\"sched_we_off\":%d,"
        "\"wifi_mode\":\"%s\",\"ota_url\":\"%s\","
        "\"build\":%d}",
        sd.temp_indoor, sd.temp_heatsink,
        sd.indoor_valid ? "true" : "false",
        sd.heatsink_valid ? "true" : "false",
        fan_get_duty(), fan_get_rpm(), peltier_is_on() ? "true" : "false",
        scheduler_is_active() ? "true" : "false",
        time_synced ? "true" : "false", time_str,
        cfg->temp_peltier_on, cfg->temp_peltier_off,
        cfg->temp_heatsink_max, cfg->temp_heatsink_target,
        cfg->pid_kp, cfg->pid_ki, cfg->pid_kd,
        cfg->sched_wd_on, cfg->sched_wd_off,
        cfg->sched_we_on, cfg->sched_we_off,
        wifi_is_connected() ? "STA" : "AP",
        ota_url, BUILD_NUMBER);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

static esp_err_t handler_api_config(httpd_req_t *req) {
    char buf[512];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    // Simple key=value parsing (URL-encoded form data)
    app_config_t *cfg = nvs_config_get();

    char value[65];

    if (httpd_query_key_value(buf, "temp_on", value, sizeof(value)) == ESP_OK)
        cfg->temp_peltier_on = strtof(value, NULL);
    if (httpd_query_key_value(buf, "temp_off", value, sizeof(value)) == ESP_OK)
        cfg->temp_peltier_off = strtof(value, NULL);
    if (httpd_query_key_value(buf, "temp_max", value, sizeof(value)) == ESP_OK)
        cfg->temp_heatsink_max = strtof(value, NULL);
    if (httpd_query_key_value(buf, "temp_target", value, sizeof(value)) == ESP_OK)
        cfg->temp_heatsink_target = strtof(value, NULL);
    if (httpd_query_key_value(buf, "pid_kp", value, sizeof(value)) == ESP_OK)
        cfg->pid_kp = strtof(value, NULL);
    if (httpd_query_key_value(buf, "pid_ki", value, sizeof(value)) == ESP_OK)
        cfg->pid_ki = strtof(value, NULL);
    if (httpd_query_key_value(buf, "pid_kd", value, sizeof(value)) == ESP_OK)
        cfg->pid_kd = strtof(value, NULL);
    if (httpd_query_key_value(buf, "sched_wd_on", value, sizeof(value)) == ESP_OK)
        cfg->sched_wd_on = (uint16_t)atoi(value);
    if (httpd_query_key_value(buf, "sched_wd_off", value, sizeof(value)) == ESP_OK)
        cfg->sched_wd_off = (uint16_t)atoi(value);
    if (httpd_query_key_value(buf, "sched_we_on", value, sizeof(value)) == ESP_OK)
        cfg->sched_we_on = (uint16_t)atoi(value);
    if (httpd_query_key_value(buf, "sched_we_off", value, sizeof(value)) == ESP_OK)
        cfg->sched_we_off = (uint16_t)atoi(value);

    nvs_config_save();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t handler_api_sensor_sim(httpd_req_t *req) {
    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    // Parse simulation values
    char value[65];
    float indoor_temp = -999.0f;
    float heatsink_temp = -999.0f;

    if (httpd_query_key_value(buf, "indoor", value, sizeof(value)) == ESP_OK)
        indoor_temp = strtof(value, NULL);
    if (httpd_query_key_value(buf, "heatsink", value, sizeof(value)) == ESP_OK)
        heatsink_temp = strtof(value, NULL);

    // Set simulation values
    sensor_set_simulation(indoor_temp, heatsink_temp);

    ESP_LOGI(TAG, "Sensor simulation: indoor=%.1f, heatsink=%.1f", indoor_temp, heatsink_temp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t handler_api_wifi(httpd_req_t *req) {
    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};

    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(buf, "pass", pass, sizeof(pass));

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    nvs_config_set_wifi(ssid, pass);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"msg\":\"Restarting WiFi...\"}");

    // Reconnect after short delay
    vTaskDelay(pdMS_TO_TICKS(1000));
    wifi_reconnect_sta();

    return ESP_OK;
}

// ===== OTA Handlers =====

static esp_err_t handler_api_ota(httpd_req_t *req) {
    char buf[300];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char url[OTA_URL_MAX_LEN] = {0};
    httpd_query_key_value(buf, "url", url, sizeof(url));

    // Save URL if provided
    if (strlen(url) > 0) {
        ota_set_url(url);
    }

    // Start OTA update
    ota_start_update(NULL);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"msg\":\"OTA gestartet\"}");
    return ESP_OK;
}

static esp_err_t handler_api_ota_status(httpd_req_t *req) {
    char buf[256];
    const char *status_str;
    switch (ota_get_status()) {
        case OTA_IN_PROGRESS: status_str = "in_progress"; break;
        case OTA_SUCCESS:     status_str = "success"; break;
        case OTA_FAILED:      status_str = "failed"; break;
        default:              status_str = "idle"; break;
    }

    int len = snprintf(buf, sizeof(buf),
        "{\"status\":\"%s\",\"error\":\"%s\",\"url\":\"%s\"}",
        status_str, ota_get_error(), ota_get_url());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

// Catch-all handler for captive portal redirect
static esp_err_t handler_captive_redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://10.1.1.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ===== DNS Captive Portal =====

static void dns_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    uint8_t rx_buf[512];
    uint8_t tx_buf[512];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                          (struct sockaddr *)&client_addr, &addr_len);
        if (len < 12) continue;

        // Build minimal DNS response pointing to 10.1.1.1
        memcpy(tx_buf, rx_buf, len);
        tx_buf[2] = 0x81; tx_buf[3] = 0x80;  // Response flags
        tx_buf[6] = 0x00; tx_buf[7] = 0x01;  // 1 answer

        int offset = len;
        // Pointer to question name
        tx_buf[offset++] = 0xC0;
        tx_buf[offset++] = 0x0C;
        // Type A
        tx_buf[offset++] = 0x00; tx_buf[offset++] = 0x01;
        // Class IN
        tx_buf[offset++] = 0x00; tx_buf[offset++] = 0x01;
        // TTL 60s
        tx_buf[offset++] = 0x00; tx_buf[offset++] = 0x00;
        tx_buf[offset++] = 0x00; tx_buf[offset++] = 0x3C;
        // Data length 4
        tx_buf[offset++] = 0x00; tx_buf[offset++] = 0x04;
        // IP 10.1.1.1
        tx_buf[offset++] = 10;
        tx_buf[offset++] = 1;
        tx_buf[offset++] = 1;
        tx_buf[offset++] = 1;

        sendto(sock, tx_buf, offset, 0,
               (struct sockaddr *)&client_addr, addr_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

// ===== Public API =====

void webserver_init(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    // Register URI handlers
    httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = handler_index
    };
    httpd_uri_t uri_api_status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = handler_api_status
    };
    httpd_uri_t uri_api_config = {
        .uri = "/api/config", .method = HTTP_POST, .handler = handler_api_config
    };
    httpd_uri_t uri_api_sensor_sim = {
        .uri = "/api/sensor/sim", .method = HTTP_POST, .handler = handler_api_sensor_sim
    };
    httpd_uri_t uri_api_wifi = {
        .uri = "/api/wifi", .method = HTTP_POST, .handler = handler_api_wifi
    };
    httpd_uri_t uri_api_ota = {
        .uri = "/api/ota", .method = HTTP_POST, .handler = handler_api_ota
    };
    httpd_uri_t uri_api_ota_status = {
        .uri = "/api/ota/status", .method = HTTP_GET, .handler = handler_api_ota_status
    };
    // Catch-all for captive portal (must be last)
    httpd_uri_t uri_catchall = {
        .uri = "/*", .method = HTTP_GET, .handler = handler_captive_redirect
    };

    httpd_register_uri_handler(s_server, &uri_index);
    httpd_register_uri_handler(s_server, &uri_api_status);
    httpd_register_uri_handler(s_server, &uri_api_config);
    httpd_register_uri_handler(s_server, &uri_api_sensor_sim);
    httpd_register_uri_handler(s_server, &uri_api_wifi);
    httpd_register_uri_handler(s_server, &uri_api_ota);
    httpd_register_uri_handler(s_server, &uri_api_ota_status);
    httpd_register_uri_handler(s_server, &uri_catchall);

    ESP_LOGI(TAG, "HTTP server started");

    // Start captive DNS if in AP mode
    if (wifi_get_mode() == WIFI_MODE_AP) {
        webserver_start_captive_dns();
    }
}

void webserver_start_captive_dns(void) {
    if (s_dns_task == NULL) {
        xTaskCreate(dns_task, "dns_captive", 4096, NULL, 2, &s_dns_task);
        ESP_LOGI(TAG, "Captive DNS started");
    }
}

void webserver_stop_captive_dns(void) {
    if (s_dns_task != NULL) {
        vTaskDelete(s_dns_task);
        s_dns_task = NULL;
        ESP_LOGI(TAG, "Captive DNS stopped");
    }
}
