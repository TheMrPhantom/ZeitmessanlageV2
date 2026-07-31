#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "OTA.h"

#define BUTTON_GPIO GPIO_NUM_33
#define BUTTON_RELEASE_DEBOUNCE_MS 50
#define OTA_BUTTON_HOLD_MS 10000
#define HORN_TX_ATTEMPTS 3
#define HORN_TX_INTERVAL_MS 10
#define HORN_TX_DRAIN_MS 20

#define HORN_FRAME_HEADER_LEN 24
#define HORN_CONTROL_MESSAGE_LEN 2
#define HORN_FRAME_LEN (HORN_FRAME_HEADER_LEN + HORN_CONTROL_MESSAGE_LEN)
#define HORN_VENDOR_MARKER 0xdd
#define HORN_COMMAND_STOP 0x03

static const char *TAG = "horn_remote";
static const uint8_t s_horn_mac[6] = {0xde, 0x09, 0xdd, 0x09, 0x00, 0x01};

static uint8_t s_source_mac[6];
static uint16_t s_sequence_number;

static void remote_ota_status(dd_ota_state_t state, esp_err_t error, void *context)
{
    (void)context;
    if (state == DD_OTA_FAILED) {
        ESP_LOGE(TAG, "Firmware upgrade failed: %s", esp_err_to_name(error));
    } else if (state == DD_OTA_SUCCEEDED) {
        ESP_LOGI(TAG, "Firmware upgrade complete; restarting");
    }
}

static void start_remote_ota_mode(void)
{
    const dd_ota_options_t options = {
        .status_cb = remote_ota_status,
        .context = NULL,
    };
    esp_err_t err = dd_ota_start(&options);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start OTA worker: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    vTaskDelete(NULL);
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t init_button(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static esp_err_t init_wifi(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to initialize esp-netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create default event loop");

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "Failed to initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Failed to set Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set station mode");
    ESP_RETURN_ON_ERROR(esp_read_mac(s_source_mac, ESP_MAC_WIFI_STA), TAG, "Failed to read station MAC");
    s_sequence_number = (uint16_t)(esp_random() & 0x0fffU);
    return ESP_OK;
}

static void build_stop_frame(uint8_t frame[HORN_FRAME_LEN])
{
    const uint8_t frame_template[HORN_FRAME_LEN] = {
        0xd0, 0x00,                          /* action management frame */
        0x00, 0x00,                          /* duration */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* destination */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* source */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* BSSID */
        0x00, 0x00,                          /* sequence control */
        HORN_VENDOR_MARKER,
        HORN_COMMAND_STOP,
    };

    memcpy(frame, frame_template, sizeof(frame_template));
    memcpy(frame + 4, s_horn_mac, sizeof(s_horn_mac));
    memcpy(frame + 10, s_source_mac, sizeof(s_source_mac));
    memcpy(frame + 16, s_horn_mac, sizeof(s_horn_mac));
    uint16_t sequence_control = (uint16_t)((s_sequence_number & 0x0fffU) << 4);
    frame[22] = (uint8_t)sequence_control;
    frame[23] = (uint8_t)(sequence_control >> 8);
    s_sequence_number = (uint16_t)((s_sequence_number + 1U) & 0x0fffU);
}

static esp_err_t send_stop_command(void)
{
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_channel(CONFIG_HORN_TIMER_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err == ESP_OK) {
        uint8_t frame[HORN_FRAME_LEN];
        build_stop_frame(frame);

        bool queued = false;
        esp_err_t last_tx_err = ESP_OK;
        for (unsigned int attempt = 0; attempt < HORN_TX_ATTEMPTS; attempt++) {
            last_tx_err = esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
            if (last_tx_err == ESP_OK) {
                queued = true;
            } else {
                ESP_LOGW(TAG,
                         "Horn stop transmission %u/%u failed: %s",
                         attempt + 1U,
                         HORN_TX_ATTEMPTS,
                         esp_err_to_name(last_tx_err));
            }

            if (attempt + 1U < HORN_TX_ATTEMPTS) {
                vTaskDelay(pdMS_TO_TICKS(HORN_TX_INTERVAL_MS));
            }
        }
        err = queued ? ESP_OK : last_tx_err;
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Horn stop command queued");
        /* esp_wifi_80211_tx() returns after queueing; keep Wi-Fi up long enough to drain it. */
        vTaskDelay(pdMS_TO_TICKS(HORN_TX_DRAIN_MS));
    } else {
        ESP_LOGE(TAG, "Failed to send horn stop command: %s", esp_err_to_name(err));
    }

    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop Wi-Fi: %s", esp_err_to_name(stop_err));
        if (err == ESP_OK) {
            err = stop_err;
        }
    }
    return err;
}

static void wait_for_button_release_or_request_ota(int64_t pressed_at_us)
{
    const TickType_t debounce_ticks = pdMS_TO_TICKS(BUTTON_RELEASE_DEBOUNCE_MS);
    TickType_t released_since = xTaskGetTickCount();
    bool ota_requested = false;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            released_since = now;
            if (!ota_requested &&
                esp_timer_get_time() - pressed_at_us >=
                (int64_t)OTA_BUTTON_HOLD_MS * 1000LL) {
                ESP_LOGI(TAG, "Button held for %d seconds; firmware upgrade requested",
                         OTA_BUTTON_HOLD_MS / 1000);
                ota_requested = true;
            }
        } else if ((TickType_t)(now - released_since) >= debounce_ticks) {
            if (ota_requested) {
                // Require a continuously stable release before rebooting so
                // switch bounce cannot leave the wake button asserted.
                dd_ota_request_reboot();
            }
            return;
        }
        vTaskDelay(1);
    }
}

static bool sleep_until_button_press(void)
{
    bool pressed_during_attempt = false;
    bool woke_from_button = false;
    esp_err_t err = rtc_gpio_pullup_en(BUTTON_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable the RTC button pull-up: %s", esp_err_to_name(err));
        pressed_during_attempt = gpio_get_level(BUTTON_GPIO) == 0;
        vTaskDelay(pdMS_TO_TICKS(100));
        return pressed_during_attempt || gpio_get_level(BUTTON_GPIO) == 0;
    }

    err = esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button wake-up: %s", esp_err_to_name(err));
        pressed_during_attempt = gpio_get_level(BUTTON_GPIO) == 0;
    } else {
        err = esp_light_sleep_start();
        woke_from_button = err == ESP_OK &&
                           esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
        /* Capture a short press before logging, delaying, or reconfiguring the pad. */
        pressed_during_attempt = gpio_get_level(BUTTON_GPIO) == 0;
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enter light sleep: %s", esp_err_to_name(err));
        }
    }

    esp_err_t rtc_err = rtc_gpio_deinit(BUTTON_GPIO);
    if (rtc_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to release the RTC button GPIO after sleep: %s",
                 esp_err_to_name(rtc_err));
    }

    esp_err_t gpio_err = init_button();
    if (gpio_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore the button GPIO after sleep: %s",
                 esp_err_to_name(gpio_err));
    }

    bool currently_pressed = gpio_get_level(BUTTON_GPIO) == 0;
    if (err != ESP_OK && !pressed_during_attempt && !currently_pressed) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return woke_from_button || pressed_during_attempt || currently_pressed;
}

void app_main(void)
{
    if (dd_ota_consume_reboot_request()) {
        ESP_LOGI(TAG, "Starting clean firmware upgrade boot");
        start_remote_ota_mode();
        return;
    }

    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(init_button());
    ESP_ERROR_CHECK(init_wifi());

    esp_err_t valid_err = dd_ota_mark_app_valid();
    if (valid_err != ESP_OK) {
        ESP_LOGE(TAG, "Could not mark running firmware valid: %s; restarting for rollback",
                 esp_err_to_name(valid_err));
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
    }

    ESP_LOGI(TAG,
             "Ready on button GPIO %d; horn Wi-Fi channel %d",
             BUTTON_GPIO,
             CONFIG_HORN_TIMER_WIFI_CHANNEL);

    bool button_pressed = gpio_get_level(BUTTON_GPIO) == 0;
    while (true) {
        if (button_pressed) {
            const int64_t pressed_at_us = esp_timer_get_time();
            send_stop_command();
            wait_for_button_release_or_request_ota(pressed_at_us);
        }

        button_pressed = sleep_until_button_press();
    }
}
