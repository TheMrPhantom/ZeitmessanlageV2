#include "timepanel_common.h"

#include <cstring>

#include "TimepanelProtocol.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace {

constexpr size_t DUPLICATE_CACHE_SIZE = 16;
constexpr int64_t DUPLICATE_WINDOW_US = 2000000;

struct PacketSignature {
    bool valid = false;
    uint8_t source_mac[6]{};
    uint16_t sequence_number = 0;
    uint8_t command = 0;
    int64_t received_at_us = 0;
};

struct WirelessCommand {
    uint8_t command = 0;
    TimepanelCompetitorPayload competitor{};
    uint32_t value = 0;
};

uint8_t s_timepanel_mac[6] = {
    TIMEPANEL_MAC_PREFIX_0,
    TIMEPANEL_MAC_PREFIX_1,
    TIMEPANEL_MAC_PREFIX_2,
    TIMEPANEL_MAC_PREFIX_3,
    static_cast<uint8_t>((CONFIG_TIMEPANEL_MAC_ID >> 8) & 0xff),
    static_cast<uint8_t>(CONFIG_TIMEPANEL_MAC_ID & 0xff),
};

QueueHandle_t s_wireless_command_queue = nullptr;
std::array<PacketSignature, DUPLICATE_CACHE_SIZE> s_duplicate_cache{};
size_t s_duplicate_cache_next = 0;

size_t ieee80211_header_len(const uint8_t *frame, uint16_t len)
{
    if (len < 2) {
        return 0;
    }

    const uint16_t frame_ctrl = frame[0] | (static_cast<uint16_t>(frame[1]) << 8);
    const uint8_t type = (frame_ctrl >> 2) & 0x03;
    const uint8_t subtype = (frame_ctrl >> 4) & 0x0f;
    const bool to_ds = (frame_ctrl & (1 << 8)) != 0;
    const bool from_ds = (frame_ctrl & (1 << 9)) != 0;
    const bool order = (frame_ctrl & (1 << 15)) != 0;

    size_t header_len = 0;
    switch (type) {
    case 0:
        header_len = 24;
        if (subtype == 8 || subtype == 5) {
            header_len += 12;
        }
        break;
    case 1:
        header_len = (subtype == 13 || subtype == 12 || subtype == 11) ? 10 : 16;
        break;
    case 2:
        header_len = 24;
        if (to_ds && from_ds) {
            header_len += 6;
        }
        if ((subtype & 0x08) != 0) {
            header_len += 2;
        }
        if (order) {
            header_len += 4;
        }
        break;
    default:
        return 0;
    }

    return header_len <= len ? header_len : 0;
}

bool timepanel_frame_is_duplicate(const uint8_t *frame, uint8_t command)
{
    const uint8_t *source_mac = frame + 10;
    const uint16_t sequence_control =
        frame[22] | (static_cast<uint16_t>(frame[23]) << 8);
    const uint16_t sequence_number = sequence_control >> 4;
    const int64_t now_us = esp_timer_get_time();

    for (PacketSignature &signature : s_duplicate_cache) {
        if (!signature.valid ||
            signature.sequence_number != sequence_number ||
            signature.command != command ||
            std::memcmp(signature.source_mac,
                        source_mac,
                        sizeof(signature.source_mac)) != 0) {
            continue;
        }

        if (now_us - signature.received_at_us <= DUPLICATE_WINDOW_US) {
            return true;
        }

        signature.received_at_us = now_us;
        return false;
    }

    PacketSignature &signature = s_duplicate_cache[s_duplicate_cache_next];
    std::memcpy(signature.source_mac, source_mac, sizeof(signature.source_mac));
    signature.sequence_number = sequence_number;
    signature.command = command;
    signature.received_at_us = now_us;
    signature.valid = true;
    s_duplicate_cache_next = (s_duplicate_cache_next + 1) % DUPLICATE_CACHE_SIZE;
    return false;
}

void copy_payload_string(char *target,
                         size_t target_len,
                         const char *source,
                         size_t source_len)
{
    const size_t copy_len = std::min(target_len - 1, source_len);
    std::memcpy(target, source, copy_len);
    target[copy_len] = '\0';
}

bool parse_wireless_command(const uint8_t *frame,
                            uint16_t len,
                            WirelessCommand &command)
{
    if (len < TIMEPANEL_FRAME_HEADER_LEN) {
        return false;
    }

    const uint16_t frame_ctrl =
        frame[0] | (static_cast<uint16_t>(frame[1]) << 8);
    const uint8_t type = (frame_ctrl >> 2) & 0x03;
    const uint8_t subtype = (frame_ctrl >> 4) & 0x0f;
    if (type != 0 || subtype != 13 ||
        std::memcmp(frame + 4, s_timepanel_mac, sizeof(s_timepanel_mac)) != 0 ||
        std::memcmp(frame + 16, s_timepanel_mac, sizeof(s_timepanel_mac)) != 0) {
        return false;
    }

    const size_t header_len = ieee80211_header_len(frame, len);
    if (header_len == 0 || len < header_len + TIMEPANEL_CONTROL_MESSAGE_LEN) {
        return false;
    }

    const uint8_t *body = frame + header_len;
    if (body[0] != TIMEPANEL_VENDOR_MARKER) {
        return false;
    }

    if (timepanel_frame_is_duplicate(frame, body[1])) {
        return false;
    }

    command.command = body[1];
    const uint8_t *payload = body + TIMEPANEL_CONTROL_MESSAGE_LEN;
    const size_t payload_len = len - header_len - TIMEPANEL_CONTROL_MESSAGE_LEN;

    switch (command.command) {
    case TIMEPANEL_COMMAND_COMPETITOR:
    case TIMEPANEL_COMMAND_RESET: {
        if (payload_len < sizeof(TimepanelCompetitorPayload)) {
            return false;
        }
        const auto *received =
            reinterpret_cast<const TimepanelCompetitorPayload *>(payload);
        copy_payload_string(command.competitor.first_name,
                            sizeof(command.competitor.first_name),
                            received->first_name,
                            sizeof(received->first_name));
        copy_payload_string(command.competitor.last_name,
                            sizeof(command.competitor.last_name),
                            received->last_name,
                            sizeof(received->last_name));
        copy_payload_string(command.competitor.dog_name,
                            sizeof(command.competitor.dog_name),
                            received->dog_name,
                            sizeof(received->dog_name));
        return true;
    }
    case TIMEPANEL_COMMAND_START:
    case TIMEPANEL_COMMAND_STOP:
    case TIMEPANEL_COMMAND_PARCOURS: {
        if (payload_len < sizeof(TimepanelU32Payload)) {
            return false;
        }
        TimepanelU32Payload payload_value{};
        std::memcpy(&payload_value, payload, sizeof(payload_value));
        command.value = payload_value.value;
        return true;
    }
    case TIMEPANEL_COMMAND_FAULT:
    case TIMEPANEL_COMMAND_REFUSAL: {
        if (payload_len < sizeof(TimepanelU16Payload)) {
            return false;
        }
        TimepanelU16Payload payload_value{};
        std::memcpy(&payload_value, payload, sizeof(payload_value));
        command.value = payload_value.value;
        return true;
    }
    case TIMEPANEL_COMMAND_DIS:
        return true;
    default:
        return false;
    }
}

void wifi_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT || s_wireless_command_queue == nullptr) {
        return;
    }

    const auto *pkt = static_cast<const wifi_promiscuous_pkt_t *>(buf);
    WirelessCommand command{};
    if (parse_wireless_command(pkt->payload, pkt->rx_ctrl.sig_len, command)) {
        xQueueSend(s_wireless_command_queue, &command, 0);
    }
}

void wireless_command_task(void *)
{
    WirelessCommand command{};
    while (true) {
        if (xQueueReceive(s_wireless_command_queue, &command, portMAX_DELAY) !=
            pdTRUE) {
            continue;
        }

        switch (command.command) {
        case TIMEPANEL_COMMAND_COMPETITOR:
            apply_runner_command_competitor(command.competitor.first_name,
                                            command.competitor.last_name,
                                            command.competitor.dog_name);
            break;
        case TIMEPANEL_COMMAND_RESET:
            apply_runner_command_reset(command.competitor.first_name,
                                       command.competitor.last_name,
                                       command.competitor.dog_name);
            break;
        case TIMEPANEL_COMMAND_START:
            apply_runner_command_start(static_cast<int>(command.value));
            break;
        case TIMEPANEL_COMMAND_STOP:
            apply_runner_command_stop(static_cast<int>(command.value));
            break;
        case TIMEPANEL_COMMAND_FAULT:
            apply_runner_command_fault(static_cast<int>(command.value));
            break;
        case TIMEPANEL_COMMAND_REFUSAL:
            apply_runner_command_refusal(static_cast<int>(command.value));
            break;
        case TIMEPANEL_COMMAND_DIS:
            apply_runner_command_dis();
            break;
        case TIMEPANEL_COMMAND_PARCOURS:
            apply_parcours_command_start(command.value);
            break;
        default:
            break;
        }
    }
}

} // namespace

void initialize_wireless_receiver()
{
    s_wireless_command_queue = xQueueCreate(10, sizeof(WirelessCommand));
    if (s_wireless_command_queue == nullptr) {
        ESP_LOGE(TAG, "Timepanel wireless command queue could not be created");
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize esp-netif: %s", esp_err_to_name(err));
        return;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
        return;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_config);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mac(WIFI_IF_STA, s_timepanel_mac));

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_set_channel(CONFIG_TIMEPANEL_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi channel: %s", esp_err_to_name(err));
        return;
    }

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_promiscuous(true));

    const BaseType_t task_created =
        xTaskCreate(wireless_command_task,
                    "timepanel_wireless",
                    4096,
                    nullptr,
                    6,
                    nullptr);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Timepanel wireless task could not be started");
        return;
    }

    ESP_LOGI(TAG,
             "Timepanel receiver listening on Wi-Fi channel %d with MAC %02x:%02x:%02x:%02x:%02x:%02x",
             CONFIG_TIMEPANEL_WIFI_CHANNEL,
             s_timepanel_mac[0],
             s_timepanel_mac[1],
             s_timepanel_mac[2],
             s_timepanel_mac[3],
             s_timepanel_mac[4],
             s_timepanel_mac[5]);
}
