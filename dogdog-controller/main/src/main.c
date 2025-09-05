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
#include "SevenSegment.h"
#include "NetworkFault.h"
#include "KeyValue.h"
#include "Buzzer.h"
#include "Keyboard.h"
#include "ButtonInput.h"
#include "Button.h"
#include "ra01s.h"
#include "LoraNetwork.h"
#include "Timer.h"
#include <stdio.h>
#include "esp-idf-ds3231.h"
#include "GPIOPins.h"
#include "Clock.h"
#include "Lora.h"

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

void app_main(void)
{

    // Initialize LoRa
    nvs_flash_init();
    gpio_install_isr_service(0);
    InitLoraHandlers(HandleReceivedPacket);

    ESP_LOGI(TAG, "Starting...");

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    resetQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(TimerTrigger));
    networkFaultQueue = xQueueCreate(2, sizeof(StationConnectivityStatus));
    sevenSegmentQueue = xQueueCreate(10, sizeof(SevenSegmentDisplay));
    timeQueue = xQueueCreate(1, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));
    buzzerQueue = xQueueCreate(10, sizeof(int));
    triggerAndResetQueue = xQueueCreateSet(2);
    xQueueAddToSet(triggerQueue, triggerAndResetQueue);
    xQueueAddToSet(resetQueue, triggerAndResetQueue);

    increaseKey("startups");

    init_external_clock();
    init_keyboard();
    init_glow_pins();
    init_lora();

    xTaskCreate(Timer_Task, "Timer_Task", 4048, NULL, 12, NULL);
    xTaskCreate(Network_Fault_Task, "Network_Fault_Task", 4048, NULL, 9, NULL);
    xTaskCreatePinnedToCore(Seven_Segment_Task, "Seven_Segment_Task", 16096, NULL, 8, NULL, 1);
    xTaskCreate(Buzzer_Task, "Buzzer_Task", 4048, NULL, 7, NULL);

    xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 23, NULL);
    xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048, NULL, 23, NULL);
    xTaskCreate(LoraSyncTask, "LoraSyncTask", 4048, NULL, 8, NULL);

    xTaskCreate(Button_Input_Task, "Button_Input_Task", 8192, NULL, 8, NULL);
    xTaskCreate(Button_Task, "Button_Task", 8192, NULL, 3, &buttonTask);

    xTaskCreate(ClockTask, "ClockTask", 4048, NULL, 24, NULL);
}
