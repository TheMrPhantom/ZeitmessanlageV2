/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define BUZZER_DELAY_US_MAX ((int64_t)CONFIG_BUZZER_DELAY_MS * 1000LL)
#define BUZZER_DURATION_US_DEFAULT ((int64_t)CONFIG_BUZZER_DURATION_MS * 1000LL)
#define BUZZER_SECOND_DELAY_US_DEFAULT ((int64_t)CONFIG_BUZZER_SECOND_DELAY_MS * 1000LL)
#define TIMER_START_MESSAGE_LEN 10
#define TIMER_CONTROL_MESSAGE_LEN 2
#define TIMER_COMMAND_START 0x01
#define TIMER_COMMAND_RESET 0x02
#define TIMER_COMMAND_STOP 0x03

#define HTTP_POST_BUF_LEN 256
#define HTTP_ROOT_BUF_LEN 4096

#define TIMER_FRAME_HEADER_LEN 24
#define TIMER_DUPLICATE_CACHE_SIZE 16
#define TIMER_DUPLICATE_WINDOW_US 2000000

#define DNS_PORT 53
#define DNS_TASK_STACK_SIZE 4096
#define DNS_TASK_PRIORITY 4
#define DNS_BUF_LEN 512
#define SOFTAP_IP_ADDR "192.168.4.1"
#define STATUS_LED_RMT_RESOLUTION_HZ 10000000
#define STATUS_LED_BRIGHTNESS 32
#define BUTTON0_GPIO GPIO_NUM_0
#define BUTTON0_TASK_STACK_SIZE 3072
#define BUTTON0_TASK_PRIORITY 5
#define BUTTON0_DEBOUNCE_US 200000

#define CONTROL_QUEUE_LEN 24
#define CONTROL_START_CAPACITY 8
#define CONTROL_URGENT_CAPACITY 4
#define CONTROL_TASK_STACK_SIZE 4096
#define CONTROL_TASK_PRIORITY 6
#define HTTP_RECV_TIMEOUT_RETRIES 3

#if CONFIG_BUZZER_GPIO == CONFIG_STATUS_LED_GPIO || CONFIG_BUZZER_GPIO == 0 || \
    CONFIG_STATUS_LED_GPIO == 0
#error "Buzzer, status LED, and button GPIOs must be distinct"
#endif

#ifdef CONFIG_BUZZER_SECOND_ENABLED
#define BUZZER_SECOND_ENABLED_DEFAULT true
#else
#define BUZZER_SECOND_ENABLED_DEFAULT false
#endif

static const uint8_t s_softap_mac[6] = { 0xde, 0x09, 0xdd, 0x09, 0x00, 0x01 };

static const char *TAG = "buzzer_timer";

static esp_timer_handle_t s_delay_timer;
static esp_timer_handle_t s_buzz_stop_timer;
static esp_timer_handle_t s_second_buzz_timer;
static rmt_channel_handle_t s_status_led_rmt_channel;
static rmt_encoder_handle_t s_status_led_encoder;
static QueueHandle_t s_button0_queue;
static QueueHandle_t s_control_queue;
static SemaphoreHandle_t s_control_start_slots;
static SemaphoreHandle_t s_control_urgent_slots;
static portMUX_TYPE s_config_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_runtime_delay_us = BUZZER_DELAY_US_MAX;
static int64_t s_runtime_buzz_duration_us = BUZZER_DURATION_US_DEFAULT;
static bool s_runtime_second_buzz_enabled = BUZZER_SECOND_ENABLED_DEFAULT;
static int64_t s_runtime_second_buzz_delay_us = BUZZER_SECOND_DELAY_US_DEFAULT;
static bool s_buzz_fired;
static bool s_second_buzz_pending;
static int64_t s_second_buzz_pending_delay_us;
static bool s_have_last_start_elapsed;
static uint64_t s_last_start_elapsed_ns;
static size_t s_duplicate_cache_next;
static atomic_uint s_dropped_control_commands;

typedef enum {
    TIMER_ACTION_NONE,
    TIMER_ACTION_START,
    TIMER_ACTION_RESET,
    TIMER_ACTION_STOP,
} timer_action_t;

typedef enum {
    CONTROL_EVENT_START,
    CONTROL_EVENT_RESET,
    CONTROL_EVENT_STOP,
    CONTROL_EVENT_BUTTON_STOP,
    CONTROL_EVENT_DELAY_EXPIRED,
    CONTROL_EVENT_BUZZ_STOP_EXPIRED,
    CONTROL_EVENT_SECOND_BUZZ_EXPIRED,
} control_event_type_t;

typedef struct {
    control_event_type_t type;
    uint64_t elapsed_ns;
} control_event_t;

typedef enum {
    BUZZ_PHASE_IDLE,
    BUZZ_PHASE_COUNTING,
    BUZZ_PHASE_BUZZING,
    BUZZ_PHASE_WAITING_SECOND,
} buzz_phase_t;

static buzz_phase_t s_buzz_phase;

typedef struct {
    uint8_t source_mac[6];
    uint16_t sequence_number;
    uint8_t command;
    int64_t received_at_us;
    bool valid;
} timer_packet_signature_t;

static timer_packet_signature_t s_duplicate_cache[TIMER_DUPLICATE_CACHE_SIZE];

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

static void get_runtime_second_buzz_config(bool *enabled, int64_t *delay_us)
{
    portENTER_CRITICAL(&s_config_lock);
    *enabled = s_runtime_second_buzz_enabled;
    *delay_us = s_runtime_second_buzz_delay_us;
    portEXIT_CRITICAL(&s_config_lock);
}

static void set_runtime_second_buzz_config(bool enabled, uint32_t delay_ms)
{
    portENTER_CRITICAL(&s_config_lock);
    s_runtime_second_buzz_enabled = enabled;
    s_runtime_second_buzz_delay_us = (int64_t)delay_ms * 1000LL;
    portEXIT_CRITICAL(&s_config_lock);
}

static void stop_timer_if_active(esp_timer_handle_t timer)
{
    esp_err_t err = esp_timer_stop(timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to stop timer: %s", esp_err_to_name(err));
    }
}

static bool start_message_should_schedule(uint64_t elapsed_ns)
{
    if (!s_have_last_start_elapsed || elapsed_ns < s_last_start_elapsed_ns) {
        s_buzz_fired = false;
        s_second_buzz_pending = false;
        s_second_buzz_pending_delay_us = 0;
    }
    s_have_last_start_elapsed = true;
    s_last_start_elapsed_ns = elapsed_ns;
    return !s_buzz_fired;
}

static void mark_buzz_fired(bool fired)
{
    s_buzz_fired = fired;
}

static void arm_second_buzz(bool pending, int64_t delay_us)
{
    s_second_buzz_pending = pending;
    s_second_buzz_pending_delay_us = pending ? delay_us : 0;
}

static bool consume_second_buzz(int64_t *delay_us)
{
    bool pending = s_second_buzz_pending;
    *delay_us = s_second_buzz_pending_delay_us;
    s_second_buzz_pending = false;
    s_second_buzz_pending_delay_us = 0;
    return pending;
}

static void clear_second_buzz(void)
{
    arm_second_buzz(false, 0);
}

static void reset_buzz_sequence(void)
{
    s_buzz_fired = false;
    s_second_buzz_pending = false;
    s_second_buzz_pending_delay_us = 0;
    s_have_last_start_elapsed = false;
    s_last_start_elapsed_ns = 0;
}

static esp_err_t buzzer_set_level(bool enabled)
{
    if (enabled) {
        ESP_RETURN_ON_ERROR(gpio_hold_dis(CONFIG_BUZZER_GPIO),
                            TAG,
                            "Failed to release buzzer GPIO hold");
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_BUZZER_GPIO, enabled ? 1 : 0),
                        TAG,
                        "Failed to set buzzer GPIO level");

    if (!enabled) {
        ESP_RETURN_ON_ERROR(gpio_hold_en(CONFIG_BUZZER_GPIO),
                            TAG,
                            "Failed to hold buzzer GPIO low");
    }
    return ESP_OK;
}

static esp_err_t buzzer_set(bool enabled)
{
    esp_err_t err = buzzer_set_level(enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to turn buzzer %s: %s",
                 enabled ? "on" : "off",
                 esp_err_to_name(err));
    }
    return err;
}

static void start_buzz(bool allow_second_buzz)
{
    int64_t duration_us = get_runtime_buzz_duration_us();
    bool second_buzz_enabled = false;
    int64_t second_buzz_delay_us = 0;

    get_runtime_second_buzz_config(&second_buzz_enabled, &second_buzz_delay_us);
    arm_second_buzz(allow_second_buzz && second_buzz_enabled, second_buzz_delay_us);

    if (buzzer_set(true) != ESP_OK) {
        mark_buzz_fired(false);
        clear_second_buzz();
        (void)buzzer_set(false);
        s_buzz_phase = BUZZ_PHASE_IDLE;
        return;
    }

    stop_timer_if_active(s_buzz_stop_timer);
    esp_err_t err = esp_timer_start_once(s_buzz_stop_timer, duration_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start buzz stop timer: %s", esp_err_to_name(err));
        mark_buzz_fired(false);
        clear_second_buzz();
        (void)buzzer_set(false);
        s_buzz_phase = BUZZ_PHASE_IDLE;
    } else {
        mark_buzz_fired(true);
        s_buzz_phase = BUZZ_PHASE_BUZZING;
        ESP_LOGI(TAG, "Buzzer started for %" PRId64 " us", duration_us);
    }
}

static void finish_buzz(void)
{
    (void)buzzer_set(false);
    s_buzz_phase = BUZZ_PHASE_IDLE;

    esp_err_t led_err = status_led_set(STATUS_LED_READY);
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set ready LED after buzz: %s", esp_err_to_name(led_err));
    }

    int64_t second_buzz_delay_us = 0;
    if (consume_second_buzz(&second_buzz_delay_us)) {
        esp_err_t timer_err = esp_timer_start_once(s_second_buzz_timer, second_buzz_delay_us);
        if (timer_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start second buzz timer: %s", esp_err_to_name(timer_err));
        } else {
            s_buzz_phase = BUZZ_PHASE_WAITING_SECOND;
            ESP_LOGI(TAG, "Second buzz scheduled in %" PRId64 " us", second_buzz_delay_us);
        }
    }

    ESP_LOGI(TAG, "Buzzer stopped");
}

static void enqueue_timer_event(control_event_type_t type)
{
    const control_event_t event = {
        .type = type,
    };

    if (xQueueSendToFront(s_control_queue, &event, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Control queue full while handling timer event %d", (int)type);
        if (type == CONTROL_EVENT_BUZZ_STOP_EXPIRED) {
            /*
             * Fail safe without racing the control task's multi-step GPIO hold
             * sequence. A later serialized command will restore the low hold.
             */
            esp_err_t err = gpio_set_level(CONFIG_BUZZER_GPIO, 0);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Emergency buzzer shutdown failed: %s", esp_err_to_name(err));
            }
        }
    }
}

static void buzz_start_timer_cb(void *arg)
{
    (void)arg;
    enqueue_timer_event(CONTROL_EVENT_DELAY_EXPIRED);
}

static void buzz_stop_timer_cb(void *arg)
{
    (void)arg;
    enqueue_timer_event(CONTROL_EVENT_BUZZ_STOP_EXPIRED);
}

static void second_buzz_timer_cb(void *arg)
{
    (void)arg;
    enqueue_timer_event(CONTROL_EVENT_SECOND_BUZZ_EXPIRED);
}

static void cancel_buzz_state(void)
{
    s_buzz_phase = BUZZ_PHASE_IDLE;
    stop_timer_if_active(s_delay_timer);
    stop_timer_if_active(s_buzz_stop_timer);
    stop_timer_if_active(s_second_buzz_timer);
    (void)buzzer_set(false);
}

static void schedule_buzz(uint64_t elapsed_ns)
{
    if (!start_message_should_schedule(elapsed_ns)) {
        return;
    }

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
        start_buzz(true);
        return;
    }

    esp_err_t err = esp_timer_start_once(s_delay_timer, remaining_us);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start delay timer: %s", esp_err_to_name(err));
        (void)status_led_set(STATUS_LED_READY);
    } else {
        s_buzz_phase = BUZZ_PHASE_COUNTING;
        ESP_LOGI(TAG,
                 "Buzzer scheduled in %" PRId64 " us (elapsed remote time: %" PRIu64 " ns)",
                 remaining_us,
                 elapsed_ns);
    }
}

static void reset_buzz_timer(void)
{
    reset_buzz_sequence();
    cancel_buzz_state();
    esp_err_t led_err = status_led_set(STATUS_LED_READY);
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set ready LED: %s", esp_err_to_name(led_err));
    }
    ESP_LOGI(TAG, "Buzzer timer reset");
}

static void stop_buzz_timer(void)
{
    clear_second_buzz();
    cancel_buzz_state();
    esp_err_t led_err = status_led_set(STATUS_LED_READY);
    if (led_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set ready LED: %s", esp_err_to_name(led_err));
    }
    start_buzz(false);
}

static bool enqueue_control_command(timer_action_t action, uint64_t elapsed_ns)
{
    control_event_t event = {
        .elapsed_ns = elapsed_ns,
    };
    SemaphoreHandle_t slots = NULL;
    BaseType_t queued = pdFALSE;

    switch (action) {
    case TIMER_ACTION_START:
        event.type = CONTROL_EVENT_START;
        slots = s_control_start_slots;
        break;
    case TIMER_ACTION_RESET:
        event.type = CONTROL_EVENT_RESET;
        slots = s_control_urgent_slots;
        break;
    case TIMER_ACTION_STOP:
        event.type = CONTROL_EVENT_STOP;
        slots = s_control_urgent_slots;
        break;
    default:
        return false;
    }

    if (xSemaphoreTake(slots, 0) != pdTRUE) {
        atomic_fetch_add_explicit(&s_dropped_control_commands, 1, memory_order_relaxed);
        return false;
    }

    /* External commands stay FIFO; timer expirations are the only front-queued events. */
    queued = xQueueSendToBack(s_control_queue, &event, 0);

    if (queued != pdTRUE) {
        (void)xSemaphoreGive(slots);
        atomic_fetch_add_explicit(&s_dropped_control_commands, 1, memory_order_relaxed);
        return false;
    }
    return true;
}

static void enqueue_button_stop(void)
{
    const control_event_t event = {
        .type = CONTROL_EVENT_BUTTON_STOP,
    };

    if (xQueueSendToFront(s_control_queue, &event, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Control queue full while handling the physical stop button");
    }
}

static void release_control_command_slot(control_event_type_t type)
{
    if (type == CONTROL_EVENT_START) {
        (void)xSemaphoreGive(s_control_start_slots);
    } else if (type == CONTROL_EVENT_RESET || type == CONTROL_EVENT_STOP) {
        (void)xSemaphoreGive(s_control_urgent_slots);
    }
}

static void maybe_log_dropped_control_commands(void)
{
    static int64_t last_log_us;
    int64_t now_us = esp_timer_get_time();

    if (now_us - last_log_us < 1000000) {
        return;
    }

    unsigned int dropped = atomic_exchange_explicit(&s_dropped_control_commands,
                                                     0,
                                                     memory_order_relaxed);
    if (dropped > 0) {
        ESP_LOGW(TAG, "Dropped %u timer command(s) because the control queue was busy", dropped);
    }
    last_log_us = now_us;
}

static void buzzer_control_task(void *arg)
{
    (void)arg;

    control_event_t event;
    while (true) {
        if (xQueueReceive(s_control_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        release_control_command_slot(event.type);
        maybe_log_dropped_control_commands();

        switch (event.type) {
        case CONTROL_EVENT_START:
            schedule_buzz(event.elapsed_ns);
            break;
        case CONTROL_EVENT_RESET:
            reset_buzz_timer();
            break;
        case CONTROL_EVENT_STOP:
        case CONTROL_EVENT_BUTTON_STOP:
            stop_buzz_timer();
            break;
        case CONTROL_EVENT_DELAY_EXPIRED:
            if (s_buzz_phase == BUZZ_PHASE_COUNTING &&
                !esp_timer_is_active(s_delay_timer)) {
                start_buzz(true);
            }
            break;
        case CONTROL_EVENT_BUZZ_STOP_EXPIRED:
            if (s_buzz_phase == BUZZ_PHASE_BUZZING &&
                !esp_timer_is_active(s_buzz_stop_timer)) {
                finish_buzz();
            }
            break;
        case CONTROL_EVENT_SECOND_BUZZ_EXPIRED:
            if (s_buzz_phase == BUZZ_PHASE_WAITING_SECOND &&
                !esp_timer_is_active(s_second_buzz_timer)) {
                start_buzz(false);
            }
            break;
        }
    }
}

static void IRAM_ATTR button0_isr_handler(void *arg)
{
    (void)arg;
    uint32_t gpio_num = BUTTON0_GPIO;
    BaseType_t higher_priority_task_woken = pdFALSE;
    (void)xQueueSendFromISR(s_button0_queue, &gpio_num, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
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
        if (last_press_us != 0 && now_us - last_press_us < BUTTON0_DEBOUNCE_US) {
            continue;
        }

        if (gpio_get_level((gpio_num_t)gpio_num) == 0) {
            last_press_us = now_us;
            ESP_LOGI(TAG, "Button 0 pressed");
            enqueue_button_stop();
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

static bool timer_frame_is_duplicate(const uint8_t *frame, uint8_t command)
{
    const uint8_t *source_mac = frame + 10;
    uint16_t sequence_control = frame[22] | ((uint16_t)frame[23] << 8);
    uint16_t sequence_number = sequence_control >> 4;
    int64_t now_us = esp_timer_get_time();

    for (size_t i = 0; i < TIMER_DUPLICATE_CACHE_SIZE; i++) {
        timer_packet_signature_t *signature = &s_duplicate_cache[i];
        if (!signature->valid ||
            signature->sequence_number != sequence_number ||
            signature->command != command ||
            memcmp(signature->source_mac, source_mac, sizeof(signature->source_mac)) != 0) {
            continue;
        }

        if (now_us - signature->received_at_us <= TIMER_DUPLICATE_WINDOW_US) {
            return true;
        }
    }

    return false;
}

static void remember_timer_frame(const uint8_t *frame, uint8_t command)
{
    const uint8_t *source_mac = frame + 10;
    uint16_t sequence_control = frame[22] | ((uint16_t)frame[23] << 8);
    uint16_t sequence_number = sequence_control >> 4;
    int64_t now_us = esp_timer_get_time();

    /* Refresh an expired matching entry instead of consuming another cache slot. */
    for (size_t i = 0; i < TIMER_DUPLICATE_CACHE_SIZE; i++) {
        timer_packet_signature_t *signature = &s_duplicate_cache[i];
        if (signature->valid &&
            signature->sequence_number == sequence_number &&
            signature->command == command &&
            memcmp(signature->source_mac, source_mac, sizeof(signature->source_mac)) == 0) {
            signature->received_at_us = now_us;
            return;
        }
    }

    timer_packet_signature_t *signature = &s_duplicate_cache[s_duplicate_cache_next];
    memcpy(signature->source_mac, source_mac, sizeof(signature->source_mac));
    signature->sequence_number = sequence_number;
    signature->command = command;
    signature->received_at_us = now_us;
    signature->valid = true;
    s_duplicate_cache_next = (s_duplicate_cache_next + 1) % TIMER_DUPLICATE_CACHE_SIZE;
}

static timer_action_t parse_timer_message(const uint8_t *frame, uint16_t len, uint64_t *elapsed_ns)
{
    if (len < TIMER_FRAME_HEADER_LEN) {
        return TIMER_ACTION_NONE;
    }

    uint16_t frame_ctrl = frame[0] | ((uint16_t)frame[1] << 8);
    uint8_t type = (frame_ctrl >> 2) & 0x03;
    uint8_t subtype = (frame_ctrl >> 4) & 0x0f;
    if (type != 0 || subtype != 13 ||
        memcmp(frame + 4, s_softap_mac, sizeof(s_softap_mac)) != 0 ||
        memcmp(frame + 16, s_softap_mac, sizeof(s_softap_mac)) != 0) {
        return TIMER_ACTION_NONE;
    }

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
        break;
    case TIMER_COMMAND_RESET:
    case TIMER_COMMAND_STOP:
        break;
    default:
        return TIMER_ACTION_NONE;
    }

    switch (body[1]) {
    case TIMER_COMMAND_START: {
        uint64_t value = 0;
        memcpy(&value, body + 2, sizeof(value));
        *elapsed_ns = value;
        return TIMER_ACTION_START;
    }
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
    if (buf == NULL || type != WIFI_PKT_MGMT) {
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    uint64_t elapsed_ns = 0;
    timer_action_t action = parse_timer_message(pkt->payload, pkt->rx_ctrl.sig_len, &elapsed_ns);
    if (action == TIMER_ACTION_NONE) {
        return;
    }

    uint8_t command = TIMER_COMMAND_STOP;
    if (action == TIMER_ACTION_START) {
        command = TIMER_COMMAND_START;
    } else if (action == TIMER_ACTION_RESET) {
        command = TIMER_COMMAND_RESET;
    }
    if (timer_frame_is_duplicate(pkt->payload, command)) {
        return;
    }

    if (enqueue_control_command(action, elapsed_ns)) {
        /* Only accepted commands suppress retransmissions with the same signature. */
        remember_timer_frame(pkt->payload, command);
    }
}

static esp_err_t init_buzzer_gpio(void)
{
    /*
     * Preload the output latch before enabling the output driver. This avoids
     * a high pulse while the GPIO changes from its reset state to an output.
     */
    ESP_RETURN_ON_ERROR(gpio_hold_dis(CONFIG_BUZZER_GPIO),
                        TAG,
                        "Failed to release buzzer GPIO hold during initialization");
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_BUZZER_GPIO, 0),
                        TAG,
                        "Failed to preload buzzer GPIO low");

    gpio_config_t config = {
        .pin_bit_mask = 1ULL << CONFIG_BUZZER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "Failed to configure buzzer GPIO");
    return buzzer_set_level(false);
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

    esp_err_t err = rmt_new_tx_channel(&tx_channel_config, &s_status_led_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create status LED RMT channel: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_new_bytes_encoder(&encoder_config, &s_status_led_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create status LED encoder: %s", esp_err_to_name(err));
        (void)rmt_del_channel(s_status_led_rmt_channel);
        s_status_led_rmt_channel = NULL;
        return err;
    }

    err = rmt_enable(s_status_led_rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable status LED RMT channel: %s", esp_err_to_name(err));
        (void)rmt_del_encoder(s_status_led_encoder);
        (void)rmt_del_channel(s_status_led_rmt_channel);
        s_status_led_encoder = NULL;
        s_status_led_rmt_channel = NULL;
        return err;
    }
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
    const esp_timer_create_args_t second_args = {
        .callback = second_buzz_timer_cb,
        .name = "buzz_second",
    };

    esp_err_t err = esp_timer_create(&delay_args, &s_delay_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create delay timer: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_timer_create(&stop_args, &s_buzz_stop_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create stop timer: %s", esp_err_to_name(err));
        (void)esp_timer_delete(s_delay_timer);
        s_delay_timer = NULL;
        return err;
    }

    err = esp_timer_create(&second_args, &s_second_buzz_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create second buzz timer: %s", esp_err_to_name(err));
        (void)esp_timer_delete(s_buzz_stop_timer);
        (void)esp_timer_delete(s_delay_timer);
        s_buzz_stop_timer = NULL;
        s_delay_timer = NULL;
        return err;
    }
    return ESP_OK;
}

static esp_err_t init_buzzer_control(void)
{
    s_control_queue = xQueueCreate(CONTROL_QUEUE_LEN, sizeof(control_event_t));
    s_control_start_slots = xSemaphoreCreateCounting(CONTROL_START_CAPACITY,
                                                     CONTROL_START_CAPACITY);
    s_control_urgent_slots = xSemaphoreCreateCounting(CONTROL_URGENT_CAPACITY,
                                                      CONTROL_URGENT_CAPACITY);
    if (s_control_queue == NULL ||
        s_control_start_slots == NULL ||
        s_control_urgent_slots == NULL) {
        if (s_control_urgent_slots != NULL) {
            vSemaphoreDelete(s_control_urgent_slots);
            s_control_urgent_slots = NULL;
        }
        if (s_control_start_slots != NULL) {
            vSemaphoreDelete(s_control_start_slots);
            s_control_start_slots = NULL;
        }
        if (s_control_queue != NULL) {
            vQueueDelete(s_control_queue);
            s_control_queue = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreate(buzzer_control_task,
                                    "buzzer_control",
                                    CONTROL_TASK_STACK_SIZE,
                                    NULL,
                                    CONTROL_TASK_PRIORITY,
                                    NULL);
    if (created != pdPASS) {
        vSemaphoreDelete(s_control_urgent_slots);
        vSemaphoreDelete(s_control_start_slots);
        vQueueDelete(s_control_queue);
        s_control_urgent_slots = NULL;
        s_control_start_slots = NULL;
        s_control_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
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

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure button 0 GPIO: %s", esp_err_to_name(err));
        vQueueDelete(s_button0_queue);
        s_button0_queue = NULL;
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        vQueueDelete(s_button0_queue);
        s_button0_queue = NULL;
        return err;
    }

    TaskHandle_t button_task_handle = NULL;
    BaseType_t created = xTaskCreate(button0_task,
                                    "button0",
                                    BUTTON0_TASK_STACK_SIZE,
                                    NULL,
                                    BUTTON0_TASK_PRIORITY,
                                    &button_task_handle);
    if (created != pdPASS) {
        vQueueDelete(s_button0_queue);
        s_button0_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    err = gpio_isr_handler_add(BUTTON0_GPIO, button0_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add button 0 ISR handler: %s", esp_err_to_name(err));
        vTaskDelete(button_task_handle);
        vQueueDelete(s_button0_queue);
        s_button0_queue = NULL;
        return err;
    }
    return ESP_OK;
}

static esp_err_t init_wifi_softap(void)
{
    wifi_config_t wifi_config = { 0 };
    const size_t ssid_len = strlen(CONFIG_BUZZER_SOFTAP_SSID);
    const size_t password_len = strlen(CONFIG_BUZZER_SOFTAP_PASSWORD);

    if (ssid_len == 0 || ssid_len > sizeof(wifi_config.ap.ssid)) {
        ESP_LOGE(TAG,
                 "SoftAP SSID must contain between 1 and %u bytes",
                 (unsigned int)sizeof(wifi_config.ap.ssid));
        return ESP_ERR_INVALID_ARG;
    }
    if (password_len > sizeof(wifi_config.ap.password) - 1 ||
        (password_len > 0 && password_len < 8)) {
        ESP_LOGE(TAG, "SoftAP password must be empty or contain between 8 and 63 bytes");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_AP();
    esp_netif_t *ap_netif = esp_netif_new(&netif_config);
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi AP netif");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_netif_attach_wifi_ap(ap_netif);
    if (err == ESP_OK) {
        err = esp_wifi_set_default_wifi_ap_handlers();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach default Wi-Fi AP netif: %s", esp_err_to_name(err));
        /* Also clears IDF's interface pointer if attachment failed part-way. */
        esp_netif_destroy_default_wifi(ap_netif);
        return err;
    }

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

    memcpy(wifi_config.ap.ssid, CONFIG_BUZZER_SOFTAP_SSID, ssid_len);
    memcpy(wifi_config.ap.password, CONFIG_BUZZER_SOFTAP_PASSWORD, password_len);
    wifi_config.ap.ssid_len = ssid_len;
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
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT,
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
    if (query_len < 12 || query_len > DNS_BUF_LEN ||
        (buf[2] & 0xf8) != 0 || /* only standard queries, never responses */
        (buf[4] == 0 && buf[5] == 0)) {
        return -1;
    }

    size_t cursor = 12;
    while (true) {
        if (cursor >= (size_t)query_len) {
            return -1;
        }

        uint8_t label_len = buf[cursor++];
        if (label_len == 0) {
            break;
        }
        if (label_len > 63 || label_len > (size_t)query_len - cursor) {
            return -1;
        }
        cursor += label_len;
    }

    if ((size_t)query_len - cursor < 4) {
        return -1;
    }
    size_t question_end = cursor + 4; /* QTYPE and QCLASS */
    if (question_end + 16 > DNS_BUF_LEN) {
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
    if (inet_pton(AF_INET, SOFTAP_IP_ADDR, answer + 12) != 1) {
        return -1;
    }

    return (int)(question_end + 16);
}

static void captive_dns_task(void *arg)
{
    int sock = (int)(intptr_t)arg;

    uint8_t buf[DNS_BUF_LEN];
    while (true) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&source_addr, &socklen);
        if (len < 0) {
            if (errno != EINTR) {
                ESP_LOGW(TAG, "DNS receive failed: errno %d", errno);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }
        if (len == 0) {
            continue;
        }

        int response_len = build_dns_response(buf, len);
        if (response_len > 0) {
            int sent = sendto(sock,
                              buf,
                              response_len,
                              0,
                              (struct sockaddr *)&source_addr,
                              socklen);
            if (sent < 0) {
                ESP_LOGD(TAG, "DNS response send failed: errno %d", errno);
            }
        }
    }
}

static esp_err_t start_captive_dns(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket: errno %d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in listen_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket: errno %d", errno);
        close(sock);
        return ESP_FAIL;
    }

    BaseType_t created = xTaskCreate(captive_dns_task,
                                    "captive_dns",
                                    DNS_TASK_STACK_SIZE,
                                    (void *)(intptr_t)sock,
                                    DNS_TASK_PRIORITY,
                                    NULL);
    if (created != pdPASS) {
        close(sock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t set_no_store_headers(httpd_req_t *req)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req,
                                           "Cache-Control",
                                           "no-store, no-cache, must-revalidate, max-age=0"),
                        TAG,
                        "Failed to set Cache-Control header");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Pragma", "no-cache"),
                        TAG,
                        "Failed to set Pragma header");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(req, "Expires", "0"),
                        TAG,
                        "Failed to set Expires header");
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char delay_buf[16];
    char duration_buf[16];
    char second_delay_buf[16];
    int delay_ms = (int)(get_runtime_delay_us() / 1000LL);
    int buzz_duration_ms = (int)(get_runtime_buzz_duration_us() / 1000LL);
    bool second_buzz_enabled = false;
    int64_t second_buzz_delay_us = 0;
    get_runtime_second_buzz_config(&second_buzz_enabled, &second_buzz_delay_us);
    int second_buzz_delay_ms = (int)(second_buzz_delay_us / 1000LL);
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

    written = snprintf(second_delay_buf,
                       sizeof(second_delay_buf),
                       "%d.%03d",
                       second_buzz_delay_ms / 1000,
                       second_buzz_delay_ms % 1000);
    if (written < 0 || written >= (int)sizeof(second_delay_buf)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Second delay value too large");
    }

    ESP_RETURN_ON_ERROR(set_no_store_headers(req), TAG, "Failed to set no-store headers");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "text/html"),
                        TAG,
                        "Failed to set HTML response type");

    char *html = malloc(HTTP_ROOT_BUF_LEN);
    if (html == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }

    written = snprintf(
        html,
        HTTP_ROOT_BUF_LEN,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"Cache-Control\" content=\"no-store\">"
        "<meta http-equiv=\"Pragma\" content=\"no-cache\">"
        "<meta http-equiv=\"Expires\" content=\"0\">"
        "<title>Buzzer Timer</title>"
        "<style>"
        "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#f5f7fb;color:#172033}"
        "main{max-width:520px;margin:0 auto;padding:32px 18px}"
        "section{background:#fff;border:1px solid #d9e0ea;border-radius:8px;padding:22px;box-shadow:0 8px 24px #1b2b4a14}"
        "h1{font-size:28px;margin:0 0 18px}label{display:block;font-weight:650;margin:16px 0 8px}"
        "input{box-sizing:border-box;width:100%%;font-size:18px;padding:10px;border:1px solid #aeb8c8;border-radius:6px}"
        "input[type=checkbox]{width:auto}.check{display:flex;align-items:center;gap:10px}"
        "button{display:block;margin-top:24px;font-size:17px;padding:10px 14px;border:0;border-radius:6px;background:#1267d8;color:white}"
        "p{line-height:1.45}.status{min-height:24px;color:#26613f}.meta{font-size:12px;color:#647083;margin-top:18px}"
        "</style></head><body><main><section>"
        "<h1>Buzzer Timer</h1><p>SoftAP channel %d. Current buzz delay is %s s. Buzz time is %s ms.</p>"
        "<form id=\"config-form\">"
        "<label for=\"delay\">Buzz delay in seconds</label>"
        "<input id=\"delay\" name=\"delay\" type=\"number\" min=\"0\" max=\"3600\" step=\"0.001\" value=\"%s\" required>"
        "<label for=\"duration\">Buzz time in milliseconds</label>"
        "<input id=\"duration\" name=\"duration\" type=\"number\" min=\"1\" max=\"60000\" step=\"1\" value=\"%s\" required>"
        "<label class=\"check\"><input id=\"second-enabled\" name=\"second-enabled\" type=\"checkbox\"%s>Second buzz</label>"
        "<label for=\"second-delay\">Second buzz wait after first buzz in seconds</label>"
        "<input id=\"second-delay\" name=\"second-delay\" type=\"number\" min=\"0.001\" max=\"3600\" step=\"0.001\" value=\"%s\" required>"
        "<button type=\"submit\">Save settings</button>"
        "</form><p class=\"status\" id=\"status\"></p><p class=\"meta\">Firmware %s</p></section></main><script>"
        "const form=document.getElementById('config-form');"
        "const statusEl=document.getElementById('status');"
        "form.addEventListener('submit',async(e)=>{"
        "e.preventDefault();statusEl.textContent='Saving...';"
        "const delay_seconds=Number(document.getElementById('delay').value);"
        "const buzz_duration_ms=Number(document.getElementById('duration').value);"
        "const second_buzz_enabled=document.getElementById('second-enabled').checked;"
        "const second_buzz_delay_seconds=Number(document.getElementById('second-delay').value);"
        "const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({delay_seconds,buzz_duration_ms,second_buzz_enabled,second_buzz_delay_seconds})});"
        "if(!r.ok){statusEl.textContent='Save failed';return;}"
        "const cfg=await r.json();document.getElementById('delay').value=cfg.delay_seconds;"
        "document.getElementById('duration').value=cfg.buzz_duration_ms;"
        "document.getElementById('second-enabled').checked=cfg.second_buzz_enabled;"
        "document.getElementById('second-delay').value=cfg.second_buzz_delay_seconds;"
        "statusEl.textContent='Saved for future triggers';"
        "});"
        "</script></body></html>",
        CONFIG_BUZZER_SOFTAP_CHANNEL,
        delay_buf,
        duration_buf,
        delay_buf,
        duration_buf,
        second_buzz_enabled ? " checked" : "",
        second_delay_buf,
        esp_app_get_description()->version);
    if (written < 0 || written >= HTTP_ROOT_BUF_LEN) {
        free(html);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTML too large");
    }

    esp_err_t err = httpd_resp_send(req, html, written);
    free(html);
    return err;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    char json[256];
    int delay_ms = (int)(get_runtime_delay_us() / 1000LL);
    int buzz_duration_ms = (int)(get_runtime_buzz_duration_us() / 1000LL);
    bool second_buzz_enabled = false;
    int64_t second_buzz_delay_us = 0;
    get_runtime_second_buzz_config(&second_buzz_enabled, &second_buzz_delay_us);
    int second_buzz_delay_ms = (int)(second_buzz_delay_us / 1000LL);
    int written = snprintf(json,
                           sizeof(json),
                           "{\"delay_seconds\":\"%d.%03d\","
                           "\"delay_ms\":%d,"
                           "\"buzz_duration_ms\":%d,"
                           "\"second_buzz_enabled\":%s,"
                           "\"second_buzz_delay_seconds\":\"%d.%03d\","
                           "\"second_buzz_delay_ms\":%d}\n",
                           delay_ms / 1000,
                           delay_ms % 1000,
                           delay_ms,
                           buzz_duration_ms,
                           second_buzz_enabled ? "true" : "false",
                           second_buzz_delay_ms / 1000,
                           second_buzz_delay_ms % 1000,
                           second_buzz_delay_ms);
    if (written < 0 || written >= (int)sizeof(json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON too large");
    }

    ESP_RETURN_ON_ERROR(set_no_store_headers(req), TAG, "Failed to set no-store headers");
    ESP_RETURN_ON_ERROR(httpd_resp_set_type(req, "application/json"),
                        TAG,
                        "Failed to set JSON response type");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static const char *find_json_value(const char *body, const char *key)
{
    const char *value = body;
    size_t key_len = strlen(key);

    while ((value = strstr(value, key)) != NULL) {
        const char *key_end = value + key_len;
        if (value > body && value[-1] == '"' && *key_end == '"') {
            const char *separator = key_end + 1;
            while (isspace((unsigned char)*separator)) {
                separator++;
            }
            if (*separator == ':') {
                value = separator + 1;
                while (isspace((unsigned char)*value)) {
                    value++;
                }
                if (*value == '"') {
                    value++;
                }
                return *value == '\0' ? NULL : value;
            }
        }
        value = key_end;
    }
    return NULL;
}

static bool json_token_has_valid_end(const char *end)
{
    if (*end == '"') {
        end++;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    return *end == ',' || *end == '}' || *end == '\0';
}

static const char *find_json_number_value(const char *body, const char *key)
{
    const char *value = find_json_value(body, key);
    return value != NULL && isdigit((unsigned char)*value) ? value : NULL;
}

static bool parse_uint_field(const char *body, const char *key, uint32_t max_value, uint32_t *value_out)
{
    const char *value = find_json_number_value(body, key);
    if (value == NULL) {
        return false;
    }

    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || errno == ERANGE || parsed > max_value ||
        !json_token_has_valid_end(end)) {
        return false;
    }

    *value_out = (uint32_t)parsed;
    return true;
}

static bool parse_bool_field(const char *body, const char *key, bool *value_out)
{
    const char *value = find_json_value(body, key);
    if (value == NULL) {
        return false;
    }

    if (strncmp(value, "true", 4) == 0 && json_token_has_valid_end(value + 4)) {
        *value_out = true;
        return true;
    }
    if (strncmp(value, "false", 5) == 0 && json_token_has_valid_end(value + 5)) {
        *value_out = false;
        return true;
    }
    if ((*value == '1' || *value == '0') && json_token_has_valid_end(value + 1)) {
        *value_out = *value == '1';
        return true;
    }

    return false;
}

static bool parse_seconds_field(const char *body,
                                const char *seconds_key,
                                const char *ms_key,
                                uint32_t max_ms,
                                uint32_t *delay_ms)
{
    const char *value = find_json_number_value(body, seconds_key);
    if (value == NULL) {
        return parse_uint_field(body, ms_key, max_ms, delay_ms);
    }

    char *end = NULL;
    errno = 0;
    unsigned long seconds = strtoul(value, &end, 10);
    if (end == value || errno == ERANGE || seconds > max_ms / 1000UL) {
        return false;
    }

    uint32_t fraction_ms = 0;
    if (*end == '.') {
        end++;
        unsigned int digits = 0;
        uint32_t scale = 100;
        while (isdigit((unsigned char)*end)) {
            if (digits < 3) {
                fraction_ms += (uint32_t)(*end - '0') * scale;
                scale /= 10;
            }
            digits++;
            end++;
        }
        if (digits == 0 || digits > 3) {
            return false;
        }
    }
    if (!json_token_has_valid_end(end)) {
        return false;
    }

    uint64_t parsed_ms = (uint64_t)seconds * 1000ULL + fraction_ms;
    if (parsed_ms > max_ms) {
        return false;
    }

    *delay_ms = (uint32_t)parsed_ms;
    return true;
}

static bool parse_delay_seconds_field(const char *body, uint32_t *delay_ms)
{
    return parse_seconds_field(body, "delay_seconds", "delay_ms", 3600000, delay_ms);
}

static bool parse_second_buzz_delay_seconds_field(const char *body, uint32_t *delay_ms)
{
    return parse_seconds_field(body,
                               "second_buzz_delay_seconds",
                               "second_buzz_delay_ms",
                               3600000,
                               delay_ms);
}

static bool json_has_key(const char *body, const char *key)
{
    return find_json_value(body, key) != NULL;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len >= HTTP_POST_BUF_LEN) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    }

    char body[HTTP_POST_BUF_LEN] = { 0 };
    size_t received_total = 0;
    unsigned int timeout_retries = 0;
    while (received_total < req->content_len) {
        int received = httpd_req_recv(req,
                                      body + received_total,
                                      req->content_len - received_total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT &&
            timeout_retries < HTTP_RECV_TIMEOUT_RETRIES) {
            timeout_retries++;
            continue;
        }
        if (received <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
        }
        timeout_retries = 0;
        received_total += (size_t)received;
    }
    body[received_total] = '\0';

    uint32_t delay_ms = 0;
    uint32_t buzz_duration_ms = 0;
    bool second_buzz_enabled = false;
    int64_t current_second_buzz_delay_us = 0;
    get_runtime_second_buzz_config(&second_buzz_enabled, &current_second_buzz_delay_us);
    uint32_t second_buzz_delay_ms = (uint32_t)(current_second_buzz_delay_us / 1000LL);

    if (!parse_delay_seconds_field(body, &delay_ms)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected delay_seconds between 0 and 3600");
    }
    if (!parse_uint_field(body, "buzz_duration_ms", 60000, &buzz_duration_ms) || buzz_duration_ms == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected buzz_duration_ms between 1 and 60000");
    }
    if (json_has_key(body, "second_buzz_enabled") &&
        !parse_bool_field(body, "second_buzz_enabled", &second_buzz_enabled)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected second_buzz_enabled as true or false");
    }
    if ((json_has_key(body, "second_buzz_delay_seconds") || json_has_key(body, "second_buzz_delay_ms")) &&
        (!parse_second_buzz_delay_seconds_field(body, &second_buzz_delay_ms) || second_buzz_delay_ms == 0)) {
        return httpd_resp_send_err(req,
                                   HTTPD_400_BAD_REQUEST,
                                   "Expected second_buzz_delay_seconds between 0.001 and 3600");
    }

    set_runtime_delay_ms(delay_ms);
    set_runtime_buzz_duration_ms(buzz_duration_ms);
    set_runtime_second_buzz_config(second_buzz_enabled, second_buzz_delay_ms);
    ESP_LOGI(TAG,
             "Runtime delay updated to %" PRIu32 " ms, buzz duration to %" PRIu32
             " ms, second buzz %s after %" PRIu32 " ms",
             delay_ms,
             buzz_duration_ms,
             second_buzz_enabled ? "enabled" : "disabled",
             second_buzz_delay_ms);
    return config_get_handler(req);
}

static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    config.stack_size = 6144;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

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

    err = httpd_register_uri_handler(server, &config_get_uri);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(server, &config_post_uri);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(server, &root_uri);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register HTTP handler: %s", esp_err_to_name(err));
        esp_err_t stop_err = httpd_stop(server);
        if (stop_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop incomplete HTTP server: %s", esp_err_to_name(stop_err));
        }
        return err;
    }

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

void app_main(void)
{
    /*
     * The buzzer is safety-critical: force and hold it low before any
     * initialization that can block, fail, or restart the device.
     */
    ESP_ERROR_CHECK(init_buzzer_gpio());
    ESP_ERROR_CHECK(init_status_led());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(init_timers());
    ESP_ERROR_CHECK(init_buzzer_control());
    ESP_ERROR_CHECK(status_led_set(STATUS_LED_READY));
    ESP_ERROR_CHECK(init_button0_gpio());
    ESP_ERROR_CHECK(init_wifi_softap());
    ESP_ERROR_CHECK(start_captive_dns());
    ESP_ERROR_CHECK(start_webserver());

    ESP_LOGI(TAG,
             "Ready. GPIO %d, status LED GPIO %d, default delay %d ms, buzz duration %d ms, "
             "second buzz %s after %d ms",
             CONFIG_BUZZER_GPIO,
             CONFIG_STATUS_LED_GPIO,
             CONFIG_BUZZER_DELAY_MS,
             CONFIG_BUZZER_DURATION_MS,
             BUZZER_SECOND_ENABLED_DEFAULT ? "enabled" : "disabled",
             CONFIG_BUZZER_SECOND_DELAY_MS);
}
