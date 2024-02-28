#include "Timer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "SevenSegment.h"

extern QueueHandle_t timerQueue;
extern QueueHandle_t networkQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t sevenSegmentQueue;

char *TIMER_TAG = "TIMER";
int timerTriggerCause;
int timerTime = 0;
bool timerIsRunning = false;

void startTimer()
{
    timerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
    timerIsRunning = true;
}

void stopTimer()
{
    timerIsRunning = false;
}

void sendTimeToNetwork(int *timeElapsed)
{
    xQueueSend(networkQueue, timeElapsed, 0);
}

void Timer_Task(void *params)
{

    while (true)
    {
        if (xQueueReceive(timerQueue, &timerTriggerCause, pdMS_TO_TICKS(10)))
        {
            ESP_LOGI(TIMER_TAG, "Received %i", timerTriggerCause);

            // The trigger was the sensor
            if (timerTriggerCause == -1)
            {
                startTimer();
                ESP_LOGI(TIMER_TAG, "Started timer");
            }
            else
            {
                int timeElapsedLocal = (int)pdTICKS_TO_MS(xTaskGetTickCount()) - timerTime;
                stopTimer();

                SevenSegmentDisplay toSend;
                toSend.type = SEVEN_SEGMENT_SET_TIME;
                toSend.time = timerTriggerCause;
                xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                ESP_LOGI(TIMER_TAG, "Stopped timer. Run was %ims (local difference: %ims)", timerTriggerCause, timeElapsedLocal - timerTriggerCause);
            }
        }

        if (timerIsRunning)
        {
            int currentTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
            SevenSegmentDisplay toSend;
            toSend.type = SEVEN_SEGMENT_SET_TIME;
            toSend.time = currentTime - timerTime;
            xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
        }
    }
}