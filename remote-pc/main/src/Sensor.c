#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "Sensor.h"
#include "Keyboard.h"
#include "Network.h"
#include "Button.h"

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

extern QueueHandle_t sensorInterputQueue;
extern QueueHandle_t buttonQueue;
static const char *TAG = "SENSOR";
const int sensorButtonPins[] = {BUTTON_TYPE_ACTIVATE, BUTTON_TYPE_DIS, BUTTON_TYPE_FAULT, BUTTON_TYPE_REFUSAL, BUTTON_TYPE_RESET};
const int sensorCooldown = 3500;
const int faultCooldown = 3000;

bool sensors_active = false;

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    xQueueSendFromISR(sensorInterputQueue, &pinNumber, NULL);
}

void init_button_pins()
{
    ESP_LOGI(TAG, "Configuring IO");
    for (int i = 0; i < sizeof(sensorButtonPins) / sizeof(int); i++)
    {

        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_NEGEDGE;              // falling edge
        io_conf.pin_bit_mask = 1ULL << sensorButtonPins[i]; // select pin
        io_conf.mode = GPIO_MODE_INPUT;                     // input mode
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;            // enable pull-up mode
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;       // disable pull-down mode (was not possible only with PULLUP_ENABLE.)
        gpio_config(&io_conf);
    }

    ESP_LOGI(TAG, "Done configuring IO");

    gpio_install_isr_service(0);

    for (int i = 0; i < sizeof(sensorButtonPins) / sizeof(int); i++)
    {
        ESP_LOGI(TAG, "Configuring ISR for Pin %i", sensorButtonPins[i]);
        gpio_isr_handler_add(sensorButtonPins[i], gpio_interrupt_handler, (void *)sensorButtonPins[i]);
    }

    ESP_LOGI(TAG, "Done configuring ISR");
}

void Sensor_Interrupt_Task(void *params)
{
    ESP_LOGI(TAG, "Setting up Sensors");
    init_button_pins();

    int pinNumber = 0;

    timeval_t last_button_interrupt;
    gettimeofday(&last_button_interrupt, NULL);

    while (true)
    {
        if (xQueueReceive(sensorInterputQueue, &pinNumber, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i", pinNumber);

            vTaskDelay(pdMS_TO_TICKS(3));
            timeval_t now;
            gettimeofday(&now, NULL);

            if (gpio_get_level(pinNumber) == 0 && (TIME_US(now) - TIME_US(last_button_interrupt) > 100000))
            {
                gettimeofday(&last_button_interrupt, NULL);

                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", pinNumber);

                if (pinNumber == BUTTON_TYPE_ACTIVATE)
                {
                    sensors_active = !sensors_active;

                    glow_state_t glow_state;
                    if (sensors_active)
                    {
                        glow_state.state = 1;
                        glow_state.pinNumber = BUTTON_GLOW_TYPE_ACTIVATE;
                        xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    }
                    else
                    {
                        glow_state.state = 0;
                    }
                    ESP_LOGI(TAG, "Sensors are now %s", sensors_active ? "active" : "inactive");

                    glow_state.pinNumber = BUTTON_GLOW_TYPE_FAULT;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));

                    glow_state.pinNumber = BUTTON_GLOW_TYPE_REFUSAL;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));

                    glow_state.pinNumber = BUTTON_GLOW_TYPE_DIS;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                }
                else if (pinNumber == BUTTON_TYPE_RESET)
                {
                    glow_state_t glow_state;
                    glow_state.state = 0;
                    glow_state.pinNumber = BUTTON_GLOW_TYPE_RESET;
                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    broadcast("reset");
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
