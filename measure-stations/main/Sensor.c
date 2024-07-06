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
#include "Buzzer.h"

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

extern QueueHandle_t sensorInterputQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t buzzerQueue;
extern QueueHandle_t faultQueue;

char *TAG = "SENSOR";
const int sensorPins[] = {16, 17, 18, 19, 21};
const int sensorCooldown = 2000;

int faultTime = 0;
bool faultWarning = false;
bool fault = false;

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
        gpio_set_direction(sensorPins[i], GPIO_MODE_INPUT);
        gpio_pulldown_en(sensorPins[i]);
        gpio_pullup_dis(sensorPins[i]);
        gpio_set_intr_type(sensorPins[i], GPIO_INTR_POSEDGE);
    }

    ESP_LOGI(TAG, "Done configuring IO");

    gpio_install_isr_service(0);

    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        ESP_LOGI(TAG, "Configuring ISR for Pin %i", sensorPins[i]);
        gpio_isr_handler_add(sensorPins[i], gpio_interrupt_handler, (void *)sensorPins[i]);
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
        if (xQueueReceive(sensorInterputQueue, &pinNumber, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(TAG, "Interrupt of Pin: %i", pinNumber);

            if (lastTriggerTime < (int)pdTICKS_TO_MS(xTaskGetTickCount()) - sensorCooldown)
            {
                if (!fault)
                {
                    int cause = 0;
                    lastTriggerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                    ESP_LOGI(TAG, "Interrupt of Pin: %i", pinNumber);

                    xQueueSend(triggerQueue, &cause, 0);

                    cause = Buzzer_TRIGGER;
                    xQueueSend(buzzerQueue, &cause, 0);
                }
                else
                {
                    ESP_LOGI(TAG, "Triggered but fault was detected so no signal will be sent");
                }
            }
        }

        // Check for faults only 4 seconds after startup
        if (pdTICKS_TO_MS(xTaskGetTickCount()) > 4000)
        {
            bool isCurrentlyGood = true;
            int currentFaults = 0;
            for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
            {
                int level = gpio_get_level(sensorPins[i]);
                currentFaults += level;
            }

            if (currentFaults > 0)
            {
                isCurrentlyGood = false;
            }

            if (!faultWarning && !isCurrentlyGood)
            {
                // Currently disconnected but not in warning state -> aktivate warning state
                faultTime = xTaskGetTickCount();
                faultWarning = true;
            }

            if (faultWarning)
            {
                if (pdTICKS_TO_MS(xTaskGetTickCount()) - pdTICKS_TO_MS(faultTime) > 3000 && !fault)
                {
                    // Currently in warning state, timout reached but no fault activated yet -> go into fault state
                    fault = true;

                    int cause = Buzzer_INDICATE_ERROR;
                    xQueueSend(buzzerQueue, &cause, 0);
                    xQueueSend(faultQueue, &cause, 0);

                    ESP_LOGI(TAG, "Sensor connection is lost");
                }
            }

            if (isCurrentlyGood)
            {
                if (fault)
                {
                    // No more fault
                    int cause = Buzzer_INDICATE_ERROR;
                    xQueueSend(buzzerQueue, &cause, 0);
                    xQueueSend(faultQueue, &cause, 0);
                    ESP_LOGI(TAG, "Sensor connection restored");
                }
                faultWarning = false;
                fault = false;
            }
        }
    }
}