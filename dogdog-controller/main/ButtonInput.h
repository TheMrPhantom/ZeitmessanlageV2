#ifndef __BUTTON_INPUT_H
#define __BUTTON_INPUT_H
void Button_Input_Task(void *params);
void init_Pins();

typedef struct sensor_interrupt_t
{
    int pinNumber;
    int edge;
} sensor_interrupt_t;

#define BUTTON_TYPE_FAULT 16
#define BUTTON_TYPE_REFUSAL 8
#define BUTTON_TYPE_DIS 18
#define BUTTON_TYPE_RESET 17
#define BUTTON_TYPE_ACTIVATE 15

#endif // __MAIN_H