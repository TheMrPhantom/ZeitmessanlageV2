#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
#include "KeyValue.h"
#include "NetworkFault.h"

extern QueueHandle_t sensorInterruptQueue;
extern QueueHandle_t buzzerQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t networkFaultQueue;
QueueHandle_t sensorStatusQueue;
extern QueueHandle_t sevenSegmentQueue;

char *TAG = "SENSOR";
const int sensorPins[] = {TRIGGER_PIN_1, TRIGGER_PIN_2};
const int sensorCooldown = 2000;
const int faultCooldown = 3000;

TickType_t faultTime = 0;
bool faultWarning = false;
bool fault = false;

timeval_t last_start_trigger_time;
timeval_t last_sensor_stop_time;

uint32_t cpu_hz = 1;
static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    SensorTriggerEvent event = {
        .pin_number = (int)(intptr_t)args,
        .cpu_cycles = esp_cpu_get_cycle_count(),
    };
    BaseType_t higher_priority_task_woken = pdFALSE;

    // read pin state
    int pinState = gpio_get_level(event.pin_number);
    if (pinState == 1)
    {
        xQueueSendFromISR(sensorInterruptQueue, &event, &higher_priority_task_woken);
    }
    /* Only the latest changed pin is useful to the status task.  Overwrite the
       one-slot queue so contact bounce cannot leave a stale notification. */
    xQueueOverwriteFromISR(sensorStatusQueue, &event.pin_number, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

esp_err_t init_Sensor_Pins(void)
{
    cpu_hz = (uint32_t)esp_clk_cpu_freq();
    if (cpu_hz == 0)
    {
        ESP_LOGE(TAG, "CPU frequency unavailable; latency correction disabled");
    }

    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        ESP_LOGI(TAG, "Configuring IO Pin %i", sensorPins[i]);
        esp_err_t err = gpio_reset_pin(sensorPins[i]);
        if (err == ESP_OK)
            err = gpio_set_direction(sensorPins[i], GPIO_MODE_INPUT);
        if (err == ESP_OK)
            err = gpio_pulldown_dis(sensorPins[i]);
        if (err == ESP_OK)
            err = gpio_pullup_dis(sensorPins[i]);
        if (err == ESP_OK)
            err = gpio_set_intr_type(sensorPins[i], GPIO_INTR_ANYEDGE);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure sensor pin %d: %s",
                     sensorPins[i], esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "Done configuring IO");

    for (int i = 0; i < sizeof(sensorPins) / sizeof(int); i++)
    {
        ESP_LOGI(TAG, "Configuring ISR for Pin %i", sensorPins[i]);
        esp_err_t err = gpio_isr_handler_add(sensorPins[i], gpio_interrupt_handler,
                                             (void *)(intptr_t)sensorPins[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to install sensor ISR for pin %d: %s",
                     sensorPins[i], esp_err_to_name(err));
            for (int installed = 0; installed < i; installed++)
            {
                ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_isr_handler_remove(sensorPins[installed]));
            }
            for (int pin = 0; pin < sizeof(sensorPins) / sizeof(int); pin++)
            {
                ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_intr_type(sensorPins[pin], GPIO_INTR_DISABLE));
            }
            return err;
        }
    }

    ESP_LOGI(TAG, "Done configuring ISR");
    return ESP_OK;
}

void Sensor_Interrupt_Task(void *params)
{

    ESP_LOGI(TAG, "Setting up Sensors");
    gettimeofday(&last_start_trigger_time, NULL);
    gettimeofday(&last_sensor_stop_time, NULL);

    /* app_main creates this task only for a compile-time cable-controller
       build.  Do not let a stale NVS role value disable its sensors. */
    ESP_LOGI(TAG, "Controller is cable based: Starting Sensor Interrupt Task");

    /* This queue must exist before the GPIO ISR is installed. */
    sensorStatusQueue = xQueueCreate(1, sizeof(int));
    if (!sensorStatusQueue)
    {
        ESP_LOGE(TAG, "Failed to create sensor status queue");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t sensor_init_err = init_Sensor_Pins();
    if (sensor_init_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Sensor initialization failed: %s", esp_err_to_name(sensor_init_err));
        vQueueDelete(sensorStatusQueue);
        sensorStatusQueue = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (xTaskCreate(Sensor_Status_Task, "Sensor_Status_Task", 4048, NULL, 1, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create sensor status task");
    }

    SensorTriggerEvent trigger_event = {0};
    TickType_t sensor_started_tick = xTaskGetTickCount();
    TickType_t last_trigger_tick = sensor_started_tick - pdMS_TO_TICKS(sensorCooldown);

    while (true)
    {
        if (xQueueReceive(sensorInterruptQueue, &trigger_event, pdMS_TO_TICKS(500)))
        {
            int pinNumber = trigger_event.pin_number;
            ESP_LOGI(TAG, "Checking interrupt of Pin: %i", pinNumber);

            if (gpio_get_level(pinNumber) == 1)
            {
                ESP_LOGI(TAG, "Confirmed interrupt of Pin: %i", pinNumber);
                TickType_t now_tick = xTaskGetTickCount();
                if ((now_tick - last_trigger_tick) >= pdMS_TO_TICKS(sensorCooldown))
                {
                    if (!fault)
                    {

                        last_trigger_tick = now_tick;
                        ESP_LOGI(TAG, "Interrupt of Pin: %i", pinNumber);
                        TimerTrigger timerTriggerCause;

                        timeval_t current_time;
                        gettimeofday(&current_time, NULL);

                        esp_cpu_cycle_count_t current_cpu_cycle = esp_cpu_get_cycle_count();
                        esp_cpu_cycle_count_t diff_cycles = current_cpu_cycle - trigger_event.cpu_cycles;

                        uint32_t latency_us = cpu_hz == 0
                                                  ? 0
                                                  : (uint32_t)((uint64_t)diff_cycles * 1000000ULL / cpu_hz);
                        int64_t adjusted_time_us = TIME_US(current_time) - (int64_t)latency_us;

                        ESP_LOGI(TAG, "Latency for Pin %i: %" PRIu32 "us", pinNumber, latency_us);

                        timerTriggerCause.timestamp = adjusted_time_us;
                        timerTriggerCause.is_start = pinNumber == TRIGGER_PIN_1 ? true : false;
                        timerTriggerCause.is_final_time = false;
                        timerTriggerCause.force_restart = false;
                        if (xQueueSend(triggerQueue, &timerTriggerCause, 0) != pdTRUE)
                        {
                            ESP_LOGW(TAG, "Timer trigger queue full; dropping trigger on pin %i", pinNumber);
                        }

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
        if ((xTaskGetTickCount() - sensor_started_tick) > pdMS_TO_TICKS(4000))
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
                if ((xTaskGetTickCount() - faultTime) > pdMS_TO_TICKS(faultCooldown) && !fault)
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
        sendSensorStatus(pinNumber, TRIGGER_PIN_1);
        sendSensorStatus(pinNumber, TRIGGER_PIN_2);

        pinNumber = -1;
        xQueueReceive(sensorStatusQueue, &pinNumber, pdMS_TO_TICKS(1000));
    }
}

void sendSensorStatus(int triggeredPin, int pinToCheck)
{
    int sensor_amount = 4;
#ifdef CONFIG_SENSOR_AMOUNT
    sensor_amount = CONFIG_SENSOR_AMOUNT;
#endif
    if (sensor_amount < 0)
    {
        sensor_amount = 0;
    }
    if (sensor_amount > DISPLAY_MAX_SENSOR_COUNT)
    {
        ESP_LOGW(TAG, "Capping configured sensor count %d to display limit %d",
                 sensor_amount, DISPLAY_MAX_SENSOR_COUNT);
        sensor_amount = DISPLAY_MAX_SENSOR_COUNT;
    }
    int pinState = gpio_get_level(pinToCheck);
    SevenSegmentDisplay toDisplay = {0};
    toDisplay.type = SEVEN_SEGMENT_SENSOR_STATUS;
    toDisplay.sensorStatus.sensor = pinToCheck == TRIGGER_PIN_1 ? SENSOR_START : SENSOR_STOP;
    toDisplay.sensorStatus.num_sensors = sensor_amount;
    toDisplay.sensorStatus.is_trigger = triggeredPin == pinToCheck ? true : false;
    for (int i = 0; i < toDisplay.sensorStatus.num_sensors; i++)
    {
        toDisplay.sensorStatus.status[i] = pinState == 1 || toDisplay.sensorStatus.is_trigger ? false : true;
    }
    if (xQueueSend(sevenSegmentQueue, &toDisplay, 0) != pdTRUE)
    {
        ESP_LOGW(TAG, "Display queue full; dropping sensor status");
    }

    StationConnectivityStatus status;
    status.station = pinToCheck == TRIGGER_PIN_1 ? START_ALIVE : STOP_ALIVE;
    status.signal = 0;
    if (xQueueSend(networkFaultQueue, &status, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Network status queue full; dropping cable status");
    }
}
