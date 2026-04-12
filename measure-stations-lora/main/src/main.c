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
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "KeyValue.h"
#include "OTA.h"

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

int station_id = 0;
int controller_id = 0;
int start_id = 0;
int stop_id = 0;
int is_xrl = 0;
int num_fake_sensors = 0;
int num_sensors_required_for_trigger = 0;

void start_isr_service_tast(void *params)
{
    TaskHandle_t mainTask = (TaskHandle_t)params;
    gpio_install_isr_service(0);
    // Notify main task that isr service is installed
    xTaskNotifyGive(mainTask);
    vTaskDelete(NULL);
}

//Task that checks for 10 seconds if boot button was pressed to trigger OTA mode
void ota_check_task(void *params)
{
    const int boot_button_gpio = GPIO_NUM_0;
    gpio_set_direction(boot_button_gpio, GPIO_MODE_INPUT);
    gpio_pullup_en(boot_button_gpio);
    gpio_pulldown_dis(boot_button_gpio);

    int pressed_count = 0;
    for (int i = 0; i < 100; i++)
    {
        if (gpio_get_level(boot_button_gpio) == 0) // Assuming active low button
        {
            pressed_count++;
        }
        else
        {
            pressed_count = 0; // reset count if button is released
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
    }

    if (pressed_count >= 50) // Button was pressed for at least 5 seconds
    {
        ESP_LOGI("OTA_CHECK", "Boot button held for 5 seconds, entering OTA mode");
        xTaskCreate(ota_task, "ota_task", 16384, NULL, 5, NULL);
    }
    else
    {
        ESP_LOGI("OTA_CHECK", "Boot button not held long enough, starting normally");
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
    const char *TAG = "MAIN";
    ESP_LOGI(TAG, "Starting...");
    nvs_flash_init();

    // Configure IDs

    controller_id = getValue("controller_id");

    if (controller_id == 0)
    {
        controller_id = CONFIG_LORA_CONTROLLER_ID;
        storeValue("controller_id", CONFIG_LORA_CONTROLLER_ID);
    }

    start_id = getValue("start_id");

    if (start_id == 0)
    {
        start_id = CONFIG_START_LORA_ID;
        storeValue("start_id", CONFIG_START_LORA_ID);
    }

    stop_id = getValue("stop_id");

    if (stop_id == 0)
    {
        stop_id = CONFIG_STOP_LORA_ID;
        storeValue("stop_id", CONFIG_STOP_LORA_ID);
    }

    station_id = getValue("station_id");
    if (station_id == 0)
    {
        station_id = CONFIG_LORA_STATION_ID;
        storeValue("station_id", CONFIG_LORA_STATION_ID);
    }

#ifdef CONFIG_IS_XLR
    is_xrl = 1;
    num_fake_sensors = CONFIG_NUM_FAKE_SENSORS;
    storeValue("is_xrl", 1);
    storeValue("num_fake_s", CONFIG_NUM_FAKE_SENSORS);
    num_sensors_required_for_trigger = 1;
#else
    num_sensors_required_for_trigger = getValue("num_s_req");
    if (num_sensors_required_for_trigger == 0)
    {
        num_sensors_required_for_trigger = 2;
    }
#ifdef NUM_SENSORS_REQUIRED_FOR_TRIGGER
    num_sensors_required_for_trigger = CONFIG_NUM_SENSORS_REQUIRED_FOR_TRIGGER;
    storeValue("num_sensors_required_for_trigger", num_sensors_required_for_trigger);
#endif
#endif

    is_xrl = getValue("is_xrl");
    num_fake_sensors = getValue("num_fake_s");
    //-------

    xTaskCreate(start_isr_service_tast, "StartISRServiceTask", 4048, xTaskGetCurrentTaskHandle(), 5, NULL);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // mainTask = xTaskGetCurrentTaskHandle();
    InitLoraHandlers(HandleReceivedPacket);

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(int));
    buzzerQueue = xQueueCreate(5, sizeof(int));
    faultQueue = xQueueCreate(5, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));

    // gpio_install_isr_service(0);
    init_lora();

    set_all_leds(255, 0, 255); // Set all leds to purple while waiting for time sync
    xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 24, NULL);
    xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048, NULL, 12, NULL);

    xTaskCreate(Buzzer_Task, "Buzzer_Task", 8192, NULL, 12, NULL);
    xTaskCreatePinnedToCore(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 8192 * 2, NULL, 3, &sensorInterruptTaskHandle, 0);

    int buzzerType = BUZZER_STARTUP;
    xQueueSend(buzzerQueue, &buzzerType, 0);
}
