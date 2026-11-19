#ifndef TEST_TINYUSB_H
#define TEST_TINYUSB_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define TUD_OPT_HIGH_SPEED 0
#define TUD_CONFIG_DESC_LEN 9
#define CFG_TUD_HID 1
#define TUD_HID_DESC_LEN 25
#define TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP 0x20
#define TUD_CONFIG_DESCRIPTOR(...) 0
#define TUD_HID_DESCRIPTOR(...) 0
#define TUD_HID_REPORT_DESC_KEYBOARD(...) 0
#define TUD_HID_REPORT_DESC_MOUSE(...) 0
#define HID_REPORT_ID(...) 0
typedef struct {
    const void *device_descriptor;
    const char **string_descriptor;
    unsigned string_descriptor_count;
    bool external_phy;
    const uint8_t *configuration_descriptor;
} tinyusb_config_t;
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
bool tud_mounted(void);
bool tud_suspended(void);
bool tud_hid_ready(void);
void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_suspend_cb(bool remote_wakeup_en);
void tud_resume_cb(void);
#endif
