#ifndef __BUTTON_INPUT_H
#define __BUTTON_INPUT_H

#include "esp_err.h"

void Button_Input_Task(void *params);
esp_err_t start_ota_gesture_monitor(void);
esp_err_t init_button_pins(void);

typedef struct sensor_interrupt_t
{
    int pinNumber;
    int edge;
} sensor_interrupt_t;

#endif // __MAIN_H
