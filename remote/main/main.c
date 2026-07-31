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
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define BUTTON_GPIO GPIO_NUM_33
#define BUTTON_RELEASE_DEBOUNCE_MS 50

#define HORN_FRAME_HEADER_LEN 24
#define HORN_CONTROL_MESSAGE_LEN 2
#define HORN_FRAME_LEN (HORN_FRAME_HEADER_LEN + HORN_CONTROL_MESSAGE_LEN)
#define HORN_VENDOR_MARKER 0xdd
#define HORN_COMMAND_STOP 0x03

static const char *TAG = "horn_remote";
static const uint8_t s_horn_mac[6] = {0xde, 0x09, 0xdd, 0x09, 0x00, 0x01};

static uint8_t s_source_mac[6];
static uint8_t s_sequence_number;

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
    frame[22] = (uint8_t)(s_sequence_number << 4);
    frame[23] = (uint8_t)(s_sequence_number >> 4);
    s_sequence_number++;
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
        err = esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Horn stop command sent");
        vTaskDelay(pdMS_TO_TICKS(20));
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

static void wait_for_button_release(void)
{
    TickType_t released_since = 0;

    while (released_since < pdMS_TO_TICKS(BUTTON_RELEASE_DEBOUNCE_MS)) {
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            released_since = 0;
        } else {
            released_since++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static bool sleep_until_button_press(void)
{
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(BUTTON_GPIO));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0));

    esp_err_t err = esp_light_sleep_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter light sleep: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_ERROR_CHECK(rtc_gpio_deinit(BUTTON_GPIO));
    ESP_ERROR_CHECK(init_button());
    return err == ESP_OK && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(init_button());
    ESP_ERROR_CHECK(init_wifi());

    ESP_LOGI(TAG,
             "Ready on button GPIO %d; horn Wi-Fi channel %d",
             BUTTON_GPIO,
             CONFIG_HORN_TIMER_WIFI_CHANNEL);

    bool button_pressed = gpio_get_level(BUTTON_GPIO) == 0;
    while (true) {
        if (button_pressed) {
            send_stop_command();
            wait_for_button_release();
        }

        button_pressed = sleep_until_button_press();
    }
}
