#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "OTA.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "DogDogOTA";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define DOGDOG_OTA_SKIP_ONCE_MARKER 0xdd075a11U
#define DOGDOG_OTA_UPDATE_PENDING_MARKER 0xdd075a12U
#define DOGDOG_OTA_MARKER_XOR 0xffffffffU
#ifndef CONFIG_DOGDOG_OTA_TASK_STACK_SIZE
#define CONFIG_DOGDOG_OTA_TASK_STACK_SIZE 24576
#endif

typedef struct dogdog_ota_rtc_state {
    uint32_t marker;
    uint32_t marker_check;
} dogdog_ota_rtc_state_t;

RTC_NOINIT_ATTR static dogdog_ota_rtc_state_t s_ota_rtc_state;

typedef struct dogdog_ota_context {
    EventGroupHandle_t event_group;
    dogdog_ota_status_cb_t status_cb;
    dogdog_ota_progress_cb_t progress_cb;
    void *user_ctx;
    int retries;
} dogdog_ota_context_t;

typedef struct dogdog_ota_task_args {
    dogdog_ota_config_t config;
    esp_err_t result;
    SemaphoreHandle_t done;
} dogdog_ota_task_args_t;

static esp_netif_t *s_wifi_netif = NULL;
static esp_event_handler_instance_t s_wifi_instance = NULL;
static esp_event_handler_instance_t s_ip_instance = NULL;
static bool s_ota_wifi_shutting_down = false;
static bool s_ota_probe_suppressed_for_boot = false;

static void notify_status(dogdog_ota_context_t *context,
                          dogdog_ota_event_t event)
{
    if (context != NULL && context->status_cb != NULL) {
        context->status_cb(event, context->user_ctx);
    }
}

static void notify_progress(dogdog_ota_context_t *context,
                           const dogdog_ota_progress_t *progress)
{
    if (context != NULL && context->progress_cb != NULL && progress != NULL) {
        context->progress_cb(progress, context->user_ctx);
    }
}

static const char *device_name_from_config(const dogdog_ota_config_t *config)
{
    if (config != NULL && config->device_name != NULL &&
        config->device_name[0] != '\0') {
        return config->device_name;
    }
    return CONFIG_DOGDOG_OTA_FALLBACK_DEVICE_NAME;
}

static esp_err_t build_firmware_url(char *firmware_url,
                                    size_t firmware_url_size,
                                    const char *device_name)
{
    const int url_len = snprintf(firmware_url,
                                 firmware_url_size,
                                 "%s/firmware/%s.bin",
                                 CONFIG_DOGDOG_OTA_BASE_URL,
                                 device_name);
    if (url_len <= 0 || url_len >= (int)firmware_url_size) {
        ESP_LOGE(TAG, "OTA firmware URL too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static bool marker_is_set(uint32_t marker)
{
    return s_ota_rtc_state.marker == marker &&
           s_ota_rtc_state.marker_check == (marker ^ DOGDOG_OTA_MARKER_XOR);
}

static void set_marker(uint32_t marker)
{
    s_ota_rtc_state.marker = marker;
    s_ota_rtc_state.marker_check = marker ^ DOGDOG_OTA_MARKER_XOR;
}

static void clear_marker(void)
{
    s_ota_rtc_state.marker = 0;
    s_ota_rtc_state.marker_check = 0;
}

static bool consume_skip_once_marker(void)
{
    if (!marker_is_set(DOGDOG_OTA_SKIP_ONCE_MARKER)) {
        return false;
    }

    clear_marker();
    ESP_LOGI(TAG, "Skipping OTA probe once after OTA restart marker");
    return true;
}

bool dogdog_ota_update_pending(void)
{
    return marker_is_set(DOGDOG_OTA_UPDATE_PENDING_MARKER);
}

static bool consume_update_pending_marker(void)
{
    if (!marker_is_set(DOGDOG_OTA_UPDATE_PENDING_MARKER)) {
        return false;
    }

    clear_marker();
    ESP_LOGI(TAG, "Resuming pending OTA update");
    return true;
}

static void restart_with_skip_once_marker(void)
{
    set_marker(DOGDOG_OTA_SKIP_ONCE_MARKER);
    s_ota_wifi_shutting_down = true;
    ESP_LOGI(TAG, "Restarting after OTA probe, next boot skips OTA once");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static void restart_with_update_pending_marker(dogdog_ota_context_t *context,
                                               int delay_ms)
{
    set_marker(DOGDOG_OTA_UPDATE_PENDING_MARKER);
    s_ota_wifi_shutting_down = true;
    notify_status(context, DOGDOG_OTA_EVENT_RESTARTING_FOR_UPDATE);
    ESP_LOGI(TAG, "Restarting into OTA-only boot path");
    if (delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    esp_restart();
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    dogdog_ota_context_t *context = (dogdog_ota_context_t *)arg;

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_ota_wifi_shutting_down) {
            ESP_LOGI(TAG, "OTA Wi-Fi disconnected during planned restart");
            return;
        }
        if (context->retries < CONFIG_DOGDOG_OTA_CONNECT_RETRIES) {
            context->retries++;
            ESP_LOGI(TAG,
                     "OTA Wi-Fi disconnected, retry %d/%d",
                     context->retries,
                     CONFIG_DOGDOG_OTA_CONNECT_RETRIES);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(context->event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "OTA Wi-Fi connected, got IP " IPSTR,
                 IP2STR(&event->ip_info.ip));
        context->retries = 0;
        xEventGroupSetBits(context->event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t initialize_wifi(dogdog_ota_context_t *context)
{
    s_ota_wifi_shutting_down = false;

    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "NVS init failed");

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp-netif init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop init failed: %s", esp_err_to_name(err));
        return err;
    }

    context->event_group = xEventGroupCreate();
    if (context->event_group == NULL) {
        ESP_LOGE(TAG, "OTA Wi-Fi event group allocation failed");
        return ESP_ERR_NO_MEM;
    }

    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "default Wi-Fi netif creation failed");
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_config);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM),
                        TAG,
                        "Wi-Fi storage config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA),
                        TAG,
                        "Wi-Fi STA mode failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT,
                            WIFI_EVENT_STA_DISCONNECTED,
                            wifi_event_handler,
                            context,
                            &s_wifi_instance),
                        TAG,
                        "Wi-Fi event handler registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            IP_EVENT,
                            IP_EVENT_STA_GOT_IP,
                            wifi_event_handler,
                            context,
                            &s_ip_instance),
                        TAG,
                        "IP event handler registration failed");

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static void cleanup_wifi(dogdog_ota_context_t *context)
{
    esp_wifi_disconnect();
    esp_wifi_stop();

    if (s_wifi_instance != NULL) {
        esp_event_handler_instance_unregister(WIFI_EVENT,
                                              WIFI_EVENT_STA_DISCONNECTED,
                                              s_wifi_instance);
        s_wifi_instance = NULL;
    }
    if (s_ip_instance != NULL) {
        esp_event_handler_instance_unregister(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              s_ip_instance);
        s_ip_instance = NULL;
    }

    esp_wifi_deinit();

    if (s_wifi_netif != NULL) {
        esp_netif_destroy_default_wifi(s_wifi_netif);
        s_wifi_netif = NULL;
    }

    if (context != NULL && context->event_group != NULL) {
        vEventGroupDelete(context->event_group);
        context->event_group = NULL;
    }
}

static esp_err_t scan_for_ota_network(void)
{
    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t *)CONFIG_DOGDOG_OTA_WIFI_SSID,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    ESP_LOGI(TAG, "Scanning for OTA Wi-Fi '%s'", CONFIG_DOGDOG_OTA_WIFI_SSID);
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA Wi-Fi scan failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA Wi-Fi scan result failed: %s", esp_err_to_name(err));
        return err;
    }

    if (ap_count == 0) {
        ESP_LOGI(TAG, "OTA Wi-Fi not found, continuing normal boot");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "OTA Wi-Fi found");
    return ESP_OK;
}

static esp_err_t connect_to_ota_network(dogdog_ota_context_t *context)
{
    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid,
             sizeof(wifi_config.sta.ssid),
             "%s",
             CONFIG_DOGDOG_OTA_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password,
             sizeof(wifi_config.sta.password),
             "%s",
             CONFIG_DOGDOG_OTA_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
                        TAG,
                        "OTA Wi-Fi config failed");

    xEventGroupClearBits(context->event_group,
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    context->retries = 0;
    notify_status(context, DOGDOG_OTA_EVENT_CONNECTING);

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA Wi-Fi connect failed immediately: %s",
                 esp_err_to_name(err));
        return err;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        context->event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(CONFIG_DOGDOG_OTA_CONNECT_TIMEOUT_MS));

    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "OTA Wi-Fi connection timed out or failed");
    return ESP_ERR_TIMEOUT;
}

static bool same_app_descriptor(const esp_app_desc_t *running,
                                const esp_app_desc_t *incoming)
{
    if (running == NULL || incoming == NULL) {
        return false;
    }

    return memcmp(running->version,
                  incoming->version,
                  sizeof(running->version)) == 0 &&
           memcmp(running->project_name,
                  incoming->project_name,
                  sizeof(running->project_name)) == 0 &&
           memcmp(running->date,
                  incoming->date,
                  sizeof(running->date)) == 0 &&
           memcmp(running->time,
                  incoming->time,
                  sizeof(running->time)) == 0;
}

static int month_number(const char *month)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int i = 0; i < 12; ++i) {
        if (strncmp(month, months[i], 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

static int64_t days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -2 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static int64_t utc_epoch_from_calendar(int year, int month, int day,
                                      int hour, int minute, int second)
{
    const int64_t days = days_from_civil(year, month, day);
    return days * 86400LL + (int64_t)hour * 3600LL + (int64_t)minute * 60LL +
           second;
}

static int64_t local_utc_offset_seconds(void)
{
    return 20 * 60 * 60;
}

static bool app_build_timestamp(const esp_app_desc_t *desc,
                               bool timestamp_is_utc,
                               int64_t *timestamp)
{
    if (desc == NULL || timestamp == NULL) {
        return false;
    }

    char month_name[4] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (sscanf(desc->date, "%3s %d %d", month_name, &day, &year) != 3 ||
        sscanf(desc->time, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }

    const int month = month_number(month_name);
    if (month <= 0 || day <= 0 || day > 31 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        return false;
    }

    int64_t epoch = utc_epoch_from_calendar(year, month, day, hour, minute, second);
    if (!timestamp_is_utc) {
        epoch -= local_utc_offset_seconds();
    }

    *timestamp = epoch;
    return true;
}

static void log_build_utc(const char *label, const esp_app_desc_t *desc)
{
    if (desc == NULL) {
        return;
    }

    int64_t epoch = 0;
    if (!app_build_timestamp(desc, true, &epoch)) {
        return;
    }

    char utc_buf[32] = {0};
    struct tm tm_utc = {0};
    const time_t epoch_time = (time_t)epoch;
    if (gmtime_r(&epoch_time, &tm_utc) == NULL) {
        return;
    }

    if (strftime(utc_buf,
                 sizeof(utc_buf),
                 "%Y-%m-%d %H:%M:%S UTC",
                 &tm_utc) == 0) {
        return;
    }

    ESP_LOGI(TAG, "%s UTC: %s", label, utc_buf);
}

static bool same_project_name(const esp_app_desc_t *running,
                              const esp_app_desc_t *incoming)
{
    if (running == NULL || incoming == NULL) {
        return false;
    }

    return memcmp(running->project_name,
                  incoming->project_name,
                  sizeof(running->project_name)) == 0;
}

static bool should_install_image(const esp_app_desc_t *running,
                                 const esp_app_desc_t *incoming)
{
    if (running == NULL || incoming == NULL) {
        return false;
    }

    if (same_app_descriptor(running, incoming)) {
        ESP_LOGI(TAG, "OTA image is already installed");
        return false;
    }

    if (!same_project_name(running, incoming)) {
        ESP_LOGW(TAG, "OTA image project differs, skipping update");
        return false;
    }

    int64_t running_timestamp = 0;
    int64_t incoming_timestamp = 0;
    if (app_build_timestamp(running, false, &running_timestamp) &&
        app_build_timestamp(incoming, true, &incoming_timestamp)) {
        ESP_LOGI(TAG,
                 "Firmware compare: running_epoch=%lld incoming_epoch=%lld",
                 (long long)running_timestamp,
                 (long long)incoming_timestamp);
        log_build_utc("Running firmware", running);
        log_build_utc("Available firmware", incoming);

        if (incoming_timestamp < running_timestamp) {
            ESP_LOGW(TAG,
                     "OTA image is older than running firmware (UTC compare), skipping update");
            return false;
        }
        if (incoming_timestamp == running_timestamp) {
            ESP_LOGW(TAG,
                     "OTA image build time matches running firmware (UTC compare), skipping update");
            return false;
        }
    }

    return true;
}

static esp_err_t probe_ota_update_available(const char *device_name,
                                            dogdog_ota_context_t *context,
                                            bool *update_available)
{
    if (update_available == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *update_available = false;

    char firmware_url[192];
    esp_err_t err = build_firmware_url(firmware_url,
                                       sizeof(firmware_url),
                                       device_name);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Checking OTA image at %s", firmware_url);

    esp_http_client_config_t http_config = {
        .url = firmware_url,
        .timeout_ms = CONFIG_DOGDOG_OTA_RECV_TIMEOUT_MS,
        .keep_alive_enable = true,
        .buffer_size = 4096,
        .keep_alive_enable = true,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
#if CONFIG_DOGDOG_OTA_ENABLE_PARTIAL_HTTP_DOWNLOAD
        .partial_http_download = true,
        .max_http_request_size = CONFIG_DOGDOG_OTA_HTTP_REQUEST_SIZE,
#endif
    };

    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA probe begin failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t incoming_desc = {0};
    err = esp_https_ota_get_img_desc(ota_handle, &incoming_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA probe image description failed: %s",
                 esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        return err;
    }

    esp_app_desc_t running_desc = {0};
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition != NULL &&
        esp_ota_get_partition_description(running_partition,
                                          &running_desc) == ESP_OK) {
        ESP_LOGI(TAG,
                 "Running firmware: %s %s %s",
                 running_desc.project_name,
                 running_desc.date,
                 running_desc.time);
        ESP_LOGI(TAG,
                 "Available firmware: %s %s %s",
                 incoming_desc.project_name,
                 incoming_desc.date,
                 incoming_desc.time);

        if (!should_install_image(&running_desc, &incoming_desc)) {
            esp_https_ota_abort(ota_handle);
            notify_status(context, DOGDOG_OTA_EVENT_NO_UPDATE);
            return ESP_OK;
        }
    }

    esp_https_ota_abort(ota_handle);
    *update_available = true;
    ESP_LOGI(TAG, "OTA image update is available");
    return ESP_OK;
}

static esp_err_t perform_ota(const char *device_name,
                             dogdog_ota_context_t *context)
{
    char firmware_url[192];
    esp_err_t err = build_firmware_url(firmware_url,
                                       sizeof(firmware_url),
                                       device_name);
    if (err != ESP_OK) {
        return err;
    }

    const size_t total_size = 0;

    ESP_LOGI(TAG, "Starting OTA from %s", firmware_url);
    notify_status(context, DOGDOG_OTA_EVENT_UPDATING);

    esp_http_client_config_t http_config = {
        .url = firmware_url,
        .timeout_ms = CONFIG_DOGDOG_OTA_RECV_TIMEOUT_MS,
        .keep_alive_enable = true,
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
#if CONFIG_DOGDOG_OTA_ENABLE_PARTIAL_HTTP_DOWNLOAD
        .partial_http_download = true,
        .max_http_request_size = CONFIG_DOGDOG_OTA_HTTP_REQUEST_SIZE,
#endif
    };

    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t incoming_desc = {0};
    err = esp_https_ota_get_img_desc(ota_handle, &incoming_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA image description failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        return err;
    }

    esp_app_desc_t running_desc = {0};
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition != NULL &&
        esp_ota_get_partition_description(running_partition,
                                          &running_desc) == ESP_OK) {
        ESP_LOGI(TAG,
                 "Running firmware: %s %s %s",
                 running_desc.project_name,
                 running_desc.date,
                 running_desc.time);
        ESP_LOGI(TAG,
                 "Available firmware: %s %s %s",
                 incoming_desc.project_name,
                 incoming_desc.date,
                 incoming_desc.time);

        if (!should_install_image(&running_desc, &incoming_desc)) {
            esp_https_ota_abort(ota_handle);
            notify_status(context, DOGDOG_OTA_EVENT_NO_UPDATE);
            return ESP_OK;
        }
    }

    size_t last_logged_bytes = 0;
    int last_reported_percent = -1;
    const TickType_t start_ticks = xTaskGetTickCount();
    while (true) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        const size_t bytes_read = esp_https_ota_get_image_len_read(ota_handle);
        if (bytes_read != last_logged_bytes) {
            last_logged_bytes = bytes_read;
            const uint32_t elapsed_ms = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount() - start_ticks);
            const uint32_t bytes_per_second = elapsed_ms > 0 ? (uint32_t)((bytes_read * 1000ULL) / elapsed_ms) : 0;
            ESP_LOGI(TAG,
                     "OTA download in progress: %zu bytes received (%lu B/s)",
                     bytes_read,
                     (unsigned long)bytes_per_second);

            dogdog_ota_progress_t progress = {
                .progress_percent = -1,
                .bytes_received = bytes_read,
                .total_size = total_size,
            };

            if (total_size > 0) {
                progress.progress_percent = (int)((bytes_read * 100ULL) / total_size);
            }

            if (total_size == 0 || progress.progress_percent > last_reported_percent) {
                last_reported_percent = progress.progress_percent > 0 ? progress.progress_percent : 0;
                notify_progress(context, &progress);
            }
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        return err;
    }

    if (!esp_https_ota_is_complete_data_received(ota_handle)) {
        ESP_LOGE(TAG, "OTA image was not completely received");
        esp_https_ota_abort(ota_handle);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "OTA image transfer complete, finalizing update");
    err = esp_https_ota_finish(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "OTA update successful, rebooting");
    restart_with_skip_once_marker();
    return ESP_OK;
}

esp_err_t dogdog_ota_check_and_update_ex(const dogdog_ota_config_t *config)
{
#if !CONFIG_DOGDOG_OTA_ENABLED
    (void)config;
    return ESP_OK;
#else
    if (s_ota_probe_suppressed_for_boot) {
        ESP_LOGI(TAG, "Skipping OTA probe for this boot");
        return ESP_OK;
    }
    if (consume_skip_once_marker()) {
        return ESP_OK;
    }
    const bool resume_pending_update = consume_update_pending_marker();
    const bool restart_before_update =
        !resume_pending_update && config != NULL && config->restart_before_update;
    const int restart_delay_ms =
        config != NULL && config->restart_delay_ms > 0
            ? config->restart_delay_ms
            : 1500;

    dogdog_ota_context_t context = {
        .event_group = NULL,
        .status_cb = config != NULL ? config->status_cb : NULL,
        .progress_cb = config != NULL ? config->progress_cb : NULL,
        .user_ctx = config != NULL ? config->user_ctx : NULL,
        .retries = 0,
    };

    notify_status(&context, DOGDOG_OTA_EVENT_CHECKING);

    esp_err_t err = initialize_wifi(&context);
    if (err != ESP_OK) {
        notify_status(&context, DOGDOG_OTA_EVENT_FAILED);
        cleanup_wifi(&context);
        return err;
    }

    if (!resume_pending_update) {
        err = scan_for_ota_network();
        if (err == ESP_ERR_NOT_FOUND) {
            cleanup_wifi(&context);
            return ESP_OK;
        }
        if (err != ESP_OK) {
            notify_status(&context, DOGDOG_OTA_EVENT_FAILED);
            cleanup_wifi(&context);
            return err;
        }
        notify_status(&context, DOGDOG_OTA_EVENT_WIFI_FOUND);
    }

    err = connect_to_ota_network(&context);
    if (err == ESP_OK) {
        if (restart_before_update) {
            bool update_available = false;
            err = probe_ota_update_available(device_name_from_config(config),
                                             &context,
                                             &update_available);
            if (err == ESP_OK && update_available) {
                notify_status(&context, DOGDOG_OTA_EVENT_UPDATING);
                restart_with_update_pending_marker(&context, restart_delay_ms);
            }
        } else {
            err = perform_ota(device_name_from_config(config), &context);
        }
    }

    if (err != ESP_OK) {
        notify_status(&context, DOGDOG_OTA_EVENT_FAILED);
        ESP_LOGW(TAG, "OTA check failed: %s",
                 esp_err_to_name(err));
    }

    if (resume_pending_update) {
        s_ota_probe_suppressed_for_boot = true;
    }

    cleanup_wifi(&context);
    return err;
#endif
}

esp_err_t dogdog_ota_check_and_update(const char *device_name)
{
    const dogdog_ota_config_t config = {
        .device_name = device_name,
        .status_cb = NULL,
        .progress_cb = NULL,
        .user_ctx = NULL,
        .restart_before_update = false,
        .restart_delay_ms = 0,
    };
    return dogdog_ota_check_and_update_ex(&config);
}

static void dogdog_ota_worker_task(void *arg)
{
    dogdog_ota_task_args_t *args = (dogdog_ota_task_args_t *)arg;
    args->result = dogdog_ota_check_and_update_ex(&args->config);
    xSemaphoreGive(args->done);
    vTaskDelete(NULL);
}

esp_err_t dogdog_ota_check_and_update_in_task(const dogdog_ota_config_t *config,
                                              uint32_t stack_size)
{
    dogdog_ota_task_args_t args = {
        .config = {0},
        .result = ESP_FAIL,
        .done = xSemaphoreCreateBinary(),
    };
    if (args.done == NULL) {
        ESP_LOGE(TAG, "OTA worker semaphore allocation failed");
        return ESP_ERR_NO_MEM;
    }

    if (config != NULL) {
        args.config = *config;
    }

    const uint32_t effective_stack_size =
        stack_size > 0 ? stack_size : CONFIG_DOGDOG_OTA_TASK_STACK_SIZE;
    const BaseType_t created = xTaskCreate(dogdog_ota_worker_task,
                                           "dogdog_ota",
                                           effective_stack_size,
                                           &args,
                                           6,
                                           NULL);
    if (created != pdPASS) {
        vSemaphoreDelete(args.done);
        ESP_LOGE(TAG, "OTA worker task allocation failed");
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(args.done, portMAX_DELAY);
    vSemaphoreDelete(args.done);
    return args.result;
}

void ota_task(void *pvParameters)
{
    const char *device_name = pvParameters != NULL
                                  ? (const char *)pvParameters
                                  : CONFIG_DOGDOG_OTA_FALLBACK_DEVICE_NAME;
    ESP_ERROR_CHECK_WITHOUT_ABORT(dogdog_ota_check_and_update(device_name));
    vTaskDelete(NULL);
}
