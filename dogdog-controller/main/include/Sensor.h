#ifndef SENSOR_H
#define SENSOR_H

#include <sys/time.h>
#include "Timer.h"
#include "SevenSegment.h"
#include "esp_private/esp_clk.h"
#include "esp_err.h"
typedef struct timeval timeval_t;

typedef struct
{
    int pin_number;
    esp_cpu_cycle_count_t cpu_cycles;
} SensorTriggerEvent;

#define TIME_US(t) ((int64_t)t.tv_sec * 1000000L + (int64_t)t.tv_usec)

void Sensor_Interrupt_Task(void *params);
void Sensor_Status_Task(void *params);
void sendSensorStatus(int triggeredPin, int pinToCheck);
esp_err_t init_Sensor_Pins(void);

#endif
