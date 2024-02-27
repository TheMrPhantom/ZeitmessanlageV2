#include "Timer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

extern QueueHandle_t timerQueue;
extern QueueHandle_t networkQueue;
extern QueueHandle_t resetQueue;

char *TIMER_TAG = "TIMER";
int timerTriggerCause;
int timerTime = 0;
bool timerIsRunning = false;

void startTimer()
{
    timerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
    timerIsRunning = true;
}

void stopTimer(int *timeElapsed)
{
    int currentTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
    *timeElapsed = currentTime - timerTime;
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
        if (xQueueReceive(timerQueue, &timerTriggerCause, portMAX_DELAY))
        {
            ESP_LOGI(TIMER_TAG, "Received %i", timerTriggerCause);
            if (timerTriggerCause == 0)
            {
                // The trigger was the sensor
                if (!timerIsRunning)
                {
                    startTimer();
                    ESP_LOGI(TIMER_TAG, "Started timer");
                }
                else
                {
                    int timeElapsed = 0;
                    stopTimer(&timeElapsed);
                    ESP_LOGI(TIMER_TAG, "Stopped timer. Run was %ims", timeElapsed);
                    sendTimeToNetwork(&timeElapsed);
                }
            }
            else if (timerTriggerCause == 1)
            {
                // The trigger was the other station
                if (timerIsRunning)
                {
                    int timeElapsed = 0;
                    stopTimer(&timeElapsed);
                    ESP_LOGI(TIMER_TAG, "Other station stopped timer. Run was %ims", timeElapsed);
                    sendTimeToNetwork(&timeElapsed);
                }
                else
                {
                    ESP_LOGW(TIMER_TAG, "Other station tried to stop timer, but timer not started");
                }
            }
            else if (timerTriggerCause == 2)
            {
                // The trigger was remote
                timerIsRunning = false;
                ESP_LOGI(TIMER_TAG, "Remote reseted timer");
                int toSend = 1;
                xQueueSend(resetQueue, &toSend, 0);
            }
        }
    }
}