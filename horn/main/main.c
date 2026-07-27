/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define BUZZER_DELAY_US_MAX ((int64_t)CONFIG_BUZZER_DELAY_MS * 1000LL)
#define BUZZER_DURATION_US_DEFAULT ((int64_t)CONFIG_BUZZER_DURATION_MS * 1000LL)
#define TIMER_START_MESSAGE_LEN 10
#define TIMER_CONTROL_MESSAGE_LEN 2
#define TIMER_COMMAND_START 0x01
#define TIMER_COMMAND_RESET 0x02
#define TIMER_COMMAND_STOP 0x03
#define HTTP_POST_BUF_LEN 160
#define DNS_PORT 53
#define DNS_TASK_STACK_SIZE 3072
#define DNS_TASK_PRIORITY 4
#define DNS_BUF_LEN 512
#define SOFTAP_IP_ADDR "192.168.4.1"
#define STATUS_LED_RMT_RESOLUTION_HZ 10000000
#define STATUS_LED_BRIGHTNESS 32
#define BUTTON0_GPIO GPIO_NUM_0
#define BUTTON0_TASK_STACK_SIZE 2048
#define BUTTON0_TASK_PRIORITY 5
#define BUTTON0_DEBOUNCE_US 200000

static const uint8_t s_softap_mac[6] = { 0xde, 0x09, 0xdd, 0x09, 0x00, 0x01 };

static const char *TAG = "buzzer_timer";

static esp_timer_handle_t s_delay_timer;
static esp_timer_handle_t s_buzz_stop_timer;
static rmt_channel_handle_t s_status_led_rmt_channel;
static rmt_encoder_handle_t s_status_led_encoder;
static QueueHandle_t s_button0_queue;
static portMUX_TYPE s_config_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_runtime_delay_us = BUZZER_DELAY_US_MAX;
static int64_t s_runtime_buzz_duration_us = BUZZER_DURATION_US_DEFAULT;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} rgb_color_t;

static const rgb_color_t STATUS_LED_BOOT = { .red = STATUS_LED_BRIGHTNESS, .green = 0, .blue = 0 };
static const rgb_color_t STATUS_LED_READY = { .red = 0, .green = STATUS_LED_BRIGHTNESS, .blue = 0 };
static const rgb_color_t STATUS_LED_COUNTING = { .red = 0, .green = STATUS_LED_BRIGHTNESS, .blue = STATUS_LED_BRIGHTNESS };

static esp_err_t status_led_set(rgb_color_t color)
{
    uint8_t payload[3] = { color.green, color.red, color.blue };
    const rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };

    ESP_RETURN_ON_ERROR(rmt_transmit(s_status_led_rmt_channel,
                                     s_status_led_encoder,
                                     payload,
                                     sizeof(payload),
                                     &tx_config),
                        TAG,
                        "Failed to transmit status LED color");
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(s_status_led_rmt_channel, 100),
                        TAG,
                        "Timed out updating status LED");
    return ESP_OK;
}

static int64_t get_runtime_delay_us(void)
{
    portENTER_CRITICAL(&s_config_lock);
    int64_t delay_us = s_runtime_delay_us;
    portEXIT_CRITICAL(&s_config_lock);
    return delay_us;
}

static void set_runtime_delay_ms(uint32_t delay_ms)
{
    portENTER_CRITICAL(&s_config_lock);
    s_runtime_delay_us = (int64_t)delay_ms * 1000LL;
    portEXIT_CRITICAL(&s_config_lock);
}

static int64_t get_runtime_buzz_duration_us(void)
{
    portENTER_CRITICAL(&s_config_lock);
    int64_t duration_us = s_runtime_buzz_duration_us;
    portEXIT_CRITICAL(&s_config_lock);
    return duration_us;
}

static void set_runtime_buzz_duration_ms(uint32_t duration_ms)
{
    portENTER_CRITICAL(&s_config_lock);
    s_runtime_buzz_duration_us = (int64_t)duration_ms * 1000LL;
    portEXIT_CRITICAL(&s_config_lock);
}

static void stop_timer_if_active(esp_timer_handle_t timer)
{
    esp_err_t err = esp_timer_stop(timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to stop timer: %s", esp_err_to_name(err));
    }
}

static void buzzer_set(bool enabled)
{
    gpio_set_level(CONFIG_BUZZER_GPIO, enabled ? 1 : 0);
}

static void buzz_stop_timer_cb(void *arg)
{
    (void)arg;
    buzzer_set(false);
}

static void buzz_start_timer_cb(void *arg)
{
    (void)arg;
    buzzer_set(true);

    stop_timer_if_active(s_buzz_stop_timer);
    esp_err_t err = esp_timer_start_once(s_buzz_stop_timer, get_runtime_buzz_duration_us());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start buzz stop timer: %s", esp_err_to_name(err));
        buzzer_set(false);
    }
}

static void cancel_buzz_state(void)
{
    stop_timer_if_active(s_delay_timer);
    stop_timer_if_active(s_buzz_stop_timer);
    buzzer_set(false);
}

static void schedule_buzz(uint64_t elapsed_ns)
{
    int64_t configured_delay_us = get_runtime_delay_us();
    uint64_t elapsed_us = elapsed_ns / 1000ULL;
    int64_t remaining_us = elapsed_us >= (uint64_t)configured_delay_us
                               ? 0
                               : configured_delay_us - (int64_t)elapsed_us;

    cancel_buzz_state();
    esp_err_t led_err = status_led_set(STATUS_LED_COUNTING);
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set counting LED: %s", esp_err_to_name(led_err));
    }

    if (remaining_us == 0) {
        buzz_start_timer_cb(NULL);
        return;
    }

    esp_err_t err = esp_timer_start_once(s_delay_timer, remaining_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start delay timer: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG,
                 "Buzzer scheduled in %" PRId64 " us (elapsed remote time: %" PRIu64 " ns)",
                 remaining_us,
                 elapsed_ns);
    }
}

static void reset_buzz_timer(void)
{
    cancel_buzz_state();
    esp_err_t led_err = status_led_set(STATUS_LED_READY);
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set ready LED: %s", esp_err_to_name(led_err));
    }
    ESP_LOGI(TAG, "Buzzer timer reset");
}

static void stop_buzz_timer(void)
{
    cancel_buzz_state();
    buzz_start_timer_cb(NULL);
    esp_err_t led_err = status_led_set(STATUS_LED_READY);
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set ready LED: %s", esp_err_to_name(led_err));
    }
    ESP_LOGI(TAG, "Buzzer timer stopped with buzz");
}

static void IRAM_ATTR button0_isr_handler(void *arg)
{
    (void)arg;
    uint32_t gpio_num = BUTTON0_GPIO;
    xQueueSendFromISR(s_button0_queue, &gpio_num, NULL);
}

static void button0_task(void *arg)
{
    (void)arg;

    uint32_t gpio_num = 0;
    int64_t last_press_us = 0;
    while (true) {
        if (xQueueReceive(s_button0_queue, &gpio_num, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_press_us < BUTTON0_DEBOUNCE_US) {
            continue;
        }
        last_press_us = now_us;

        if (gpio_get_level((gpio_num_t)gpio_num) == 0) {
            ESP_LOGI(TAG, "Button 0 pressed");
            stop_buzz_timer();
        }
    }
}

static size_t ieee80211_header_len(const uint8_t *frame, uint16_t len)
{
    if (len < 2) {
        return 0;
    }

    uint16_t frame_ctrl = frame[0] | ((uint16_t)frame[1] << 8);
    uint8_t type = (frame_ctrl >> 2) & 0x03;
    uint8_t subtype = (frame_ctrl >> 4) & 0x0f;
    bool to_ds = (frame_ctrl & (1U << 8)) != 0;
    bool from_ds = (frame_ctrl & (1U << 9)) != 0;
    bool order = (frame_ctrl & (1U << 15)) != 0;
    size_t header_len = 0;

    switch (type) {
    case 0: /* Management */
        header_len = 24;
        break;
    case 1: /* Control */
        if (subtype == 12) {
            header_len = 24; /* Control wrapper */
        } else if (subtype == 10 || subtype == 11) {
            header_len = 16; /* PS-Poll, RTS */
        } else {
            header_len = 10; /* ACK, CTS and similar short control frames */
        }
        break;
    case 2: /* Data */
        header_len = 24;
        if (to_ds && from_ds) {
            header_len += 6;
        }
        if ((subtype & 0x08) != 0) {
            header_len += 2; /* QoS control */
        }
        if (order) {
            header_len += 4; /* HT control */
        }
        break;
    default:
        return 0;
    }

    return header_len <= len ? header_len : 0;
}

typedef enum {
    TIMER_ACTION_NONE,
    TIMER_ACTION_START,
    TIMER_ACTION_RESET,
    TIMER_ACTION_STOP,
} timer_action_t;

static timer_action_t parse_timer_message(const uint8_t *frame, uint16_t len, uint64_t *elapsed_ns)
{
    size_t header_len = ieee80211_header_len(frame, len);
    if (header_len == 0 || len < header_len + TIMER_CONTROL_MESSAGE_LEN) {
        return TIMER_ACTION_NONE;
    }

    const uint8_t *body = frame + header_len;
    if (body[0] != 0xdd) {
        return TIMER_ACTION_NONE;
    }

    switch (body[1]) {
    case TIMER_COMMAND_START:
        if (len < header_len + TIMER_START_MESSAGE_LEN) {
            return TIMER_ACTION_NONE;
        }
        uint64_t value = 0;
        memcpy(&value, body + 2, sizeof(value));
        *elapsed_ns = value;
        return TIMER_ACTION_START;
    case TIMER_COMMAND_RESET:
        return TIMER_ACTION_RESET;
    case TIMER_COMMAND_STOP:
        return TIMER_ACTION_STOP;
    default:
        return TIMER_ACTION_NONE;
    }
}

static void wifi_promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) {
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    uint64_t elapsed_ns = 0;
    timer_action_t action = parse_timer_message(pkt->payload, pkt->rx_ctrl.sig_len, &elapsed_ns);
    if (action == TIMER_ACTION_START) {
        schedule_buzz(elapsed_ns);
    } else if (action == TIMER_ACTION_RESET) {
        reset_buzz_timer();
    } else if (action == TIMER_ACTION_STOP) {
        stop_buzz_timer();
    }
}

static esp_err_t init_buzzer_gpio(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "Failed to configure buzzer GPIO");
    buzzer_set(false);
    return ESP_OK;
}

static esp_err_t init_status_led(void)
{
    const rmt_tx_channel_config_t tx_channel_config = {
        .gpio_num = CONFIG_STATUS_LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = STATUS_LED_RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    const rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 3,
            .level1 = 0,
            .duration1 = 9,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 9,
            .level1 = 0,
            .duration1 = 3,
        },
        .flags.msb_first = 1,
    };

    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_channel_config,
                                           &s_status_led_rmt_channel),
                        TAG,
                        "Failed to create status LED RMT channel");
    ESP_RETURN_ON_ERROR(rmt_new_bytes_encoder(&encoder_config, &s_status_led_encoder),
                        TAG,
                        "Failed to create status LED encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_status_led_rmt_channel),
                        TAG,
                        "Failed to enable status LED RMT channel");
    ESP_RETURN_ON_ERROR(status_led_set(STATUS_LED_BOOT), TAG, "Failed to set boot LED");
    return ESP_OK;
}

static esp_err_t init_timers(void)
{
    const esp_timer_create_args_t delay_args = {
        .callback = buzz_start_timer_cb,
        .name = "buzz_delay",
    };
    const esp_timer_create_args_t stop_args = {
        .callback = buzz_stop_timer_cb,
        .name = "buzz_stop",
    };

    ESP_RETURN_ON_ERROR(esp_timer_create(&delay_args, &s_delay_timer), TAG, "Failed to create delay timer");
    ESP_RETURN_ON_ERROR(esp_timer_create(&stop_args, &s_buzz_stop_timer), TAG, "Failed to create stop timer");
    return ESP_OK;
}

static esp_err_t init_button0_gpio(void)
{
    s_button0_queue = xQueueCreate(4, sizeof(uint32_t));
    if (s_button0_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << BUTTON0_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "Failed to configure button 0 GPIO");

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "Failed to install GPIO ISR service");
    }

    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON0_GPIO, button0_isr_handler, NULL),
                        TAG,
                        "Failed to add button 0 ISR handler");

    BaseType_t created = xTaskCreate(button0_task,
                                    "button0",
                                    BUTTON0_TASK_STACK_SIZE,
                                    NULL,
                                    BUTTON0_TASK_PRIORITY,
                                    NULL);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t init_wifi_softap(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    esp_netif_ip_info_t ip_info;
    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(ap_netif, &ip_info), TAG, "Failed to get AP IP info");
    esp_netif_dns_info_t dns_info = {
        .ip = {
            .type = ESP_IPADDR_TYPE_V4,
            .u_addr.ip4 = ip_info.ip,
        },
    };
    ESP_RETURN_ON_ERROR(esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info),
                        TAG,
                        "Failed to set AP DNS info");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init Wi-Fi");

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.ap.ssid, CONFIG_BUZZER_SOFTAP_SSID, sizeof(wifi_config.ap.ssid));
    strlcpy((char *)wifi_config.ap.password,
            CONFIG_BUZZER_SOFTAP_PASSWORD,
            sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(CONFIG_BUZZER_SOFTAP_SSID);
    wifi_config.ap.channel = CONFIG_BUZZER_SOFTAP_CHANNEL;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = strlen(CONFIG_BUZZER_SOFTAP_PASSWORD) == 0
                                  ? WIFI_AUTH_OPEN
                                  : WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "Failed to set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mac(WIFI_IF_AP, s_softap_mac), TAG, "Failed to set AP MAC");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "Failed to configure AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(CONFIG_BUZZER_SOFTAP_CHANNEL, WIFI_SECOND_CHAN_NONE),
                        TAG,
                        "Failed to set Wi-Fi channel");

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    ESP_RETURN_ON_ERROR(esp_wifi_set_promiscuous_filter(&filter), TAG, "Failed to set promiscuous filter");
    ESP_RETURN_ON_ERROR(esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb),
                        TAG,
                        "Failed to set promiscuous callback");
    ESP_RETURN_ON_ERROR(esp_wifi_set_promiscuous(true), TAG, "Failed to enable promiscuous mode");

    ESP_LOGI(TAG,
             "SoftAP \"%s\" started on channel %d with MAC %02x:%02x:%02x:%02x:%02x:%02x",
             CONFIG_BUZZER_SOFTAP_SSID,
             CONFIG_BUZZER_SOFTAP_CHANNEL,
             s_softap_mac[0],
             s_softap_mac[1],
             s_softap_mac[2],
             s_softap_mac[3],
             s_softap_mac[4],
             s_softap_mac[5]);
    return ESP_OK;
}

static int build_dns_response(uint8_t *buf, int query_len)
{
    if (query_len < 12) {
        return -1;
    }

    int question_end = 12;
    while (question_end < query_len && buf[question_end] != 0) {
        question_end += buf[question_end] + 1;
    }
    question_end += 5; /* terminating zero plus QTYPE and QCLASS */
    if (question_end > query_len || question_end + 16 > DNS_BUF_LEN) {
        return -1;
    }

    buf[2] = 0x81; /* Standard query response, recursion desired/available */
    buf[3] = 0x80;
    buf[4] = 0x00;
    buf[5] = 0x01; /* QDCOUNT */
    buf[6] = 0x00;
    buf[7] = 0x01; /* ANCOUNT */
    buf[8] = 0x00;
    buf[9] = 0x00; /* NSCOUNT */
    buf[10] = 0x00;
    buf[11] = 0x00; /* ARCOUNT */

    uint8_t *answer = buf + question_end;
    answer[0] = 0xc0;
    answer[1] = 0x0c; /* compressed name pointer to original question */
    answer[2] = 0x00;
    answer[3] = 0x01; /* TYPE A */
    answer[4] = 0x00;
    answer[5] = 0x01; /* CLASS IN */
    answer[6] = 0x00;
    answer[7] = 0x00;
    answer[8] = 0x00;
    answer[9] = 0x3c; /* TTL 60 seconds */
    answer[10] = 0x00;
    answer[11] = 0x04; /* RDLENGTH */
    inet_pton(AF_INET, SOFTAP_IP_ADDR, answer + 12);

    return question_end + 16;
}

static void captive_dns_task(void *arg)
{
    (void)arg;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    uint8_t buf[DNS_BUF_LEN];
    while (true) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&source_addr, &socklen);
        if (len <= 0) {
            continue;
        }

        int response_len = build_dns_response(buf, len);
        if (response_len > 0) {
            sendto(sock, buf, response_len, 0, (struct sockaddr *)&source_addr, socklen);
        }
    }
}

static esp_err_t start_captive_dns(void)
{
    BaseType_t created = xTaskCreate(captive_dns_task,
                                    "captive_dns",
                                    DNS_TASK_STACK_SIZE,
                                    NULL,
                                    DNS_TASK_PRIORITY,
                                    NULL);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char number_buf[16];
    char delay_buf[16];
    char duration_buf[16];
    int delay_ms = (int)(get_runtime_delay_us() / 1000LL);
    int buzz_duration_ms = (int)(get_runtime_buzz_duration_us() / 1000LL);
    int written = snprintf(delay_buf,
                           sizeof(delay_buf),
                           "%d.%03d",
                           delay_ms / 1000,
                           delay_ms % 1000);
    if (written < 0 || written >= (int)sizeof(delay_buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delay value too large");
    }

    written = snprintf(duration_buf, sizeof(duration_buf), "%d", buzz_duration_ms);
    if (written < 0 || written >= (int)sizeof(duration_buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Duration value too large");
    }

    httpd_resp_set_type(req, "text/html");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(
                            req,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Buzzer Timer</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#f5f7fb;color:#172033}"
        "main{max-width:520px;margin:0 auto;padding:32px 18px}"
        "section{background:#fff;border:1px solid #d9e0ea;border-radius:8px;padding:22px;box-shadow:0 8px 24px #1b2b4a14}"
        "h1{font-size:28px;margin:0 0 18px}label{display:block;font-weight:650;margin:16px 0 8px}"
        "input{box-sizing:border-box;width:100%%;font-size:18px;padding:10px;border:1px solid #aeb8c8;border-radius:6px}"
        "button{display:block;margin-top:24px;font-size:17px;padding:10px 14px;border:0;border-radius:6px;background:#1267d8;color:white}"
        "p{line-height:1.45}.status{min-height:24px;color:#26613f}"
        "</style></head><body><main><section>"
        "<h1>Buzzer Timer</h1><p>SoftAP channel "),
                        TAG,
                        "Failed to send root page");

    written = snprintf(number_buf, sizeof(number_buf), "%d", CONFIG_BUZZER_SOFTAP_CHANNEL);
    if (written < 0 || written >= (int)sizeof(number_buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Channel value too large");
    }
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, number_buf), TAG, "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, ". Current buzz delay is "),
                        TAG,
                        "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, delay_buf), TAG, "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(
                            req,
        " s. Buzz time is "),
                        TAG,
                        "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, duration_buf), TAG, "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(
                            req,
        " ms.</p>"
        "<form id=\"config-form\">"
        "<label for=\"delay\">Buzz delay in seconds</label>"
        "<input id=\"delay\" name=\"delay\" type=\"number\" min=\"0\" max=\"3600\" step=\"0.001\" value=\""),
                        TAG,
                        "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, delay_buf), TAG, "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(
                            req,
        "\" required>"
        "<label for=\"duration\">Buzz time in milliseconds</label>"
        "<input id=\"duration\" name=\"duration\" type=\"number\" min=\"1\" max=\"60000\" step=\"1\" value=\""),
                        TAG,
                        "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(req, duration_buf), TAG, "Failed to send root page");

    ESP_RETURN_ON_ERROR(httpd_resp_sendstr_chunk(
                            req,
        "\" required><button type=\"submit\">Save settings</button>"
        "</form><p class=\"status\" id=\"status\"></p></section></main><script>"
        "const form=document.getElementById('config-form');"
        "const statusEl=document.getElementById('status');"
        "form.addEventListener('submit',async(e)=>{"
        "e.preventDefault();statusEl.textContent='Saving...';"
        "const delay_seconds=Number(document.getElementById('delay').value);"
        "const buzz_duration_ms=Number(document.getElementById('duration').value);"
        "const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({delay_seconds,buzz_duration_ms})});"
        "if(!r.ok){statusEl.textContent='Save failed';return;}"
        "const cfg=await r.json();document.getElementById('delay').value=cfg.delay_seconds;"
        "document.getElementById('duration').value=cfg.buzz_duration_ms;"
        "statusEl.textContent='Saved for future triggers';"
        "});"
        "</script></body></html>"),
                        TAG,
                        "Failed to send root page");

    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    char json[160];
    int delay_ms = (int)(get_runtime_delay_us() / 1000LL);
    int buzz_duration_ms = (int)(get_runtime_buzz_duration_us() / 1000LL);
    int written = snprintf(json,
                           sizeof(json),
                           "{\"delay_seconds\":\"%d.%03d\",\"delay_ms\":%d,\"buzz_duration_ms\":%d}\n",
                           delay_ms / 1000,
                           delay_ms % 1000,
                           delay_ms,
                           buzz_duration_ms);
    if (written < 0 || written >= (int)sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON too large");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static const char *find_json_number_value(const char *body, const char *key)
{
    const char *value = strstr(body, key);
    if (value == NULL) {
        return NULL;
    }

    while (*value != '\0' && !isdigit((unsigned char)*value)) {
        value++;
    }
    return *value == '\0' ? NULL : value;
}

static bool parse_uint_field(const char *body, const char *key, uint32_t max_value, uint32_t *value_out)
{
    const char *value = find_json_number_value(body, key);
    if (value == NULL) {
        return false;
    }

    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || parsed > max_value) {
        return false;
    }

    *value_out = (uint32_t)parsed;
    return true;
}

static bool parse_delay_seconds_field(const char *body, uint32_t *delay_ms)
{
    const char *value = find_json_number_value(body, "delay_seconds");
    if (value == NULL) {
        return parse_uint_field(body, "delay_ms", 3600000, delay_ms);
    }

    char *end = NULL;
    unsigned long seconds = strtoul(value, &end, 10);
    if (end == value || seconds > 3600UL) {
        return false;
    }

    uint32_t fraction_ms = 0;
    if (*end == '.') {
        end++;
        uint32_t scale = 100;
        while (isdigit((unsigned char)*end) && scale > 0) {
            fraction_ms += (uint32_t)(*end - '0') * scale;
            scale /= 10;
            end++;
        }
    }

    uint64_t parsed_ms = (uint64_t)seconds * 1000ULL + fraction_ms;
    if (parsed_ms > 3600000ULL) {
        return false;
    }

    *delay_ms = (uint32_t)parsed_ms;
    return true;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= HTTP_POST_BUF_LEN) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    }

    char body[HTTP_POST_BUF_LEN] = { 0 };
    int received_total = 0;
    while (received_total < req->content_len) {
        int received = httpd_req_recv(req,
                                      body + received_total,
                                      req->content_len - received_total);
        if (received <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
        }
        received_total += received;
    }
    body[received_total] = '\0';

    uint32_t delay_ms = 0;
    uint32_t buzz_duration_ms = 0;
    if (!parse_delay_seconds_field(body, &delay_ms)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected delay_seconds between 0 and 3600");
    }
    if (!parse_uint_field(body, "buzz_duration_ms", 60000, &buzz_duration_ms) || buzz_duration_ms == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected buzz_duration_ms between 1 and 60000");
    }

    set_runtime_delay_ms(delay_ms);
    set_runtime_buzz_duration_ms(buzz_duration_ms);
    ESP_LOGI(TAG,
             "Runtime delay updated to %" PRIu32 " ms, buzz duration to %" PRIu32 " ms",
             delay_ms,
             buzz_duration_ms);
    return config_get_handler(req);
}

static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    config.stack_size = 6144;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "Failed to start HTTP server");

    const httpd_uri_t root_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    const httpd_uri_t config_get_uri = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
    };
    const httpd_uri_t config_post_uri = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = config_post_handler,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &config_get_uri),
                        TAG,
                        "Failed to register GET /api/config");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &config_post_uri),
                        TAG,
                        "Failed to register POST /api/config");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root_uri), TAG, "Failed to register /*");

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_status_led());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(init_buzzer_gpio());
    ESP_ERROR_CHECK(init_timers());
    ESP_ERROR_CHECK(init_button0_gpio());
    ESP_ERROR_CHECK(init_wifi_softap());
    ESP_ERROR_CHECK(start_captive_dns());
    ESP_ERROR_CHECK(start_webserver());
    ESP_ERROR_CHECK(status_led_set(STATUS_LED_READY));

    ESP_LOGI(TAG,
             "Ready. GPIO %d, status LED GPIO %d, default delay %d ms, buzz duration %d ms",
             CONFIG_BUZZER_GPIO,
             CONFIG_STATUS_LED_GPIO,
             CONFIG_BUZZER_DELAY_MS,
             CONFIG_BUZZER_DURATION_MS);
}
