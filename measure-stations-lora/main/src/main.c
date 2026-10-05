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
int num_sensors;
int triggerLevel;
static int sensor_pin_storage[10];
int *sensorPins = sensor_pin_storage;

static void measure_station_ota_status(dogdog_ota_event_t event, void *user_ctx)
{
    (void)user_ctx;

    if (event == DOGDOG_OTA_EVENT_WIFI_FOUND)
    {
        xQueueSend(buzzerQueue, &(int){Buzzer_INDICATE_OTA}, 0);
    }
}

void start_isr_service_tast(void *params)
{
    TaskHandle_t mainTask = (TaskHandle_t)params;
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    // Notify main task that isr service is installed
    xTaskNotifyGive(mainTask);
    vTaskDelete(NULL);
}

void app_main(void)
{
    const char *TAG = "MAIN";
    esp_reset_reason_t reset_reason = esp_reset_reason();
    if (reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_INT_WDT ||
        reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_WDT || reset_reason == ESP_RST_BROWNOUT)
    {
        ESP_LOGE(TAG, "Previous reset reason: %d", (int)reset_reason);
    }
    if (dogdog_ota_update_pending())
    {
        const dogdog_ota_config_t pending_ota_config = {
            .device_name = "measure-stations-lora",
            .status_cb = NULL,
            .user_ctx = NULL,
            .restart_before_update = false,
            .restart_delay_ms = 0,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(dogdog_ota_check_and_update_in_task(&pending_ota_config, 0));
    }

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
    if (is_xrl && (num_fake_sensors < 1 || num_fake_sensors > 64))
    {
        ESP_LOGW(TAG, "Invalid sensor count %d; limiting to the 64-bit state field", num_fake_sensors);
        num_fake_sensors = num_fake_sensors < 1 ? 1 : 64;
    }
    //-------

    // Cycle-count timestamps must be captured on the same core as the sensor task.
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(start_isr_service_tast, "StartISRServiceTask", 4048,
                                          xTaskGetCurrentTaskHandle(), 5, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // mainTask = xTaskGetCurrentTaskHandle();
    InitLoraHandlers(HandleReceivedPacket);

    sensorInterputQueue = xQueueCreate(5, sizeof(PinTrigger));
    triggerQueue = xQueueCreate(1, sizeof(int));
    buzzerQueue = xQueueCreate(5, sizeof(int));
    faultQueue = xQueueCreate(5, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));
    ESP_ERROR_CHECK(sensorInterputQueue && triggerQueue && buzzerQueue && faultQueue && sendQueue ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(xTaskCreate(Buzzer_Task, "Buzzer_Task", 8192, NULL, 12, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    const dogdog_ota_config_t ota_config = {
        .device_name = "measure-stations-lora",
        .status_cb = measure_station_ota_status,
        .user_ctx = NULL,
        .restart_before_update = true,
        .restart_delay_ms = 1200,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(dogdog_ota_check_and_update_in_task(&ota_config, 0));

    // gpio_install_isr_service(0);

    if (is_xrl)
    {
        triggerLevel = 1;
        num_sensors = 1;
        sensorPins[0] = GPIO_NUM_47;
    }
    else
    {
        triggerLevel = 0;
        num_sensors = 10;
        sensorPins[0] = GPIO_NUM_15;
        sensorPins[1] = GPIO_NUM_16;
        sensorPins[2] = GPIO_NUM_17;
        sensorPins[3] = GPIO_NUM_18;
        sensorPins[4] = GPIO_NUM_8;
        sensorPins[5] = GPIO_NUM_19;
        sensorPins[6] = GPIO_NUM_20;
        sensorPins[7] = GPIO_NUM_39;
        sensorPins[8] = GPIO_NUM_38;
        sensorPins[9] = GPIO_NUM_37;
    }

    init_led(num_sensors); // Pass the number of sensors as argument
    init_lora();

    set_all_leds(255, 0, 255); // Set all leds to purple while waiting for time sync
    ESP_ERROR_CHECK(xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 24, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048, NULL, 12, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 8192 * 2,
                                          NULL, 3, &sensorInterruptTaskHandle, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    int buzzerType = BUZZER_STARTUP;
    xQueueSend(buzzerQueue, &buzzerType, 0);
}
