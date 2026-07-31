#ifndef DOGDOG_STARTUP_H
#define DOGDOG_STARTUP_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define DOGDOG_STARTUP_DISPLAY_READY_BIT BIT0
#define DOGDOG_STARTUP_BUTTON_READY_BIT BIT1
#define DOGDOG_STARTUP_PRIMARY_IO_READY_BIT BIT2
#define DOGDOG_STARTUP_FAILED_BIT BIT3

#define DOGDOG_OTA_GESTURE_LATCHED_BIT BIT4
#define DOGDOG_OTA_DISPLAY_REQUEST_BIT BIT5
#define DOGDOG_OTA_DISPLAY_ACK_BIT BIT6

#define DOGDOG_STARTUP_REQUIRED_BITS                                           \
    (DOGDOG_STARTUP_DISPLAY_READY_BIT | DOGDOG_STARTUP_BUTTON_READY_BIT |      \
     DOGDOG_STARTUP_PRIMARY_IO_READY_BIT)

extern EventGroupHandle_t startupEventGroup;

void dogdog_startup_signal_ready(EventBits_t ready_bit);
void dogdog_startup_signal_failure(void);

#endif
