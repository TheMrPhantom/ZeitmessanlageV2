#ifndef __TIMER_H
#define __TIMER_H

void Timer_Task(void *params);
void stopTimer();
void sendTimeToNetwork(int *timeElapsed);

#endif