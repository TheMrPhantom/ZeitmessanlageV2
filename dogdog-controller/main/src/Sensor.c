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
#include "GPIOPins.h"

extern QueueHandle_t sensorInterruptQueue;
extern QueueHandle_t buzzerQueue;
extern QueueHandle_t triggerQueue;
QueueHandle_t sensorStatusQueue;
extern QueueHandle_t sevenSegmentQueue;

char *TAG = "SENSOR";
const int sensorPins[] = {TRIGGER_PIN_1, TRIGGER_PIN_2};
const int sensorCooldown = 2000;
const int faultCooldown = 3000;

int faultTime = 0;
bool faultWarning = false;
bool fault = false;

timeval_t last_start_trigger_time;
timeval_t last_sensor_stop_time;

uint32_t cpu_hz = 1;
esp_cpu_cycle_count_t last_trigger_cpu_cycles;

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    esp_cpu_cycle_count_t triggered_at = esp_cpu_get_cycle_count();
    int pinNumber = (int)args;
    // read pin state
    int pinState = gpio_get_level(pinNumber);
    if (pinState == 1)
    {
        xQueueSendFromISR(sensorInterruptQueue, &pinNumber, NULL);

        last_trigger_cpu_cycles = triggered_at;
    }
    xQueueSendFromISR(sensorStatusQueue, &pinNumber, NULL);
}

void init_Sensor_Pins()
{
    cpu_hz = (uint32_t)esp_clk_cpu_freq();

    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        ESP_LOGI(TAG, "Configuring IO Pin %i", sensorPins[i]);
        esp_rom_gpio_pad_select_gpio(sensorPins[i]);
        gpio_set_direction(sensorPins[i], GPIO_MODE_INPUT);
        gpio_pulldown_dis(sensorPins[i]);
        gpio_pullup_en(sensorPins[i]);
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
    gettimeofday(&last_start_trigger_time, NULL);
    gettimeofday(&last_sensor_stop_time, NULL);
    init_Sensor_Pins();
    sensorStatusQueue = xQueueCreate(1, sizeof(char *));
    xTaskCreate(Sensor_Status_Task, "Sensor_Status_Task", 4048, NULL, 1, NULL);
    int numPins = sizeof(sensorPins) / sizeof(int);
    // xTaskCreate(LED_Task, "LED_Task", 4048, &numPins, 1, NULL);
    //  Wait for led

    int pinNumber = 0;
    int lastTriggerTime = 0;

    while (true)
    {
        if (xQueueReceive(sensorInterruptQueue, &pinNumber, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i", pinNumber);

            if (gpio_get_level(pinNumber) == 1)
            {
                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", pinNumber);
                if (lastTriggerTime < (int)pdTICKS_TO_MS(xTaskGetTickCount()) - sensorCooldown)
                {
                    if (!fault)
                    {

                        lastTriggerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
                        ESP_LOGI(TAG, "Interrupt of Pin: %i", pinNumber);
                        TimerTrigger timerTriggerCause;

                        timeval_t current_time;
                        gettimeofday(&current_time, NULL);

                        esp_cpu_cycle_count_t current_cpu_cycle = esp_cpu_get_cycle_count();
                        esp_cpu_cycle_count_t diff_cycles = current_cpu_cycle - last_trigger_cpu_cycles;
                        
                        uint32_t latency_us = (uint32_t)((uint64_t)diff_cycles * 1000000ULL / cpu_hz);
                        int64_t adjusted_time_us = TIME_US(current_time) - (int64_t)latency_us;
                        
                        ESP_LOGI(TAG, "Latency for Pin %i: %uus", pinNumber, latency_us);

                        timerTriggerCause.timestamp = adjusted_time_us;
                        timerTriggerCause.is_start = pinNumber == TRIGGER_PIN_1 ? true : false;
                        xQueueSend(triggerQueue, &timerTriggerCause, 0);

                        if (pinNumber == TRIGGER_PIN_1)
                        {
                            last_start_trigger_time = current_time;
                        }
                        else if (pinNumber == TRIGGER_PIN_2)
                        {
                            last_sensor_stop_time = current_time;
                        }
                        // int cause = BUZZER_TRIGGER;
                        // xQueueSend(buzzerQueue, &cause, 0);
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

                    // int cause = Buzzer_INDICATE_ERROR;
                    // xQueueSend(buzzerQueue, &cause, 0);
                    // xQueueSend(faultQueue, &cause, 0);

                    ESP_LOGI(TAG, "Sensor connection is lost");
                }
            }

            if (isCurrentlyGood)
            {
                if (fault)
                {
                    // No more fault
                    // int cause = Buzzer_INDICATE_ERROR;
                    // xQueueSend(buzzerQueue, &cause, 0);
                    // xQueueSend(faultQueue, &cause, 0);
                    ESP_LOGI(TAG, "Sensor connection restored");
                }
                faultWarning = false;
                fault = false;
            }
        }
    }
}

void Sensor_Status_Task(void *params)
{
    ESP_LOGI(TAG, "Starting Sensor Status Task");
    int pinNumber = -1;

    while (true)
    {
        timeval_t now;
        gettimeofday(&now, NULL);

        sendSensorStatus(pinNumber, TRIGGER_PIN_1);
        sendSensorStatus(pinNumber, TRIGGER_PIN_2);

        pinNumber = -1;
        xQueueReceive(sensorStatusQueue, &pinNumber, pdMS_TO_TICKS(1000));
    }
}

void sendSensorStatus(int triggeredPin, int pinToCheck)
{
    int pinState = gpio_get_level(pinToCheck);
    SevenSegmentDisplay toDisplay;
    toDisplay.type = SEVEN_SEGMENT_SENSOR_STATUS;
    toDisplay.sensorStatus.sensor = pinToCheck == TRIGGER_PIN_1 ? SENSOR_START : SENSOR_STOP;
    toDisplay.sensorStatus.num_sensors = 5;
    toDisplay.sensorStatus.is_trigger = triggeredPin == pinToCheck ? true : false;
    toDisplay.sensorStatus.status = malloc(sizeof(bool) * toDisplay.sensorStatus.num_sensors);
    // check malloc result
    if (toDisplay.sensorStatus.status == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for sensor status");

        return;
    }
    for (int i = 0; i < toDisplay.sensorStatus.num_sensors; i++)
    {
        toDisplay.sensorStatus.status[i] = pinState == 1 || toDisplay.sensorStatus.is_trigger ? false : true;
    }
    xQueueSend(sevenSegmentQueue, &toDisplay, 0);
}