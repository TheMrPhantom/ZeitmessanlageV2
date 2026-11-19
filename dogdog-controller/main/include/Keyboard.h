#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#include "freertos/FreeRTOS.h"
#include "class/hid/hid_device.h"

void init_keyboard();
// Success means the host completed both the key-down and key-release transfers.
BaseType_t sendKey(uint8_t keycode);
BaseType_t sendText(const char *text);
BaseType_t sendTextWithKeys(uint8_t prefix, const char *text, uint8_t suffix);
uint8_t charToKeycode(char c);

// Internal diagnostics only; USB confirmation does not confirm application input.
typedef enum
{
    KEYBOARD_DELIVERY_IDLE,
    KEYBOARD_DELIVERY_PENDING,
    KEYBOARD_DELIVERY_USB_CONFIRMED,
    KEYBOARD_DELIVERY_UNCONFIRMED,
} keyboard_delivery_status_t;

keyboard_delivery_status_t keyboard_get_delivery_status(void);

#endif // __MAIN_H
