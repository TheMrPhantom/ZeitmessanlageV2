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

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

extern QueueHandle_t sensorInterputQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t triggerQueue;

char *TAG = "SENSOR";
const int sensorPins[] = {32, 33, 35};
const int sensorCooldown = 1000;

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(sensorInterputQueue, &pinNumber, NULL);
}

void init_Pins()
{
    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        ESP_LOGI(TAG, "Configuring IO Pin %i", sensorPins[i]);
        esp_rom_gpio_pad_select_gpio(sensorPins[i]);
        gpio_set_direction((gpio_num_t)sensorPins[i], GPIO_MODE_INPUT);
        gpio_pullup_en((gpio_num_t)sensorPins[i]);
        gpio_pulldown_dis((gpio_num_t)sensorPins[i]);
        gpio_set_intr_type((gpio_num_t)sensorPins[i], GPIO_INTR_POSEDGE);
    }

    ESP_LOGI(TAG, "Done configuring IO");

    gpio_install_isr_service(0);

    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        ESP_LOGI(TAG, "Configuring ISR for Pin %i", sensorPins[i]);
        gpio_isr_handler_add((gpio_num_t)sensorPins[i], gpio_interrupt_handler, (void *)sensorPins[i]);
    }

    ESP_LOGI(TAG, "Done configuring ISR");
}

void Sensor_Interrupt_Task(void *params)
{
    ESP_LOGI(TAG, "Setting up Sensors");
    init_Pins();

    int pinNumber = 0;
    int lastTriggerTime = 0;

    while (true)
    {
        if (xQueueReceive(sensorInterputQueue, &pinNumber, portMAX_DELAY))
        {

            if (lastTriggerTime < (int)pdTICKS_TO_MS(xTaskGetTickCount()) - sensorCooldown)
            {
                int cause = 0;
                lastTriggerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                ESP_LOGI(TAG, "Interrupt of Pin: %i", pinNumber);

                xQueueSend(resetQueue, &cause, 0);
            }
        }
    }
}