#ifndef __BUTTON_INPUT_H
#define __BUTTON_INPUT_H

#include "freertos/FreeRTOS.h"

void Button_Input_Task(void *params);
void init_Pins();

typedef struct sensor_interrupt_t
{
    int pinNumber;
    int edge;
    TickType_t captured_at;
} sensor_interrupt_t;

#endif // __MAIN_H
