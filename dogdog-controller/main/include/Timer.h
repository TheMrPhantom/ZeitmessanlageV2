#ifndef __TIMER_H
#define __TIMER_H
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

void Timer_Task(void *params);
void startTimer(int64_t timestamp);
void stopTimer();
void sendTimeToNetwork(int *timeElapsed);
bool restartTimerFromLastTrigger(void);
void getTimerState(bool *is_running, int64_t *start_timestamp);

typedef struct TimerTrigger
{
    bool is_start;
    int64_t timestamp;
    bool is_final_time;
    bool force_restart;
} TimerTrigger;

#endif
