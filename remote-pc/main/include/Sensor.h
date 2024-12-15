#ifndef __SENSOR_H
#define __SENSOR_H
void Sensor_Interrupt_Task(void *params);
void init_Pins();

#define BUTTON_TYPE_FAULT 3
#define BUTTON_TYPE_REFUSAL 1
#define BUTTON_TYPE_DIS 2
#define BUTTON_TYPE_RESET 0
#define BUTTON_TYPE_ACTIVATE 4

#endif // __MAIN_H