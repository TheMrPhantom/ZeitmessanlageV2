/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "lv_conf.h"
#include "esp_lcd_touch_cst816s.h"
#include <esp_system.h>
#include "soc/soc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "Timer.h"
#include "Network.h"
#include "SevenSegment.h"
#include "NetworkFault.h"
#include "KeyValue.h"
#include "Buzzer.h"
#include "Keyboard.h"
#include "ButtonInput.h"
#include "Button.h"

QueueHandle_t sensorInterputQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;
QueueHandle_t sevenSegmentQueue;
QueueHandle_t networkFaultQueue;
QueueHandle_t timeQueue;
QueueHandle_t sendQueue;
QueueHandle_t buzzerQueue;
QueueSetHandle_t triggerAndResetQueue;
TaskHandle_t buttonTask;

static const char *TAG = "Main";

void test(void)
{
    for (int i = 0; i < 15; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
        receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));
    }

    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    }

    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
        receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));
    }
}

void app_main(void)
{

    nvs_flash_init();

    ESP_LOGI(TAG, "Starting...");

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    resetQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(int));
    networkFaultQueue = xQueueCreate(2, sizeof(int));
    sevenSegmentQueue = xQueueCreate(10, sizeof(SevenSegmentDisplay));
    timeQueue = xQueueCreate(1, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));
    buzzerQueue = xQueueCreate(10, sizeof(int));

    triggerAndResetQueue = xQueueCreateSet(2);
    xQueueAddToSet(triggerQueue, triggerAndResetQueue);
    xQueueAddToSet(resetQueue, triggerAndResetQueue);

    increaseKey("startups");

    init_keyboard();
    init_glow_pins();

    xTaskCreate(Timer_Task, "Timer_Task", 4048, NULL, 9, NULL);
    xTaskCreate(Network_Fault_Task, "Network_Fault_Task", 2048, NULL, 7, NULL);
    xTaskCreatePinnedToCore(Seven_Segment_Task, "Seven_Segment_Task", 16096, NULL, 8, NULL, 1);
    xTaskCreate(Network_Task, "Network_Task", 8192, NULL, 9, NULL);
    xTaskCreate(Network_Send_Task, "Network_Send_Task", 8192, NULL, 10, NULL);
    xTaskCreate(Buzzer_Task, "Buzzer_Task", 4048, NULL, 7, NULL);
    xTaskCreate(Button_Input_Task, "Button_Input_Task", 8192, NULL, 8, NULL);
    xTaskCreate(Button_Task, "Button_Task", 8192, NULL, 3, &buttonTask);

    /*
    vTaskDelay(pdMS_TO_TICKS(4000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));

    xTaskCreate(test, "test", 4048, NULL, 3, NULL);
    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(8746));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));
    vTaskDelay(pdMS_TO_TICKS(16273));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"reset", strlen("reset"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"reset", strlen("reset"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"countdown-7", strlen("countdown-7"));

    vTaskDelay(pdMS_TO_TICKS(1000 * 60 * 7 + 15));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));
    */
}
