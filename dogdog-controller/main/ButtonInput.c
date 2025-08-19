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
#include "ButtonInput.h"
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
extern QueueHandle_t resetQueue;
extern QueueHandle_t sevenSegmentQueue;
static const char *TAG = "SENSOR";
const int sensorButtonPins[] = {BUTTON_TYPE_ACTIVATE, BUTTON_TYPE_DIS, BUTTON_TYPE_FAULT, BUTTON_TYPE_REFUSAL, BUTTON_TYPE_RESET};

bool sensors_active = false;

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    int edge = gpio_get_level(pinNumber) == 0 ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    sensor_interrupt_t sensor_interrupt;
    sensor_interrupt.pinNumber = pinNumber;
    sensor_interrupt.edge = edge;
    xQueueSendFromISR(sensorInterputQueue, &sensor_interrupt, NULL);
}

void init_button_pins()
{
    ESP_LOGI(TAG, "Configuring IO");
    for (int i = 0; i < sizeof(sensorButtonPins) / sizeof(int); i++)
    {

        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_ANYEDGE;              // any edge
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

void Button_Input_Task(void *params)
{
    ESP_LOGI(TAG, "Setting up Buttons");
    init_button_pins();

    sensor_interrupt_t sensor_interrupt;

    timeval_t last_button_interrupt;
    timeval_t reset_pressed;
    gettimeofday(&last_button_interrupt, NULL);
    gettimeofday(&reset_pressed, NULL);
    int countdown_sent = 1;

    while (true)
    {
        if (xQueueReceive(sensorInterputQueue, &sensor_interrupt, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i with state %i", sensor_interrupt.pinNumber, sensor_interrupt.edge);

            vTaskDelay(pdMS_TO_TICKS(3));
            timeval_t now;
            gettimeofday(&now, NULL);

            if (gpio_get_level(sensor_interrupt.pinNumber) == 0 && (TIME_US(now) - TIME_US(last_button_interrupt) > 300000))
            {
                gettimeofday(&last_button_interrupt, NULL);

                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", sensor_interrupt.pinNumber);

                if (sensor_interrupt.pinNumber == BUTTON_TYPE_ACTIVATE)
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
                else if (sensor_interrupt.pinNumber == BUTTON_TYPE_RESET)
                {
                    glow_state_t glow_state;
                    glow_state.state = 0;
                    glow_state.pinNumber = BUTTON_GLOW_TYPE_RESET;

                    gettimeofday(&reset_pressed, NULL);
                    countdown_sent = 0;

                    xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    int toSend = 0;
                    xQueueSend(resetQueue, &toSend, 0);

                    SevenSegmentDisplay toSendSevenSegment;
                    toSendSevenSegment.type = SEVEN_SEGMENT_COUNTDOWN_RESET;
                    xQueueSend(sevenSegmentQueue, &toSendSevenSegment, 0);

                    toSendSevenSegment.type = SEVEN_SEGMENT_RESET_FAULT_REFUSAL;
                    xQueueSend(sevenSegmentQueue, &toSendSevenSegment, pdMS_TO_TICKS(500));
                }
                else
                {
                    if (sensors_active)
                    {
                        if (sensor_interrupt.pinNumber == BUTTON_TYPE_FAULT)
                        {
                            BaseType_t result = sendKey(HID_KEY_F);
                            SevenSegmentDisplay toSendSevenSegment;
                            toSendSevenSegment.type = SEVEN_SEGMENT_INCREASE_FAULT;
                            xQueueSend(sevenSegmentQueue, &toSendSevenSegment, 0);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                        }
                        else if (sensor_interrupt.pinNumber == BUTTON_TYPE_REFUSAL)
                        {
                            BaseType_t result = sendKey(HID_KEY_R);
                            SevenSegmentDisplay toSendSevenSegment;
                            toSendSevenSegment.type = SEVEN_SEGMENT_INCREASE_REFUSAL;
                            xQueueSend(sevenSegmentQueue, &toSendSevenSegment, 0);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                        }
                        else if (sensor_interrupt.pinNumber == BUTTON_TYPE_DIS)
                        {
                            BaseType_t result = sendKey(HID_KEY_D);
                            ESP_LOGI(TAG, "Result of sending key: %i", result);
                            SevenSegmentDisplay toSendSevenSegment;
                            toSendSevenSegment.type = SEVEN_SEGMENT_DIS;
                            xQueueSend(sevenSegmentQueue, &toSendSevenSegment, pdMS_TO_TICKS(500));
                        }
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Sensors are not active");
                    }
                }
            }
        }

        timeval_t now;
        gettimeofday(&now, NULL);

        if (gpio_get_level(BUTTON_TYPE_RESET) == 0 && (TIME_US(now) - TIME_US(reset_pressed) > 1000000) && countdown_sent == 0)
        {
            countdown_sent = 1;
            SevenSegmentDisplay toSend;
            toSend.type = SEVEN_SEGMENT_COUNTDOWN;
            toSend.time = 60 * 7 * 1000;
            xQueueSend(sevenSegmentQueue, &toSend, 0);
            gettimeofday(&reset_pressed, NULL);
            ESP_LOGI(TAG, "Countdown started");
        }
    }
}
