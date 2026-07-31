#include "HornTimer.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#define HORN_TIMER_START_MESSAGE_LEN 10
#define HORN_TIMER_CONTROL_MESSAGE_LEN 2
#define HORN_TIMER_MAX_MESSAGE_LEN HORN_TIMER_START_MESSAGE_LEN
#define HORN_TIMER_FRAME_HEADER_LEN 24
#define HORN_TIMER_FRAME_LEN (HORN_TIMER_FRAME_HEADER_LEN + HORN_TIMER_MAX_MESSAGE_LEN)
#define HORN_TIMER_VENDOR_MARKER 0xdd
#define HORN_TIMER_COMMAND_START 0x01
#define HORN_TIMER_COMMAND_RESET 0x02

static const char *TAG = "HornTimer";
static bool s_horn_timer_ready = false;
static uint8_t s_sequence_number = 0;
static uint8_t s_source_mac[6] = {0};
static const uint8_t s_horn_mac[6] = {0xde, 0x09, 0xdd, 0x09, 0x00, 0x01};

static void horn_timer_send_message(uint8_t command, const uint8_t *payload, size_t payload_len)
{
    if (!s_horn_timer_ready)
    {
        return;
    }

    size_t message_len = HORN_TIMER_CONTROL_MESSAGE_LEN + payload_len;
    if ((payload_len > 0 && !payload) || message_len > HORN_TIMER_MAX_MESSAGE_LEN)
    {
        ESP_LOGW(TAG, "Invalid horn timer payload: %u bytes", (unsigned int)payload_len);
        return;
    }

    uint8_t frame[HORN_TIMER_FRAME_LEN] = {
        0xd0, 0x00,                         /* action management frame */
        0x00, 0x00,                         /* duration */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* destination */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* source */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* bssid */
        0x00, 0x00,                         /* sequence control */
        HORN_TIMER_VENDOR_MARKER,
        command,
    };

    memcpy(frame + 4, s_horn_mac, sizeof(s_horn_mac));
    memcpy(frame + 10, s_source_mac, sizeof(s_source_mac));
    memcpy(frame + 16, s_horn_mac, sizeof(s_horn_mac));
    frame[22] = (uint8_t)(s_sequence_number << 4);
    frame[23] = (uint8_t)(s_sequence_number >> 4);
    s_sequence_number++;

    if (payload_len > 0)
    {
        memcpy(frame + HORN_TIMER_FRAME_HEADER_LEN + HORN_TIMER_CONTROL_MESSAGE_LEN, payload, payload_len);
    }

    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, frame, HORN_TIMER_FRAME_HEADER_LEN + message_len, false);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to send horn timer packet: %s", esp_err_to_name(err));
    }
}

esp_err_t init_horn_timer_broadcast(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to initialize esp-netif: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_config);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE)
    {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure Wi-Fi storage: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure Wi-Fi station mode: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_channel(CONFIG_HORN_TIMER_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set horn Wi-Fi channel: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_read_mac(s_source_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read Wi-Fi MAC: %s", esp_err_to_name(err));
        return err;
    }

    s_horn_timer_ready = true;
    ESP_LOGI(TAG,
             "Horn timer unicast initialized on Wi-Fi channel %d for %02x:%02x:%02x:%02x:%02x:%02x",
             CONFIG_HORN_TIMER_WIFI_CHANNEL,
             s_horn_mac[0],
             s_horn_mac[1],
             s_horn_mac[2],
             s_horn_mac[3],
             s_horn_mac[4],
             s_horn_mac[5]);
    return ESP_OK;
}

void horn_timer_broadcast_elapsed_us(int64_t elapsed_us)
{
    if (elapsed_us < 0)
    {
        elapsed_us = 0;
    }

    uint64_t elapsed_ns = (uint64_t)elapsed_us > UINT64_MAX / 1000ULL
                              ? UINT64_MAX
                              : (uint64_t)elapsed_us * 1000ULL;
    horn_timer_send_message(HORN_TIMER_COMMAND_START, (const uint8_t *)&elapsed_ns, sizeof(elapsed_ns));
}

void horn_timer_broadcast_reset(void)
{
    horn_timer_send_message(HORN_TIMER_COMMAND_RESET, NULL, 0);
}
