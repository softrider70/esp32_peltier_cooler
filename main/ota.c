#include "ota.h"
#include "config.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_app_format.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ota";

static ota_status_t s_status = OTA_IDLE;
static char s_error[128] = {0};
static char s_ota_url[OTA_URL_MAX_LEN] = {0};

#define OTA_BUF_SIZE 4096

void ota_init(void) {
    // Mark current firmware as valid (cancels rollback timer)
    // This is called after system has been running stable for a few seconds
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "First boot after OTA — marking firmware as VALID");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // Load OTA URL from NVS
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(s_ota_url);
        if (nvs_get_str(handle, NVS_KEY_OTA_URL, s_ota_url, &len) != ESP_OK) {
            strncpy(s_ota_url, OTA_DEFAULT_URL, sizeof(s_ota_url) - 1);
        }
        nvs_close(handle);
    } else {
        strncpy(s_ota_url, OTA_DEFAULT_URL, sizeof(s_ota_url) - 1);
    }

    ESP_LOGI(TAG, "OTA initialized, URL: %s", s_ota_url);
    ESP_LOGI(TAG, "Running partition: %s", running->label);
}

static void ota_task(void *pvParameters) {
    char *url = (char *)pvParameters;

    ESP_LOGI(TAG, "OTA update starting from: %s", url);
    s_status = OTA_IN_PROGRESS;
    s_error[0] = '\0';

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 30000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        snprintf(s_error, sizeof(s_error), "HTTP client init failed");
        s_status = OTA_FAILED;
        ESP_LOGE(TAG, "%s", s_error);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "HTTP open failed: %s", esp_err_to_name(err));
        s_status = OTA_FAILED;
        ESP_LOGE(TAG, "%s", s_error);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    if (status_code != 200) {
        snprintf(s_error, sizeof(s_error), "HTTP %d", status_code);
        s_status = OTA_FAILED;
        ESP_LOGE(TAG, "Server returned %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Firmware size: %d bytes", content_length);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        snprintf(s_error, sizeof(s_error), "No OTA partition found");
        s_status = OTA_FAILED;
        ESP_LOGE(TAG, "%s", s_error);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Writing to partition: %s", update_partition->label);

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "OTA begin failed: %s", esp_err_to_name(err));
        s_status = OTA_FAILED;
        ESP_LOGE(TAG, "%s", s_error);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    char *buf = malloc(OTA_BUF_SIZE);
    if (!buf) {
        snprintf(s_error, sizeof(s_error), "Out of memory");
        s_status = OTA_FAILED;
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    int total_read = 0;
    while (1) {
        int read_len = esp_http_client_read(client, buf, OTA_BUF_SIZE);
        if (read_len < 0) {
            snprintf(s_error, sizeof(s_error), "Read error at %d bytes", total_read);
            s_status = OTA_FAILED;
            ESP_LOGE(TAG, "%s", s_error);
            break;
        }
        if (read_len == 0) {
            break;  // Done
        }

        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            snprintf(s_error, sizeof(s_error), "Write failed: %s", esp_err_to_name(err));
            s_status = OTA_FAILED;
            ESP_LOGE(TAG, "%s", s_error);
            break;
        }

        total_read += read_len;
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (s_status == OTA_FAILED) {
        esp_ota_abort(ota_handle);
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "OTA end failed: %s", esp_err_to_name(err));
        s_status = OTA_FAILED;
        ESP_LOGE(TAG, "%s", s_error);
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "Set boot partition failed: %s", esp_err_to_name(err));
        s_status = OTA_FAILED;
        ESP_LOGE(TAG, "%s", s_error);
        vTaskDelete(NULL);
        return;
    }

    s_status = OTA_SUCCESS;
    ESP_LOGI(TAG, "OTA success! Wrote %d bytes to %s. Rebooting in 2s...",
             total_read, update_partition->label);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    vTaskDelete(NULL);
}

void ota_start_update(const char *url) {
    if (s_status == OTA_IN_PROGRESS) {
        return;  // Already running
    }

    // Use stored URL if none provided
    const char *target = (url && strlen(url) > 0) ? url : s_ota_url;

    // Copy URL to static buffer for task (task outlives caller stack)
    static char s_task_url[OTA_URL_MAX_LEN];
    strncpy(s_task_url, target, sizeof(s_task_url) - 1);
    s_task_url[sizeof(s_task_url) - 1] = '\0';

    xTaskCreate(ota_task, "ota_update", 8192, s_task_url, 3, NULL);
}

ota_status_t ota_get_status(void) {
    return s_status;
}

const char* ota_get_error(void) {
    return s_error;
}

const char* ota_get_url(void) {
    return s_ota_url;
}

void ota_set_url(const char *url) {
    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);
    s_ota_url[sizeof(s_ota_url) - 1] = '\0';

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, NVS_KEY_OTA_URL, s_ota_url);
        nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "OTA URL updated: %s", s_ota_url);
}
