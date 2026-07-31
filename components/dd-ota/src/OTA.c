#define LOG_LOCAL_LEVEL ESP_LOG_INFO

#include "OTA.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#if CONFIG_DD_OTA_USE_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "nvs_flash.h"

static const char *TAG = "OTA";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define DD_OTA_REBOOT_MAGIC UINT32_C(0x4F544131)
#define DD_OTA_URL_MAX_LENGTH 256U

// Volatile is required here: the values are observed only after a CPU reset,
// which is outside the C abstract machine and could otherwise make these
// apparently dead stores eligible for interprocedural/LTO elimination.
static RTC_NOINIT_ATTR volatile uint32_t s_reboot_magic;
static RTC_NOINIT_ATTR volatile uint32_t s_reboot_magic_inverse;

typedef struct
{
    dd_ota_status_cb_t status_cb;
    void *callback_context;
} ota_task_context_t;

static portMUX_TYPE s_ota_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_ota_running;
static ota_task_context_t s_task_context;

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define ESP_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

typedef struct
{
    EventGroupHandle_t event_group;
    esp_netif_t *sta_netif;
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    esp_event_handler_instance_t ota_handler;
    int retry_count;
    bool event_loop_owned;
    bool wifi_initialized;
    volatile bool shutting_down;
} ota_wifi_context_t;

static void ota_task(void *pvParameters);

static bool is_ascii_alphanumeric(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
}

static void notify_status(const ota_task_context_t *task_context,
                          dd_ota_state_t state, esp_err_t error)
{
    if (task_context != NULL && task_context->status_cb != NULL)
    {
        task_context->status_cb(state, error, task_context->callback_context);
    }
}

static esp_err_t build_firmware_url(char *url, size_t url_size)
{
    if (url == NULL || url_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL)
    {
        ESP_LOGE(TAG, "Could not determine the running application partition");
        return ESP_ERR_NOT_FOUND;
    }

    esp_app_desc_t running_app_info = {0};
    esp_err_t err = esp_ota_get_partition_description(running_partition, &running_app_info);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not read the running image description: %s", esp_err_to_name(err));
        return err;
    }

    const size_t project_name_length = strnlen(
        running_app_info.project_name, sizeof(running_app_info.project_name));
    if (project_name_length == 0 ||
        !is_ascii_alphanumeric(running_app_info.project_name[0]))
    {
        ESP_LOGE(TAG, "The running firmware has no URL-safe project name");
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t index = 0; index < project_name_length; index++)
    {
        const char character = running_app_info.project_name[index];
        if (!is_ascii_alphanumeric(character) && character != '.' &&
            character != '_' && character != '-')
        {
            ESP_LOGE(TAG, "The running project name contains a URL-unsafe character");
            return ESP_ERR_INVALID_ARG;
        }
    }

    const char *base_url = CONFIG_DD_OTA_BASE_URL;
    size_t base_url_length = strlen(base_url);
    while (base_url_length > 0 && base_url[base_url_length - 1] == '/')
    {
        base_url_length--;
    }

    if (base_url_length < strlen("https://") ||
        strncmp(base_url, "https://", strlen("https://")) != 0)
    {
        ESP_LOGE(TAG, "The OTA base URL must use HTTPS");
        return ESP_ERR_INVALID_ARG;
    }

    const int written = snprintf(url, url_size, "%.*s/%.*s.bin",
                                 (int)base_url_length, base_url,
                                 (int)project_name_length,
                                 running_app_info.project_name);
    if (written < 0 || (size_t)written >= url_size)
    {
        ESP_LOGE(TAG, "The generated firmware URL is too long");
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

bool dd_ota_consume_reboot_request(void)
{
    const uint32_t magic = s_reboot_magic;
    const uint32_t inverse = s_reboot_magic_inverse;

    // Clear first so a later reset can never consume the same request twice.
    s_reboot_magic = 0;
    s_reboot_magic_inverse = 0;
    __sync_synchronize();

    return esp_reset_reason() == ESP_RST_SW &&
           magic == DD_OTA_REBOOT_MAGIC && inverse == ~DD_OTA_REBOOT_MAGIC;
}

void dd_ota_request_reboot(void)
{
    s_reboot_magic = DD_OTA_REBOOT_MAGIC;
    s_reboot_magic_inverse = ~DD_OTA_REBOOT_MAGIC;
    __sync_synchronize();
    esp_restart();
}

esp_err_t dd_ota_mark_app_valid(void)
{
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    esp_ota_img_states_t state;
    esp_err_t err = esp_ota_get_state_partition(running_partition, &state);
    if (err == ESP_ERR_NOT_SUPPORTED)
    {
        // Factory-only layouts do not have OTA state. Treat this as already valid
        // so the API remains safe during a staged partition-table migration.
        return ESP_OK;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not read running image state: %s", esp_err_to_name(err));
        return err;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Could not mark the running image valid: %s", esp_err_to_name(err));
        }
        return err;
    }

    if (state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_UNDEFINED)
    {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Running image cannot be marked valid from OTA state %d", (int)state);
    return ESP_ERR_INVALID_STATE;
}

static void set_wifi_failure(ota_wifi_context_t *context)
{
    if (context != NULL && context->event_group != NULL)
    {
        xEventGroupSetBits(context->event_group, WIFI_FAIL_BIT);
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    ota_wifi_context_t *context = (ota_wifi_context_t *)arg;

    if (event_base == ESP_HTTPS_OTA_EVENT)
    {
        switch (event_id)
        {
        case ESP_HTTPS_OTA_START:
            ESP_LOGI(TAG, "OTA started");
            break;
        case ESP_HTTPS_OTA_CONNECTED:
            ESP_LOGI(TAG, "Connected to OTA server");
            break;
        case ESP_HTTPS_OTA_GET_IMG_DESC:
            ESP_LOGI(TAG, "Reading image description");
            break;
        case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
            if (event_data != NULL)
            {
                ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(const esp_chip_id_t *)event_data);
            }
            break;
        case ESP_HTTPS_OTA_DECRYPT_CB:
            ESP_LOGI(TAG, "Calling OTA decrypt callback");
            break;
        case ESP_HTTPS_OTA_WRITE_FLASH:
            if (event_data != NULL)
            {
                ESP_LOGD(TAG, "Writing to flash: %d bytes written", *(const int *)event_data);
            }
            break;
        case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
            if (event_data != NULL)
            {
                ESP_LOGI(TAG, "Boot partition updated to subtype %d",
                         *(const esp_partition_subtype_t *)event_data);
            }
            break;
        case ESP_HTTPS_OTA_FINISH:
            ESP_LOGI(TAG, "OTA finished");
            break;
        case ESP_HTTPS_OTA_ABORT:
            ESP_LOGI(TAG, "OTA aborted");
            break;
        default:
            break;
        }
        return;
    }

    if (context == NULL || context->event_group == NULL || context->shutting_down)
    {
        return;
    }

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
        {
            ESP_LOGI(TAG, "Wi-Fi station started");
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to start Wi-Fi connection: %s", esp_err_to_name(err));
                set_wifi_failure(context);
            }
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            xEventGroupClearBits(context->event_group, WIFI_CONNECTED_BIT);

            if (event_data != NULL)
            {
                const wifi_event_sta_disconnected_t *disconnected =
                    (const wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u)", (unsigned int)disconnected->reason);
            }
            else
            {
                ESP_LOGW(TAG, "Wi-Fi disconnected");
            }

            if (context->retry_count >= CONFIG_OTA_WIFI_MAXIMUM_RETRY)
            {
                ESP_LOGE(TAG, "Wi-Fi connection failed after %d retries",
                         CONFIG_OTA_WIFI_MAXIMUM_RETRY);
                set_wifi_failure(context);
                break;
            }

            context->retry_count++;
            ESP_LOGI(TAG, "Retrying Wi-Fi connection (%d/%d)", context->retry_count,
                     CONFIG_OTA_WIFI_MAXIMUM_RETRY);
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to retry Wi-Fi connection: %s", esp_err_to_name(err));
                set_wifi_failure(context);
            }
            break;
        }
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "Wi-Fi station received an IP address");
        context->retry_count = 0;
        xEventGroupClearBits(context->event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(context->event_group, WIFI_CONNECTED_BIT);
    }
}

static void record_cleanup_error(esp_err_t *cleanup_result, const char *operation,
                                 esp_err_t err)
{
    if (err == ESP_OK)
    {
        return;
    }

    ESP_LOGW(TAG, "%s failed during cleanup: %s", operation, esp_err_to_name(err));
    if (*cleanup_result == ESP_OK)
    {
        *cleanup_result = err;
    }
}

static esp_err_t wifi_deinit_sta(ota_wifi_context_t *context)
{
    if (context == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t cleanup_result = ESP_OK;
    context->shutting_down = true;

    if (context->ota_handler != NULL)
    {
        esp_err_t err = esp_event_handler_instance_unregister(
            ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, context->ota_handler);
        record_cleanup_error(&cleanup_result, "Unregistering OTA event handler", err);
        context->ota_handler = NULL;
    }

    if (context->ip_handler != NULL)
    {
        esp_err_t err = esp_event_handler_instance_unregister(
            IP_EVENT, IP_EVENT_STA_GOT_IP, context->ip_handler);
        record_cleanup_error(&cleanup_result, "Unregistering IP event handler", err);
        context->ip_handler = NULL;
    }

    if (context->wifi_handler != NULL)
    {
        esp_err_t err = esp_event_handler_instance_unregister(
            WIFI_EVENT, ESP_EVENT_ANY_ID, context->wifi_handler);
        record_cleanup_error(&cleanup_result, "Unregistering Wi-Fi event handler", err);
        context->wifi_handler = NULL;
    }

    if (context->wifi_initialized)
    {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_ERR_WIFI_NOT_STARTED)
        {
            record_cleanup_error(&cleanup_result, "Stopping Wi-Fi", err);
        }
    }

    if (context->sta_netif != NULL)
    {
        esp_netif_destroy_default_wifi(context->sta_netif);
        context->sta_netif = NULL;
    }

    if (context->wifi_initialized)
    {
        esp_err_t err = esp_wifi_deinit();
        record_cleanup_error(&cleanup_result, "Deinitializing Wi-Fi", err);
        context->wifi_initialized = false;
    }

    if (context->event_loop_owned)
    {
        esp_err_t err = esp_event_loop_delete_default();
        record_cleanup_error(&cleanup_result, "Deleting default event loop", err);
        context->event_loop_owned = false;
    }

    if (context->event_group != NULL)
    {
        vEventGroupDelete(context->event_group);
        context->event_group = NULL;
    }

    return cleanup_result;
}

static esp_err_t wifi_init_sta(ota_wifi_context_t *context)
{
    if (context == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t wifi_config = {0};
    const size_t ssid_len = strlen(CONFIG_ESP_WIFI_SSID);
    const size_t password_len = strlen(CONFIG_ESP_WIFI_PASSWORD);
    const size_t h2e_identifier_len = strlen(ESP_H2E_IDENTIFIER);
    if (ssid_len == 0 || ssid_len > sizeof(wifi_config.sta.ssid))
    {
        ESP_LOGE(TAG, "OTA Wi-Fi SSID must contain between 1 and %u bytes",
                 (unsigned int)sizeof(wifi_config.sta.ssid));
        return ESP_ERR_INVALID_ARG;
    }
    if (password_len > sizeof(wifi_config.sta.password) - 1 ||
        (password_len > 0 && password_len < 8))
    {
        ESP_LOGE(TAG, "OTA Wi-Fi password must be empty or contain between 8 and 63 bytes");
        return ESP_ERR_INVALID_ARG;
    }
    if (h2e_identifier_len >= sizeof(wifi_config.sta.sae_h2e_identifier))
    {
        ESP_LOGE(TAG, "OTA Wi-Fi SAE H2E identifier must contain at most %u bytes",
                 (unsigned int)sizeof(wifi_config.sta.sae_h2e_identifier) - 1U);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        // The application owns NVS data, so this component must never erase it as recovery.
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Network stack initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_mode_t existing_mode;
    err = esp_wifi_get_mode(&existing_mode);
    if (err == ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi is already initialized; refusing to replace another owner's configuration");
        return ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_ERR_WIFI_NOT_INIT)
    {
        ESP_LOGE(TAG, "Could not determine Wi-Fi state: %s", esp_err_to_name(err));
        return err;
    }

    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") != NULL ||
        esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") != NULL ||
        esp_netif_get_handle_from_ifkey("WIFI_NAN_DEF") != NULL)
    {
        ESP_LOGE(TAG, "A default Wi-Fi interface already exists; refusing to modify another owner's network state");
        return ESP_ERR_INVALID_STATE;
    }

    context->event_group = xEventGroupCreate();
    if (context->event_group == NULL)
    {
        ESP_LOGE(TAG, "Could not allocate Wi-Fi event group");
        return ESP_ERR_NO_MEM;
    }

    err = esp_event_loop_create_default();
    if (err == ESP_OK)
    {
        context->event_loop_owned = true;
    }
    else if (err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Default event loop initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
    context->sta_netif = esp_netif_new(&netif_config);
    if (context->sta_netif == NULL)
    {
        ESP_LOGE(TAG, "Could not allocate the Wi-Fi station network interface");
        return ESP_ERR_NO_MEM;
    }

    err = esp_netif_attach_wifi_station(context->sta_netif);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not attach the Wi-Fi station interface: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_default_wifi_sta_handlers();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not install default Wi-Fi handlers: %s", esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_init_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(err));
        return err;
    }
    context->wifi_initialized = true;

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, context,
                                               &context->wifi_handler);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi event handler registration failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, context,
                                               &context->ip_handler);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "IP event handler registration failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_handler_instance_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, context,
                                               &context->ota_handler);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "OTA event handler registration failed: %s", esp_err_to_name(err));
        return err;
    }

    memcpy(wifi_config.sta.ssid, CONFIG_ESP_WIFI_SSID, ssid_len);
    memcpy(wifi_config.sta.password, CONFIG_ESP_WIFI_PASSWORD, password_len);
    memcpy(wifi_config.sta.sae_h2e_identifier, ESP_H2E_IDENTIFIER, h2e_identifier_len);
    wifi_config.sta.threshold.authmode = password_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = ESP_WIFI_SAE_MODE;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Setting Wi-Fi station mode failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Setting Wi-Fi station configuration failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Starting Wi-Fi failed: %s", esp_err_to_name(err));
        return err;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        context->event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(CONFIG_OTA_WIFI_CONNECT_TIMEOUT_MS));

    if ((bits & WIFI_CONNECTED_BIT) != 0)
    {
        ESP_LOGI(TAG, "Connected to Wi-Fi SSID %s", CONFIG_ESP_WIFI_SSID);
        return ESP_OK;
    }

    if ((bits & WIFI_FAIL_BIT) != 0)
    {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi SSID %s", CONFIG_ESP_WIFI_SSID);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Timed out after %d ms while connecting to Wi-Fi SSID %s",
             CONFIG_OTA_WIFI_CONNECT_TIMEOUT_MS, CONFIG_ESP_WIFI_SSID);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t validate_image_header(const esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition == NULL)
    {
        ESP_LOGE(TAG, "Could not determine the running application partition");
        return ESP_ERR_NOT_FOUND;
    }

    esp_app_desc_t running_app_info = {0};
    esp_err_t err = esp_ota_get_partition_description(running_partition, &running_app_info);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not read the running image description: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Running firmware: %.*s version %.*s",
             (int)sizeof(running_app_info.project_name), running_app_info.project_name,
             (int)sizeof(running_app_info.version), running_app_info.version);

    if (memcmp(new_app_info->project_name, running_app_info.project_name,
               sizeof(new_app_info->project_name)) != 0)
    {
        ESP_LOGE(TAG, "OTA image project %.*s does not match running project %.*s",
                 (int)sizeof(new_app_info->project_name), new_app_info->project_name,
                 (int)sizeof(running_app_info.project_name), running_app_info.project_name);
        return ESP_ERR_INVALID_VERSION;
    }

    if (memcmp(new_app_info->version, running_app_info.version,
               sizeof(new_app_info->version)) == 0)
    {
        ESP_LOGW(TAG, "OTA image version %.*s is already installed",
                 (int)sizeof(new_app_info->version), new_app_info->version);
        return ESP_ERR_INVALID_VERSION;
    }

    ESP_LOGI(TAG, "Installing firmware version %.*s",
             (int)sizeof(new_app_info->version), new_app_info->version);
    return ESP_OK;
}

static void ota_task(void *pvParameters)
{
    const ota_task_context_t task_context =
        pvParameters != NULL ? *(const ota_task_context_t *)pvParameters
                             : (ota_task_context_t){0};

    ota_wifi_context_t wifi_context = {0};
    esp_https_ota_handle_t https_ota_handle = NULL;
    bool ota_succeeded = false;
    esp_err_t err = ESP_FAIL;
    char firmware_url[DD_OTA_URL_MAX_LENGTH] = {0};

    ESP_LOGI(TAG, "Starting OTA task");
    notify_status(&task_context, DD_OTA_CONNECTING, ESP_OK);

    err = build_firmware_url(firmware_url, sizeof(firmware_url));
    if (err != ESP_OK)
    {
        goto cleanup;
    }

    ESP_LOGI(TAG, "Firmware URL: %s", firmware_url);

    err = wifi_init_sta(&wifi_context);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "OTA Wi-Fi setup failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    notify_status(&task_context, DD_OTA_DOWNLOADING, ESP_OK);

    const esp_http_client_config_t http_config = {
        .url = firmware_url,
        .timeout_ms = CONFIG_DD_OTA_RECV_TIMEOUT_MS,
        .keep_alive_enable = true,
        .disable_auto_redirect = true,
#if CONFIG_DD_OTA_USE_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };

    const esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
#if CONFIG_DD_OTA_ENABLE_PARTIAL_HTTP_DOWNLOAD
        .partial_http_download = true,
        .max_http_request_size = CONFIG_DD_OTA_HTTP_REQUEST_SIZE,
#endif
    };

    err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Starting HTTPS OTA failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    esp_app_desc_t app_desc = {0};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Reading OTA image description failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = validate_image_header(&app_desc);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "OTA image was not accepted: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int last_image_length = esp_https_ota_get_image_len_read(https_ota_handle);
    int64_t last_progress_time_us = esp_timer_get_time();

    do
    {
        err = esp_https_ota_perform(https_ota_handle);
        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS)
        {
            const int image_length = esp_https_ota_get_image_len_read(https_ota_handle);
            const int64_t now_us = esp_timer_get_time();
            ESP_LOGD(TAG, "Image bytes read: %d", image_length);

            if (image_length > last_image_length)
            {
                last_image_length = image_length;
                last_progress_time_us = now_us;
            }
            else if (now_us - last_progress_time_us >=
                     (int64_t)CONFIG_DD_OTA_NO_PROGRESS_TIMEOUT_MS * 1000LL)
            {
                ESP_LOGE(TAG, "OTA download made no progress for %d ms",
                         CONFIG_DD_OTA_NO_PROGRESS_TIMEOUT_MS);
                err = ESP_ERR_TIMEOUT;
            }
        }
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Downloading or writing the OTA image failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    if (!esp_https_ota_is_complete_data_received(https_ota_handle))
    {
        ESP_LOGE(TAG, "The complete OTA image was not received");
        err = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    notify_status(&task_context, DD_OTA_VALIDATING, ESP_OK);

    // esp_https_ota_finish() always consumes the handle, including on validation failure.
    err = esp_https_ota_finish(https_ota_handle);
    https_ota_handle = NULL;
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED)
        {
            ESP_LOGE(TAG, "OTA image validation failed; the image is corrupted or untrusted");
        }
        else
        {
            ESP_LOGE(TAG, "Finishing the OTA update failed: %s", esp_err_to_name(err));
        }
        goto cleanup;
    }

    ota_succeeded = true;
    ESP_LOGI(TAG, "OTA upgrade successful");

cleanup:
    if (https_ota_handle != NULL)
    {
        esp_err_t abort_err = esp_https_ota_abort(https_ota_handle);
        https_ota_handle = NULL;
        if (abort_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Aborting OTA cleanup failed: %s", esp_err_to_name(abort_err));
        }
    }

    esp_err_t cleanup_err = wifi_deinit_sta(&wifi_context);
    if (cleanup_err != ESP_OK)
    {
        ESP_LOGW(TAG, "OTA network cleanup completed with errors: %s",
                 esp_err_to_name(cleanup_err));
    }

    if (ota_succeeded)
    {
        notify_status(&task_context, DD_OTA_SUCCEEDED, ESP_OK);
        ESP_LOGI(TAG, "Rebooting into the updated firmware");
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DD_OTA_SUCCESS_REBOOT_DELAY_MS));
    }
    else
    {
        if (err == ESP_OK)
        {
            err = ESP_FAIL;
        }
        notify_status(&task_context, DD_OTA_FAILED, err);
        ESP_LOGW(TAG, "OTA failed; rebooting into the unchanged firmware in %d ms",
                 CONFIG_DD_OTA_FAILURE_REBOOT_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_DD_OTA_FAILURE_REBOOT_DELAY_MS));
    }

    // Keep the singleton claimed until reset. Clearing it before esp_restart()
    // would leave a small cross-core window in which a second worker could be
    // created while this task is already committed to rebooting.
    esp_restart();
}

esp_err_t dd_ota_start(const dd_ota_options_t *options)
{
    portENTER_CRITICAL(&s_ota_lock);
    if (s_ota_running)
    {
        portEXIT_CRITICAL(&s_ota_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_ota_running = true;
    s_task_context.status_cb = options != NULL ? options->status_cb : NULL;
    s_task_context.callback_context = options != NULL ? options->context : NULL;
    portEXIT_CRITICAL(&s_ota_lock);

    if (xTaskCreate(ota_task, "dd_ota", CONFIG_DD_OTA_TASK_STACK_SIZE,
                    &s_task_context, CONFIG_DD_OTA_TASK_PRIORITY, NULL) != pdPASS)
    {
        portENTER_CRITICAL(&s_ota_lock);
        s_ota_running = false;
        s_task_context = (ota_task_context_t){0};
        portEXIT_CRITICAL(&s_ota_lock);
        ESP_LOGE(TAG, "Could not create the OTA worker task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
