#ifndef __TIMER_H
#define __TIMER_H
#include "freertos/FreeRTOS.h"

void Timer_Task(void *params);
void stopTimer();
void sendTimeToNetwork(int *timeElapsed);

typedef struct TimerTrigger
{
    bool is_start;
    int64_t timestamp;
} TimerTrigger;

#endif