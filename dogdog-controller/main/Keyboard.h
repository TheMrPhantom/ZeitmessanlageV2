#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#include "freertos/FreeRTOS.h"
#include "class/hid/hid_device.h"

void init_keyboard();
BaseType_t sendKey(uint8_t keycode);
void sendText(char *text);
uint8_t charToKeycode(char c);

#endif // __MAIN_H