#ifndef __TIMER_H
#define __TIMER_H

#define TRIGGER_START 0
#define TRIGGER_STOP 1

void Timer_Task(void *params);
void stopTimer();
void sendTimeToNetwork(int *timeElapsed);

#endif