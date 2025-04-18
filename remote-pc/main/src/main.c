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

#include "Sensor.h"
#include "Network.h"
#include "main.h"
#include "Keyboard.h"
#include "LED.h"
#include "Button.h"
#include "Printer.h"

QueueHandle_t sensorInterputQueue;
QueueHandle_t networkQueue;
extern QueueHandle_t buttonQueue;
QueueHandle_t sendQueue;

void app_main(void)
{
    const char *TAG = "MAIN";
    ESP_LOGI(TAG, "Starting...");
    init_glow_pins();

    sensorInterputQueue = xQueueCreate(1, sizeof(sensor_interrupt_t));
    networkQueue = xQueueCreate(50, sizeof(char *));

    init_keyboard();
    init_led();
    init_printer();

    set_led(0, 0, 10);

    xTaskCreate(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 8192, NULL, 1, NULL);
    xTaskCreate(Network_Task, "Network_Task", 8192, NULL, 2, NULL);
    xTaskCreate(Button_Task, "Button_Glow_Task", 8192, NULL, 3, NULL);
    xTaskCreate(Network_Send_Task, "Network_Send_Task", 8192, NULL, 4, NULL);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
