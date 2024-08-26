#include "Timer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "Display.h"
#include "KeyValue.h"

extern QueueHandle_t displayQueue;

extern QueueHandle_t resetQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t timeQueue;

extern QueueSetHandle_t triggerAndResetQueue;

char *TIMER_TAG = "TIMER";
int timerTriggerCause;
int timerTime = 0;
bool timerIsRunning = false;

void startTimer()
{
    timerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
    timerIsRunning = true;
    increaseKey("triggers");
}

void stopTimer()
{
    timerIsRunning = false;
}

void Timer_Task(void *params)
{

    while (true)
    {
        QueueHandle_t selectedQueue = xQueueSelectFromSet(triggerAndResetQueue, pdMS_TO_TICKS(10));
        if (selectedQueue != NULL)
        {
            if (selectedQueue == triggerQueue)
            {
                xQueueReceive(triggerQueue, &timerTriggerCause, 0);
                ESP_LOGI(TIMER_TAG, "Received %i", timerTriggerCause);

                // The trigger was the sensor
                if (!timerIsRunning)
                {
                    startTimer();
                    int x = -1;
                    xQueueSend(timeQueue, &x, 0);
                    ESP_LOGI(TIMER_TAG, "Started timer");
                }
                else
                {
                    int timeElapsedLocal = (int)pdTICKS_TO_MS(xTaskGetTickCount()) - timerTime;
                    stopTimer();

                    xQueueSend(timeQueue, &timeElapsedLocal, 0);

                    LCDDisplay toSend;
                    toSend.type = SEVEN_SEGMENT_SET_TIME;
                    toSend.time = timeElapsedLocal;

                    if (xQueueSend(displayQueue, &toSend, pdMS_TO_TICKS(500)) != pdTRUE)
                    {
                        xQueueOverwrite(displayQueue, &toSend);
                    }
                    ESP_LOGI(TIMER_TAG, "Stopped timer. Run was %ims", timeElapsedLocal);
                }
            }
            else if (selectedQueue == resetQueue)
            {
                xQueueReceive(resetQueue, &timerTriggerCause, 0);
                stopTimer();

                int x = -2;
                xQueueSend(timeQueue, &x, 0);

                LCDDisplay toSend;
                toSend.type = SEVEN_SEGMENT_SET_TIME;
                toSend.time = 0;
                if (xQueueSend(displayQueue, &toSend, pdMS_TO_TICKS(500)) != pdTRUE)
                {
                    xQueueOverwrite(displayQueue, &toSend);
                }
                ESP_LOGI(TIMER_TAG, "Reset timer");
            }
        }

        if (timerIsRunning)
        {
            int currentTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
            LCDDisplay toSend;
            toSend.type = SEVEN_SEGMENT_SET_TIME;
            toSend.time = currentTime - timerTime;
            if (xQueueSend(displayQueue, &toSend, pdMS_TO_TICKS(500)) != pdTRUE)
            {
                xQueueOverwrite(displayQueue, &toSend);
            }
        }
    }
}