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
#include "SevenSegment.h"
#include "NetworkFault.h"
#include <esp_ota_ops.h>
#include <string.h>
#include <esp_system.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "KeyValue.h"

QueueHandle_t sensorInterputQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;
QueueHandle_t sevenSegmentQueue;
QueueHandle_t networkFaultQueue;
QueueHandle_t timeQueue;
QueueHandle_t sendQueue;
QueueSetHandle_t triggerAndResetQueue;

void app_main(void)
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    const char *TAG = "MAIN";
    ESP_LOGI(TAG, "Starting...");

    nvs_flash_init();

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    resetQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(int));
    networkFaultQueue = xQueueCreate(2, sizeof(int));
    sevenSegmentQueue = xQueueCreate(10, sizeof(SevenSegmentDisplay));
    timeQueue = xQueueCreate(1, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));

    triggerAndResetQueue = xQueueCreateSet(2);
    xQueueAddToSet(triggerQueue, triggerAndResetQueue);
    xQueueAddToSet(resetQueue, triggerAndResetQueue);

    increaseKey("startups");

    xTaskCreate(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 2048, NULL, 1, NULL);
    xTaskCreate(Timer_Task, "Timer_Task", 2048, NULL, 1, NULL);
    xTaskCreate(Network_Fault_Task, "Network_Fault_Task", 2048, NULL, 1, NULL);
    xTaskCreatePinnedToCore(Seven_Segment_Task, "Seven_Segment_Task", 4096, NULL, 1, NULL, 1);
    xTaskCreate(Network_Task, "Network_Task", 8192, NULL, 2, NULL);
    xTaskCreate(Network_Send_Task, "Network_Send_Task", 8192, NULL, 3, NULL);
}
