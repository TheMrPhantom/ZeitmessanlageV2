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
#include "Network.h"
#include "Buzzer.h"

QueueHandle_t sensorInterputQueue;
QueueHandle_t networkQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;
QueueHandle_t buzzerQueue;
QueueHandle_t faultQueue;
QueueSetHandle_t networkAndResetQueue;
QueueHandle_t sendQueue;

void app_main(void)
{
    const char *TAG = "MAIN";
    ESP_LOGI(TAG, "Starting...");

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(int));
    buzzerQueue = xQueueCreate(5, sizeof(int));
    faultQueue = xQueueCreate(5, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));

    xTaskCreate(Buzzer_Task, "Buzzer_Task", 2048, NULL, 1, NULL);
    xTaskCreate(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 2048, NULL, 1, NULL);
    xTaskCreate(Network_Task, "Network_Task", 8192, NULL, 2, NULL);
    xTaskCreate(Network_Send_Task, "Network_Send_Task", 8192, NULL, 3, NULL);

    int buzzerType = BUZZER_STARTUP;
    xQueueSend(buzzerQueue, &buzzerType, 0);
}
