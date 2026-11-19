#ifndef TEST_HID_DEVICE_H
#define TEST_HID_DEVICE_H
#include <stdbool.h>
#include <stdint.h>
#define HID_ITF_PROTOCOL_KEYBOARD 1
#define HID_ITF_PROTOCOL_MOUSE 2
#define HID_KEY_D 0x07
#define HID_KEY_F 0x09
#define HID_KEY_1 0x1e
#define HID_KEY_2 0x1f
#define HID_KEY_3 0x20
#define HID_KEY_4 0x21
#define HID_KEY_5 0x22
#define HID_KEY_6 0x23
#define HID_KEY_7 0x24
#define HID_KEY_8 0x25
#define HID_KEY_9 0x26
#define HID_KEY_0 0x27
#define HID_KEY_ENTER 0x28
#define HID_KEY_ESCAPE 0x29
#define HID_KEY_COMMA 0x36
typedef enum { HID_REPORT_TYPE_INVALID, HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT, HID_REPORT_TYPE_FEATURE } hid_report_type_t;
typedef struct { uint8_t modifier, reserved, keycode[6]; } hid_keyboard_report_t;
bool tud_hid_keyboard_report(uint8_t report_id, uint8_t modifier, const uint8_t keycode[6]);
void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report, uint16_t len);
void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type, const uint8_t *report, uint16_t xferred_bytes);
#endif
