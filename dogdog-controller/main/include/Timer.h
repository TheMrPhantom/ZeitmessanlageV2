#ifndef __TIMER_H
#define __TIMER_H
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

void Timer_Task(void *params);
void startTimer(int64_t timestamp);
void stopTimer();
void sendTimeToNetwork(int *timeElapsed);
extern int64_t lastTriggerTime;
#define TIMER_RESET 0
#define TIMER_RESTART_LAST_TRIGGER 1

typedef struct TimerTrigger
{
    bool is_start;
    int64_t timestamp;
    bool is_final_time;
} TimerTrigger;

#endif
