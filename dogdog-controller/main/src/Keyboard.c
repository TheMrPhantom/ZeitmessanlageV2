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
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "Keyboard.h"
#include "freertos/semphr.h"

#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "Keyboard";
static SemaphoreHandle_t keyboard_mutex;

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

static bool wait_until_hid_ready(TickType_t timeout)
{
    TickType_t started = xTaskGetTickCount();
    do
    {
        if (tud_mounted() && tud_hid_ready())
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while ((xTaskGetTickCount() - started) < timeout);
    return false;
}

static BaseType_t send_keyboard_stroke(uint8_t keycode)
{
    if (!wait_until_hid_ready(pdMS_TO_TICKS(100)))
    {
        return pdFALSE;
    }

    uint8_t keycodes[6] = {keycode};
    if (!tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycodes))
    {
        return pdFALSE;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
    if (!wait_until_hid_ready(pdMS_TO_TICKS(100)) ||
        !tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL))
    {
        ESP_LOGE(TAG, "Failed to send key-release report");
        return pdFALSE;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
    ESP_LOGI(TAG, "Keycode sent: %i", keycode);
    return pdTRUE;
}

void init_keyboard(void)
{

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
    keyboard_mutex = xSemaphoreCreateMutex();
    if (!keyboard_mutex)
    {
        ESP_LOGE(TAG, "Failed to create keyboard mutex");
    }
    ESP_LOGI(TAG, "USB initialization DONE");
}

BaseType_t sendKey(uint8_t keycode)
{
    if (!keyboard_mutex || xSemaphoreTake(keyboard_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Keyboard is unavailable/busy");
        return pdFALSE;
    }

    BaseType_t result = pdFALSE;
    if (tud_mounted())
    {
        result = send_keyboard_stroke(keycode);
    }
    else
    {
        ESP_LOGE(TAG, "USB not mounted, cannot send key");
    }
    xSemaphoreGive(keyboard_mutex);
    return result;
}

void sendText(char *text)
{
    if (!text)
    {
        ESP_LOGE(TAG, "Cannot send null text");
        return;
    }
    if (!keyboard_mutex || xSemaphoreTake(keyboard_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Keyboard is unavailable/busy");
        return;
    }

    // go through each character in the string and send it
    size_t length = strlen(text);
    for (size_t i = 0; i < length; i++)
    {
        if (send_keyboard_stroke(charToKeycode(text[i])) != pdTRUE)
        {
            ESP_LOGW(TAG, "Stopped text report at character %u", (unsigned int)i);
            break;
        }
    }
    xSemaphoreGive(keyboard_mutex);
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
