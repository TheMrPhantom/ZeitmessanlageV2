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
#include "Lora.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "Sensor.h"
#include "Buzzer.h"
#include "LED.h"
#include "LoraNetwork.h"

QueueHandle_t sensorInterputQueue;
QueueHandle_t networkQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;
QueueHandle_t buzzerQueue;
QueueHandle_t faultQueue;
QueueSetHandle_t networkAndResetQueue;
QueueHandle_t sendQueue;

TaskHandle_t networkTask;
TaskHandle_t sensorInterruptTaskHandle;

void app_main(void)
{
    const char *TAG = "MAIN";
    ESP_LOGI(TAG, "Starting...");
    // mainTask = xTaskGetCurrentTaskHandle();
    InitLoraHandlers(HandleReceivedPacket);

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(int));
    buzzerQueue = xQueueCreate(5, sizeof(int));
    faultQueue = xQueueCreate(5, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));

    //gpio_install_isr_service(0);
    init_lora();
    init_led(get_num_sensors()); // Pass the number of sensors as argument
    set_all_leds(255, 0, 255);   // Set all leds to purple while waiting for time sync
    xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 24, NULL);
    xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048, NULL, 12, NULL);

    xTaskCreate(Buzzer_Task, "Buzzer_Task", 8192, NULL, 12, NULL);
    xTaskCreatePinnedToCore(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 8192 * 2, NULL, 3, &sensorInterruptTaskHandle, 0);

    int buzzerType = BUZZER_STARTUP;
    xQueueSend(buzzerQueue, &buzzerType, 0);
}
