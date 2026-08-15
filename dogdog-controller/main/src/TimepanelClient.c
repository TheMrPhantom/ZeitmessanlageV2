#include "TimepanelClient.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "TimepanelProtocol.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

static const char *TAG = "TimepanelClient";

static bool s_timepanel_ready = false;
static uint8_t s_sequence_number = 0;
static uint8_t s_source_mac[6] = {0};
static uint8_t s_timepanel_mac[6] = {
    TIMEPANEL_MAC_PREFIX_0,
    TIMEPANEL_MAC_PREFIX_1,
    TIMEPANEL_MAC_PREFIX_2,
    TIMEPANEL_MAC_PREFIX_3,
    (uint8_t)((CONFIG_TIMEPANEL_MAC_ID >> 8) & 0xff),
    (uint8_t)(CONFIG_TIMEPANEL_MAC_ID & 0xff),
};

static TimepanelCompetitorPayload s_competitor = {0};
static uint16_t s_faults = 0;
static uint16_t s_refusals = 0;

static void copy_field(char *target, size_t target_len, const char *source)
{
    if (source == NULL)
    {
        source = "";
    }

    snprintf(target, target_len, "%s", source);
}

static void timepanel_send_message(uint8_t command,
                                   const uint8_t *payload,
                                   size_t payload_len)
{
#ifndef CONFIG_TIMEPANEL_ENABLED
    (void)command;
    (void)payload;
    (void)payload_len;
    return;
#else
    if (!s_timepanel_ready)
    {
        return;
    }

    size_t message_len = TIMEPANEL_CONTROL_MESSAGE_LEN + payload_len;
    if (message_len > TIMEPANEL_MAX_MESSAGE_LEN)
    {
        ESP_LOGW(TAG, "Timepanel payload too large: %u", (unsigned int)payload_len);
        return;
    }

    uint8_t frame[TIMEPANEL_FRAME_HEADER_LEN + TIMEPANEL_MAX_MESSAGE_LEN] = {
        0xd0, 0x00,                         /* action management frame */
        0x00, 0x00,                         /* duration */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* destination */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* source */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* bssid */
        0x00, 0x00,                         /* sequence control */
        TIMEPANEL_VENDOR_MARKER,
        command,
    };

    memcpy(frame + 4, s_timepanel_mac, sizeof(s_timepanel_mac));
    memcpy(frame + 10, s_source_mac, sizeof(s_source_mac));
    memcpy(frame + 16, s_timepanel_mac, sizeof(s_timepanel_mac));
    frame[22] = (uint8_t)(s_sequence_number << 4);
    frame[23] = (uint8_t)(s_sequence_number >> 4);
    s_sequence_number++;

    if (payload_len > 0)
    {
        memcpy(frame + TIMEPANEL_FRAME_HEADER_LEN + TIMEPANEL_CONTROL_MESSAGE_LEN,
               payload,
               payload_len);
    }

    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA,
                                      frame,
                                      TIMEPANEL_FRAME_HEADER_LEN + message_len,
                                      false);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to send timepanel packet: %s", esp_err_to_name(err));
    }
#endif
}

esp_err_t init_timepanel_client(void)
{
#ifndef CONFIG_TIMEPANEL_ENABLED
    ESP_LOGI(TAG, "Timepanel wireless client disabled");
    return ESP_OK;
#else
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

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_WIFI_NOT_INIT)
    {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_wifi_set_channel(CONFIG_HORN_TIMER_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set timepanel Wi-Fi channel: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_read_mac(s_source_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read Wi-Fi MAC: %s", esp_err_to_name(err));
        return err;
    }

    s_timepanel_ready = true;
    ESP_LOGI(TAG,
             "Timepanel sender initialized on Wi-Fi channel %d for %02x:%02x:%02x:%02x:%02x:%02x",
             CONFIG_HORN_TIMER_WIFI_CHANNEL,
             s_timepanel_mac[0],
             s_timepanel_mac[1],
             s_timepanel_mac[2],
             s_timepanel_mac[3],
             s_timepanel_mac[4],
             s_timepanel_mac[5]);
    return ESP_OK;
#endif
}

void timepanel_set_competitor(const char *first_name,
                              const char *last_name,
                              const char *dog_name)
{
    copy_field(s_competitor.first_name,
               sizeof(s_competitor.first_name),
               first_name);
    copy_field(s_competitor.last_name,
               sizeof(s_competitor.last_name),
               last_name);
    copy_field(s_competitor.dog_name,
               sizeof(s_competitor.dog_name),
               dog_name);
    timepanel_send_competitor();
}

void timepanel_send_competitor(void)
{
    timepanel_send_message(TIMEPANEL_COMMAND_COMPETITOR,
                           (const uint8_t *)&s_competitor,
                           sizeof(s_competitor));
}

void timepanel_send_reset(void)
{
    s_faults = 0;
    s_refusals = 0;
    timepanel_send_message(TIMEPANEL_COMMAND_RESET,
                           (const uint8_t *)&s_competitor,
                           sizeof(s_competitor));
}

void timepanel_send_start(int64_t offset_ms)
{
    if (offset_ms < 0)
    {
        offset_ms = 0;
    }

    TimepanelU32Payload payload = {
        .value = (uint32_t)offset_ms,
    };
    timepanel_send_message(TIMEPANEL_COMMAND_START,
                           (const uint8_t *)&payload,
                           sizeof(payload));
}

void timepanel_send_stop(int64_t elapsed_ms)
{
    if (elapsed_ms < 0)
    {
        elapsed_ms = 0;
    }

    TimepanelU32Payload payload = {
        .value = (uint32_t)elapsed_ms,
    };
    timepanel_send_message(TIMEPANEL_COMMAND_STOP,
                           (const uint8_t *)&payload,
                           sizeof(payload));
}

void timepanel_send_fault(uint16_t faults)
{
    s_faults = faults;
    TimepanelU16Payload payload = {
        .value = s_faults,
    };
    timepanel_send_message(TIMEPANEL_COMMAND_FAULT,
                           (const uint8_t *)&payload,
                           sizeof(payload));
}

void timepanel_send_refusal(uint16_t refusals)
{
    s_refusals = refusals;
    TimepanelU16Payload payload = {
        .value = s_refusals,
    };
    timepanel_send_message(TIMEPANEL_COMMAND_REFUSAL,
                           (const uint8_t *)&payload,
                           sizeof(payload));
}

void timepanel_send_fault_increment(void)
{
    if (s_faults < UINT16_MAX)
    {
        s_faults++;
    }
    timepanel_send_fault(s_faults);
}

void timepanel_send_refusal_increment(void)
{
    if (s_refusals < UINT16_MAX)
    {
        s_refusals++;
    }
    timepanel_send_refusal(s_refusals);
}

void timepanel_send_dis(void)
{
    timepanel_send_message(TIMEPANEL_COMMAND_DIS, NULL, 0);
}

void timepanel_send_parcours_timer(uint32_t duration_ms)
{
    TimepanelU32Payload payload = {
        .value = duration_ms,
    };
    timepanel_send_message(TIMEPANEL_COMMAND_PARCOURS,
                           (const uint8_t *)&payload,
                           sizeof(payload));
}
