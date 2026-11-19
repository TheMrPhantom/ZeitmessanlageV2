/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "Keyboard.h"

static const char *TAG = "Keyboard";

#define KEYBOARD_REPORT_TIMEOUT pdMS_TO_TICKS(100)
#define KEYBOARD_LOCK_TIMEOUT pdMS_TO_TICKS(1000)
#define KEYBOARD_REPORT_ATTEMPTS 3
#define KEYBOARD_REPORT_SIZE (1 + sizeof(hid_keyboard_report_t))

typedef enum
{
    REPORT_IDLE,
    REPORT_WAITING,
    REPORT_COMPLETE,
    REPORT_FAILED,
} report_state_t;

static SemaphoreHandle_t keyboard_mutex;
static portMUX_TYPE report_mux = portMUX_INITIALIZER_UNLOCKED;
static report_state_t report_state;
static uint8_t expected_report[KEYBOARD_REPORT_SIZE];
static uint32_t connection_epoch;
static bool release_needed;
static keyboard_delivery_status_t delivery_status = KEYBOARD_DELIVERY_IDLE;

keyboard_delivery_status_t keyboard_get_delivery_status(void)
{
    portENTER_CRITICAL(&report_mux);
    keyboard_delivery_status_t result = delivery_status;
    portEXIT_CRITICAL(&report_mux);
    return result;
}

static void set_delivery_status(keyboard_delivery_status_t status)
{
    portENTER_CRITICAL(&report_mux);
    delivery_status = status;
    portEXIT_CRITICAL(&report_mux);
}

static uint32_t current_connection_epoch(void)
{
    portENTER_CRITICAL(&report_mux);
    uint32_t epoch = connection_epoch;
    portEXIT_CRITICAL(&report_mux);
    return epoch;
}

void tud_mount_cb(void)
{
    portENTER_CRITICAL(&report_mux);
    ++connection_epoch;
    report_state = REPORT_IDLE;
    // Establish an all-keys-up state before accepting a new press after a reset.
    release_needed = true;
    portEXIT_CRITICAL(&report_mux);
}

void tud_umount_cb(void)
{
    portENTER_CRITICAL(&report_mux);
    ++connection_epoch;
    report_state = REPORT_IDLE;
    release_needed = true;
    delivery_status = KEYBOARD_DELIVERY_UNCONFIRMED;
    portEXIT_CRITICAL(&report_mux);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    if (instance != 0 || report == NULL || len != KEYBOARD_REPORT_SIZE)
    {
        return;
    }
    portENTER_CRITICAL(&report_mux);
    if (report_state == REPORT_WAITING && memcmp(report, expected_report, len) == 0)
    {
        report_state = REPORT_COMPLETE;
    }
    portEXIT_CRITICAL(&report_mux);
}

void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type,
                              uint8_t const *report, uint16_t xferred_bytes)
{
    (void)xferred_bytes;
    if (instance != 0 || report_type != HID_REPORT_TYPE_INPUT || report == NULL)
    {
        return;
    }
    portENTER_CRITICAL(&report_mux);
    // TinyUSB supplies its full endpoint buffer even when zero bytes transferred.
    if (report_state == REPORT_WAITING &&
        memcmp(report, expected_report, sizeof(expected_report)) == 0)
    {
        report_state = REPORT_FAILED;
    }
    portEXIT_CRITICAL(&report_mux);
}

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))};

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},    // 0: is supported language is English (0x0409)
    "TinyUSB",               // 1: Manufacturer
    "TinyUSB Device",        // 2: Product
    "123456",                // 3: Serials, should use chip ID
    "Example HID interface", // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
}

/********* Application ***************/

// The mutex covers the whole operation, including every retry and release.
static bool send_keyboard_report(uint8_t keycode, uint32_t epoch)
{
    uint8_t report[KEYBOARD_REPORT_SIZE] = {HID_ITF_PROTOCOL_KEYBOARD};
    report[3] = keycode; // Report ID, modifier, reserved, then six keycodes.
    uint8_t keycodes[6] = {keycode};

    for (int attempt = 0; attempt < KEYBOARD_REPORT_ATTEMPTS; ++attempt)
    {
        TickType_t started = xTaskGetTickCount();
        bool ready = false;
        while ((TickType_t)(xTaskGetTickCount() - started) < KEYBOARD_REPORT_TIMEOUT)
        {
            if (current_connection_epoch() != epoch || !tud_mounted() || tud_suspended())
            {
                return false;
            }

            portENTER_CRITICAL(&report_mux);
            bool completed = report_state == REPORT_COMPLETE &&
                             memcmp(report, expected_report, sizeof(report)) == 0;
            bool outstanding = report_state == REPORT_WAITING;
            portEXIT_CRITICAL(&report_mux);
            // A late completion of the same desired state also confirms delivery.
            if (completed)
            {
                return true;
            }
            // TinyUSB clears endpoint busy before invoking the callback. Its
            // callback points at the same mutable endpoint buffer: readiness
            // alone must never let us overwrite an unacknowledged report.
            if (!outstanding && tud_hid_ready())
            {
                ready = true;
                break;
            }
            vTaskDelay(1);
        }
        if (!ready)
        {
            continue;
        }

        portENTER_CRITICAL(&report_mux);
        bool connected = connection_epoch == epoch;
        if (connected)
        {
            memcpy(expected_report, report, sizeof(report));
            report_state = REPORT_WAITING;
        }
        portEXIT_CRITICAL(&report_mux);
        if (!connected)
        {
            return false;
        }

        // Arm the callback first: completion may run before this call returns.
        if (!tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0,
                                     keycode == 0 ? NULL : keycodes))
        {
            portENTER_CRITICAL(&report_mux);
            if (connection_epoch == epoch)
            {
                report_state = REPORT_IDLE;
            }
            portEXIT_CRITICAL(&report_mux);
            vTaskDelay(1);
            continue;
        }

        started = xTaskGetTickCount();
        while ((TickType_t)(xTaskGetTickCount() - started) < KEYBOARD_REPORT_TIMEOUT)
        {
            portENTER_CRITICAL(&report_mux);
            report_state_t state = report_state;
            connected = connection_epoch == epoch;
            portEXIT_CRITICAL(&report_mux);
            if (!connected || !tud_mounted() || tud_suspended())
            {
                return false;
            }
            if (state == REPORT_COMPLETE)
            {
                return true;
            }
            if (state == REPORT_FAILED)
            {
                break;
            }
            vTaskDelay(1);
        }
        // Retry the same HID state, never an entire down/up stroke. Repeating a
        // down report without an intervening release creates no second press.
    }
    return false;
}

static bool recover_keyboard_release(uint32_t epoch)
{
    portENTER_CRITICAL(&report_mux);
    bool needed = release_needed;
    portEXIT_CRITICAL(&report_mux);
    if (!needed)
    {
        return true;
    }
    if (!send_keyboard_report(0, epoch))
    {
        return false;
    }
    portENTER_CRITICAL(&report_mux);
    bool connected = connection_epoch == epoch;
    if (connected)
    {
        release_needed = false;
        report_state = REPORT_IDLE;
    }
    portEXIT_CRITICAL(&report_mux);
    return connected;
}

static bool send_keyboard_stroke(uint8_t keycode, uint32_t epoch)
{
    if (!recover_keyboard_release(epoch))
    {
        return false;
    }
    portENTER_CRITICAL(&report_mux);
    release_needed = true;
    portEXIT_CRITICAL(&report_mux);

    bool pressed = send_keyboard_report(keycode, epoch);
    // Even an unconfirmed down report may have reached the host. Always release.
    bool released = recover_keyboard_release(epoch);
    return pressed && released;
}

static void keyboard_release_task(void *params)
{
    (void)params;
    while (true)
    {
        if (tud_mounted() && !tud_suspended() &&
            xSemaphoreTake(keyboard_mutex, 0) == pdTRUE)
        {
            recover_keyboard_release(current_connection_epoch());
            xSemaphoreGive(keyboard_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void init_keyboard(void)
{
    keyboard_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(keyboard_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = hid_configuration_descriptor, // HID configuration descriptor for full-speed and high-speed are the same
        .hs_configuration_descriptor = hid_configuration_descriptor,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = hid_configuration_descriptor,
#endif // TUD_OPT_HIGH_SPEED
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_ERROR_CHECK(xTaskCreate(keyboard_release_task, "Keyboard_Release", 3072,
                               NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "USB initialization DONE");
}

BaseType_t sendKey(uint8_t keycode)
{
    if (keyboard_mutex == NULL ||
        xSemaphoreTake(keyboard_mutex, KEYBOARD_LOCK_TIMEOUT) != pdTRUE)
    {
        set_delivery_status(KEYBOARD_DELIVERY_UNCONFIRMED);
        ESP_LOGE(TAG, "USB delivery unconfirmed: keyboard busy or uninitialized");
        return pdFALSE;
    }
    set_delivery_status(KEYBOARD_DELIVERY_PENDING);
    uint32_t epoch = current_connection_epoch();
    bool confirmed = send_keyboard_stroke(keycode, epoch);
    portENTER_CRITICAL(&report_mux);
    confirmed = confirmed && connection_epoch == epoch;
    delivery_status = confirmed ? KEYBOARD_DELIVERY_USB_CONFIRMED : KEYBOARD_DELIVERY_UNCONFIRMED;
    portEXIT_CRITICAL(&report_mux);
    xSemaphoreGive(keyboard_mutex);
    if (!confirmed)
    {
        ESP_LOGE(TAG, "USB delivery unconfirmed for keycode %u after bounded retries", keycode);
    }
    return confirmed ? pdTRUE : pdFALSE;
}

static BaseType_t send_text(uint8_t prefix, const char *text, uint8_t suffix)
{
    if (text == NULL || keyboard_mutex == NULL ||
        xSemaphoreTake(keyboard_mutex, KEYBOARD_LOCK_TIMEOUT) != pdTRUE)
    {
        set_delivery_status(KEYBOARD_DELIVERY_UNCONFIRMED);
        ESP_LOGE(TAG, "USB text delivery unconfirmed: invalid text or keyboard busy");
        return pdFALSE;
    }
    set_delivery_status(KEYBOARD_DELIVERY_PENDING);
    uint32_t epoch = current_connection_epoch();
    bool confirmed = prefix == 0 || send_keyboard_stroke(prefix, epoch);
    for (const char *c = text; confirmed && *c != '\0'; ++c)
    {
        confirmed = send_keyboard_stroke(charToKeycode(*c), epoch);
    }
    if (confirmed && suffix != 0)
    {
        confirmed = send_keyboard_stroke(suffix, epoch);
    }
    portENTER_CRITICAL(&report_mux);
    confirmed = confirmed && connection_epoch == epoch;
    delivery_status = confirmed ? KEYBOARD_DELIVERY_USB_CONFIRMED : KEYBOARD_DELIVERY_UNCONFIRMED;
    portEXIT_CRITICAL(&report_mux);
    xSemaphoreGive(keyboard_mutex);
    if (!confirmed)
    {
        ESP_LOGE(TAG, "USB text delivery unconfirmed; remaining keys cancelled");
    }
    return confirmed ? pdTRUE : pdFALSE;
}

BaseType_t sendText(const char *text)
{
    return send_text(0, text, 0);
}

BaseType_t sendTextWithKeys(uint8_t prefix, const char *text, uint8_t suffix)
{
    return send_text(prefix, text, suffix);
}

/*
Supported chars 0-9 and ,
*/
uint8_t charToKeycode(char c)
{
    // Convert a char representing an ascii character to a keycode
    if (c == '0')
    {
        return HID_KEY_0;
    }
    else if (c == '1')
    {
        return HID_KEY_1;
    }
    else if (c == '2')
    {
        return HID_KEY_2;
    }
    else if (c == '3')
    {
        return HID_KEY_3;
    }
    else if (c == '4')
    {
        return HID_KEY_4;
    }
    else if (c == '5')
    {
        return HID_KEY_5;
    }
    else if (c == '6')
    {
        return HID_KEY_6;
    }
    else if (c == '7')
    {
        return HID_KEY_7;
    }
    else if (c == '8')
    {
        return HID_KEY_8;
    }
    else if (c == '9')
    {
        return HID_KEY_9;
    }
    else if (c == ',' || c == '.')
    {
        return HID_KEY_COMMA;
    }
    return HID_KEY_ESCAPE;
}
