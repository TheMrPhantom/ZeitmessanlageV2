#ifndef __CLOCK_H
#define __CLOCK_H

#include <sys/time.h>
#include "esp-idf-ds3231.h"
#include "GPIOPins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

typedef struct timeval timeval_t;

#define TIME_US(t) ((int64_t)t.tv_sec * 1000000L + (int64_t)t.tv_usec)
void ClockTask(void *arg);
void init_external_clock();

#endif // __CLOCK_H