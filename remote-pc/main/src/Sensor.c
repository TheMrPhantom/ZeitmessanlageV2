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
#include "Keyboard.h"
#include "Network.h"

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

extern QueueHandle_t sensorInterputQueue;
char *TAG = "SENSOR";
const int sensorPins[] = {16, 21, 0};
const int sensorCooldown = 3500;
const int faultCooldown = 3000;

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
    ESP_LOGI(TAG, "Configuring IO");
    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {

        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_NEGEDGE;        // falling edge
        io_conf.pin_bit_mask = 1ULL << sensorPins[i]; // select pin
        io_conf.mode = GPIO_MODE_INPUT;               // input mode
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;      // enable pull-up mode
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // disable pull-down mode (was not possible only with PULLUP_ENABLE.)
        gpio_config(&io_conf);
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

bool sensors_active = false;

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
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i", pinNumber);

            vTaskDelay(pdMS_TO_TICKS(3));

            if (gpio_get_level(pinNumber) == 0)
            {
                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", pinNumber);

                if (pinNumber == BUTTON_TYPE_ACTIVATE)
                {
                    sensors_active = !sensors_active;
                }
                else
                {
                    if (sensors_active)
                    {
                        if (pinNumber == BUTTON_TYPE_FAULT)
                        {
                            BaseType_t result = sendKey(HID_KEY_F);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                        }
                        else if (pinNumber == BUTTON_TYPE_REFUSAL)
                        {
                            BaseType_t result = sendKey(HID_KEY_V);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                        }
                        else if (pinNumber == BUTTON_TYPE_DIS)
                        {
                            BaseType_t result = sendKey(HID_KEY_D);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                        }
                        else if (pinNumber == BUTTON_TYPE_RESET)
                        {
                            broadcast("reset");
                        }
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Sensors are not active");
                    }
                }
            }
        }
    }
}
