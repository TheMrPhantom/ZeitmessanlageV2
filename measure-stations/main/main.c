/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "Sensor.h"
#include "Timer.h"
#include "Network.h"

QueueHandle_t sensorInterputQueue;
QueueHandle_t timerQueue;
QueueHandle_t networkQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;

QueueSetHandle_t networkAndResetQueue;

void app_main(void)
{
    const char *TAG = "MAIN";
    ESP_LOGI(TAG, "Starting...");

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    timerQueue = xQueueCreate(1, sizeof(int));
    networkQueue = xQueueCreate(1, sizeof(int));
    resetQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(int));

    networkAndResetQueue = xQueueCreateSet(2);
    xQueueAddToSet(networkQueue, networkAndResetQueue);
    xQueueAddToSet(resetQueue, networkAndResetQueue);
    xQueueAddToSet(triggerQueue, networkAndResetQueue);

    xTaskCreate(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 2048, NULL, 1, NULL);
    xTaskCreate(Timer_Task, "Timer_Task", 2048, NULL, 1, NULL);
    xTaskCreate(Network_Task, "Network_Task", 8192, NULL, 2, NULL);
}
