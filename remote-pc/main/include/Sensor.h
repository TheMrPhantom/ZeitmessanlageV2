#ifndef __SENSOR_H
#define __SENSOR_H
void Sensor_Interrupt_Task(void *params);
void init_Pins();

#define BUTTON_TYPE_FAULT 16
#define BUTTON_TYPE_REFUSAL 17
#define BUTTON_TYPE_DIS 18
#define BUTTON_TYPE_RESET 8
#define BUTTON_TYPE_ACTIVATE 15

#endif // __MAIN_H