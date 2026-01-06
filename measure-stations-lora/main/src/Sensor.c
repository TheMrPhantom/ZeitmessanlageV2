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

char *TAG = "SENSOR";
const int sensorPins[] = {GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_8, GPIO_NUM_19, GPIO_NUM_20, GPIO_NUM_39, GPIO_NUM_38, GPIO_NUM_37}; // GPIO pins for the sensors
const int sensorCooldown = 1500;

const int faultCooldown = 3000;
extern int64_t time_offset_to_controller;
extern portMUX_TYPE timesync_spinlock;
timeval_t last_time_sent;

uint64_t faultTime = 0;
bool faultWarning = false;
bool fault = false;

uint32_t cpu_hz = 1;
esp_cpu_cycle_count_t last_trigger_cpu_cycles;

int get_num_sensors()
{
    return sizeof(sensorPins) / sizeof(int);
}

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    esp_cpu_cycle_count_t triggered_at = esp_cpu_get_cycle_count();
    int pinNumber = (int)args;
    // read pin state
    int pinState = gpio_get_level(pinNumber);
    if (pinState == 0)
    {
        xQueueSendFromISR(sensorInterputQueue, &pinNumber, NULL);

        last_trigger_cpu_cycles = triggered_at;
    }
    xQueueSendFromISR(sensorStatusQueue, &pinNumber, NULL);
}

void init_Pins()
{
    cpu_hz = (uint32_t)esp_clk_cpu_freq();

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

    sensorStatusQueue = xQueueCreate(1, sizeof(char *));

    ESP_LOGI(TAG, "Waiting for time sync...");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Time synced!");

    xTaskCreate(Sensor_Status_Task, "Sensor_Status_Task", 2048 * 2, NULL, 1, NULL);
    // Wait for led

    int pinNumber = 0;
    uint64_t lastTriggerTime = 0;

    while (true)
    {
        if (xQueueReceive(sensorInterputQueue, &pinNumber, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i", pinNumber);

            // vTaskDelay(pdMS_TO_TICKS(3));

            if (gpio_get_level(pinNumber) == 0)
            {
                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", pinNumber);
                uint64_t currentTickMs = pdTICKS_TO_MS(xTaskGetTickCount());
                if (lastTriggerTime < currentTickMs - sensorCooldown)
                {
                    if (!fault)
                    {
                        int cause = 0;

                        ESP_LOGI(TAG, "Interrupt of Pin: %i", pinNumber);

                        int num_triggered_sensors = 0;
                        // Check if at least 2 sensors are triggered
                        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
                        {
                            if (gpio_get_level(sensorPins[i]) == 0)
                            {
                                num_triggered_sensors++;
                            }
                        }

                        if (num_triggered_sensors < 2)
                        {
                            ESP_LOGW(TAG, "Not enough sensors triggered");
                            continue;
                        }

                        lastTriggerTime = pdTICKS_TO_MS(xTaskGetTickCount());

                        timeval_t current_time;
                        gettimeofday(&current_time, NULL);

                        int64_t offset;
                        taskENTER_CRITICAL(&timesync_spinlock);
                        offset = time_offset_to_controller;
                        taskEXIT_CRITICAL(&timesync_spinlock);

                        esp_cpu_cycle_count_t current_cpu_cycle = esp_cpu_get_cycle_count();
                        esp_cpu_cycle_count_t diff_cycles = current_cpu_cycle - last_trigger_cpu_cycles;

                        uint32_t latency_us = (uint32_t)((uint64_t)diff_cycles * 1000000ULL / cpu_hz);

                        int64_t timestamp = TIME_US(current_time) + offset - (int64_t)latency_us;

                        PacketTypeTrigger trigger;
                        trigger.timestamp = timestamp;

                        PacketTypeSensorState sensors_state;
                        sensors_state.num_sensors = sizeof(sensorPins) / sizeof(int);
                        sensors_state.sensor_states = 0;

                        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
                        {
                            int level = gpio_get_level(sensorPins[i]);
                            level = level == 0 ? 1 : 0; // Invert the level so that 1 means triggered
                            sensors_state.sensor_states |= ((uint64_t)level << i);
                        }
                        sensors_state.sensor_states = ~sensors_state.sensor_states;
                        trigger.sensor_state = sensors_state;

                        gettimeofday(&last_time_sent, NULL);

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
                level = level == 0 ? 1 : 0; // Invert the level so that 1 means triggered
                currentFaults += level;
            }

            if (currentFaults > 0)
            {
                isCurrentlyGood = false;
            }

            if (!faultWarning && !isCurrentlyGood)
            {
                // Currently disconnected but not in warning state -> aktivate warning state
                faultTime = pdTICKS_TO_MS(xTaskGetTickCount());
                faultWarning = true;
            }

            if (faultWarning)
            {
                if (pdTICKS_TO_MS(xTaskGetTickCount()) - faultTime > faultCooldown && !fault)
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
    gettimeofday(&last_time_sent, NULL);
    bool last_state[sizeof(sensorPins) / sizeof(int)];
    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        gettimeofday(&last_time_clean[i], NULL);
        int level = gpio_get_level(sensorPins[i]);
        level = level == 0 ? 1 : 0; // Invert the level so that 1 means triggered
        last_state[i] = level;
    }
    xQueueSend(sensorStatusQueue, &(int){0}, 0); // Send initial message to trigger status update

    while (true)
    {
        int pinNumber;
        BaseType_t newDataReceived = xQueueReceive(sensorStatusQueue, &pinNumber, pdMS_TO_TICKS(5000));
        gettimeofday(&current_time, NULL);
        // check if any of the gpio pins are high with bit mask call
        bool any_tiggered = 0;
        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
        {
            int level = gpio_get_level(sensorPins[i]);
            level = level == 0 ? 1 : 0; // Invert the level so that 1 means triggered
            any_tiggered |= level;
        }

        PacketTypeSensorState sensors_state;
        sensors_state.num_sensors = sizeof(sensorPins) / sizeof(int);
        sensors_state.sensor_states = 0;

        for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
        {

            int level = gpio_get_level(sensorPins[i]);
            level = level == 0 ? 1 : 0; // Invert the level so that 1 means triggered
            sensors_state.sensor_states |= ((uint64_t)level << i);

            if (level == 1)
            {
                gettimeofday(&last_time_clean[i], NULL);
            }

            gettimeofday(&current_time, NULL);
            // If no sensor is triggered, turn off the LED if it was previously on for 8 seconds

            if (level == 0)
            {
                if (TIME_US(current_time) - TIME_US(last_time_clean[i]) > 8000000)
                {
                    set_led(i, 0, 0, 0); // Turn off LED
                }
                else
                {
                    set_led(i, 0, 255, 0); // Set LED to green
                }
            }
            else
            {
                set_led(i, 255, 0, 0); // Set LED to red
            }

            last_state[i] = level;
        }

        if (!newDataReceived || TIME_US(current_time) - TIME_US(last_time_sent) > 4500000)
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