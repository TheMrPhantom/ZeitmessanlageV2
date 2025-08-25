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
#include "LED.h"
#include "LoraNetwork.h"

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

extern QueueHandle_t sensorInterputQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t buzzerQueue;
extern QueueHandle_t faultQueue;
extern QueueHandle_t loraSendQueue;
QueueHandle_t sensorStatusQueue;

TaskHandle_t sensorTask;

char *TAG = "SENSOR";
const int sensorPins[] = {GPIO_NUM_2, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_26};
const int sensorCooldown = 3500;
const int faultCooldown = 3000;
extern int64_t time_offset_to_controller;

int faultTime = 0;
bool faultWarning = false;
bool fault = false;

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    int pinNumber = (int)args;
    // read pin state
    int pinState = gpio_get_level(pinNumber);
    if (pinState == 1)
    {
        xQueueSendFromISR(sensorInterputQueue, &pinNumber, NULL);
    }
    xQueueSendFromISR(sensorStatusQueue, &pinNumber, NULL);
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
        gpio_set_intr_type(sensorPins[i], GPIO_INTR_ANYEDGE);
    }

    ESP_LOGI(TAG, "Done configuring IO");

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
    sensorStatusQueue = xQueueCreate(1, sizeof(char *));
    xTaskCreate(Sensor_Status_Task, "Sensor_Status_Task", 2048 * 2, NULL, 1, &sensorTask);
    int numPins = sizeof(sensorPins) / sizeof(int);
    xTaskCreate(LED_Task, "LED_Task", 4048, &numPins, 1, NULL);
    // Wait for led

    int pinNumber = 0;
    int lastTriggerTime = 0;

    while (true)
    {
        if (xQueueReceive(sensorInterputQueue, &pinNumber, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i", pinNumber);

            // vTaskDelay(pdMS_TO_TICKS(3));

            if (gpio_get_level(pinNumber) == 1)
            {
                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", pinNumber);
                if (lastTriggerTime < (int)pdTICKS_TO_MS(xTaskGetTickCount()) - sensorCooldown)
                {
                    if (!fault)
                    {
                        int cause = 0;

                        ESP_LOGI(TAG, "Interrupt of Pin: %i", pinNumber);

                        int num_triggered_sensors = 0;
                        // Check if at least 2 sensors are triggered
                        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
                        {
                            if (gpio_get_level(sensorPins[i]) == 1)
                            {
                                num_triggered_sensors++;
                            }
                        }

                        if (num_triggered_sensors < 2)
                        {
                            ESP_LOGW(TAG, "Not enough sensors triggered");
                            continue;
                        }

                        lastTriggerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                        
                        timeval_t current_time;
                        gettimeofday(&current_time, NULL);

                        int64_t timestamp = TIME_US(current_time) + time_offset_to_controller;

                        PacketTypeTrigger trigger;
                        trigger.timestamp = timestamp;

                        PacketTypeSensorState sensors_state;
                        sensors_state.num_sensors = sizeof(sensorPins) / sizeof(int);
                        sensors_state.sensor_states = 0;

                        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
                        {
                            int level = gpio_get_level(sensorPins[i]);
                            sensors_state.sensor_states |= ((uint64_t)level << i);
                        }
                        sensors_state.sensor_states = ~sensors_state.sensor_states;
                        trigger.sensor_state = sensors_state;

                        DogDogPacket *packet = create_dogdog_packet_from_trigger_information(&trigger);
                        send_dogdog_packet(packet);

                        cause = Buzzer_TRIGGER;
                        xQueueSend(buzzerQueue, &cause, 0);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Triggered but fault was detected so no signal will be sent");
                    }
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
                if (pdTICKS_TO_MS(xTaskGetTickCount()) - pdTICKS_TO_MS(faultTime) > faultCooldown && !fault)
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

/*
 * Reads every second if the sensor has contact and sends it to the controller as one message
 */
void Sensor_Status_Task(void *params)
{

    timeval_t last_time_clean[sizeof(sensorPins) / sizeof(int)];
    timeval_t current_time;
    timeval_t last_time_sent;
    gettimeofday(&last_time_sent, NULL);
    bool last_state[sizeof(sensorPins) / sizeof(int)];
    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        gettimeofday(&last_time_clean[i], NULL);
        last_state[i] = gpio_get_level(sensorPins[i]);
    }

    ESP_LOGI(TAG, "Waiting for LED Task");

    // Wait for led task notification
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    ESP_LOGI(TAG, "LED Task is ready");

    while (true)
    {
        int pinNumber;
        BaseType_t newDataReceived = xQueueReceive(sensorStatusQueue, &pinNumber, pdMS_TO_TICKS(5000));
        gettimeofday(&current_time, NULL);
        // check if any of the gpio pins are high with bit mask call
        bool any_tiggered = 0;
        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
        {
            any_tiggered |= gpio_get_level(sensorPins[i]);
        }

        PacketTypeSensorState sensors_state;
        sensors_state.num_sensors = sizeof(sensorPins) / sizeof(int);
        sensors_state.sensor_states = 0;

        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
        {
            int level = gpio_get_level(sensorPins[i]);
            sensors_state.sensor_states |= ((uint64_t)level << i);

            if (any_tiggered)
            {
                // Set led to green if 0 and red if 1

                if (level == 0)
                {
                    set_led(i, 0, 150, 0); // Set LED to green
                    gettimeofday(&last_time_clean[i], NULL);
                }
                else
                {
                    set_led(i, 150, 0, 0); // Set LED to red
                }
            }
            else
            {
                if (level == 0 && last_state[i] == 1)
                {
                    gettimeofday(&last_time_clean[i], NULL);
                }

                gettimeofday(&current_time, NULL);
                // If no sensor is triggered, turn off the LED if it was previously on for 8 seconds
                if (TIME_US(current_time) - TIME_US(last_time_clean[i]) > 8000000)
                {
                    set_led(i, 0, 0, 0); // Turn off LED
                }
                else
                {
                    set_led(i, 0, 150, 0); // Set LED to green
                }
            }
            last_state[i] = level;
        }

        if (!newDataReceived || TIME_US(current_time) - TIME_US(last_time_sent) > 2500000)
        {
            // invert the result
            sensors_state.sensor_states = ~sensors_state.sensor_states;
            DogDogPacket *packet = create_dogdog_packet_from_sensor_state_information(&sensors_state);
            if (packet)
            {
                gettimeofday(&last_time_sent, NULL);
                // Send the packet
                xQueueSend(loraSendQueue, &packet, 0);
            }
        }
    }
}