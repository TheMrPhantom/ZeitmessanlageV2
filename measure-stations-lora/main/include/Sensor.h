#ifndef SENSOR_H
#define SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define MEASUREMENT_SENSOR_READY BIT0

extern EventGroupHandle_t measurementStartupEvents;

typedef struct PinTrigger
{
    int pin;
    int state;
    int64_t triggered_at_us;
} PinTrigger;

void Sensor_Interrupt_Task(void *params);
void Sensor_Status_Task(void *params);
esp_err_t init_Pins(void);
bool get_last_release_timestamp(int64_t *timestamp);

#endif
