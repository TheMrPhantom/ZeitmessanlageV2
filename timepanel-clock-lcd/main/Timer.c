#include "Timer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "SevenSegment.h"
#include "KeyValue.h"
#include "Buzzer.h"

extern QueueHandle_t sevenSegmentQueue;

extern QueueHandle_t resetQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t timeQueue;
extern QueueHandle_t buzzerQueue;

extern QueueSetHandle_t triggerAndResetQueue;

char *TIMER_TAG = "TIMER";
int timerTriggerCause;
int timerTime = 0;
bool timerIsRunning = false;
int lastTrigger = -1; // -1 means no trigger
int lastTriggerManager = -1;
void startTimer()
{
    timerTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
    timerIsRunning = true;
    xQueueSend(buzzerQueue, &(int){BUZZER_TRIGGER}, 0);
    increaseKey("triggers");
}

void stopTimer()
{
    xQueueSend(buzzerQueue, &(int){BUZZER_TRIGGER}, 0);
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
                ESP_LOGI(TIMER_TAG, "Received %i %i", timerTriggerCause, lastTriggerManager);

                // The trigger was the sensor
                if (!timerIsRunning && (timerTriggerCause == lastTriggerManager || lastTriggerManager == -1))
                {
                    startTimer();
                    lastTrigger = timerTriggerCause;
                    lastTriggerManager = timerTriggerCause;
                    int x = -1;
                    xQueueSend(timeQueue, &x, 0);
                    ESP_LOGI(TIMER_TAG, "Started timer");
                }
                else
                {
                    if (timerTriggerCause != lastTriggerManager)
                    {
                        int timeElapsedLocal = (int)pdTICKS_TO_MS(xTaskGetTickCount()) - timerTime;
                        stopTimer();

                        xQueueSend(timeQueue, &timeElapsedLocal, 0);

                        SevenSegmentDisplay toSend;
                        toSend.type = SEVEN_SEGMENT_SET_TIME;
                        toSend.time = timeElapsedLocal;
                        toSend.triggerStation = timerTriggerCause;
                        xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                        ESP_LOGI(TIMER_TAG, "Stopped timer. Run was %ims", timeElapsedLocal);
                    }
                    else
                    {
                        ESP_LOGW(TIMER_TAG, "Ignoring trigger from same station %i", timerTriggerCause);
                    }
                }
            }
            else if (selectedQueue == resetQueue)
            {
                xQueueReceive(resetQueue, &timerTriggerCause, 0);
                stopTimer();

                int x = -2;
                xQueueSend(timeQueue, &x, 0);

                SevenSegmentDisplay toSend;
                toSend.type = SEVEN_SEGMENT_SET_TIME;
                toSend.time = 0;
                toSend.triggerStation = -1;
                xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                ESP_LOGI(TIMER_TAG, "Reset timer");
            }
        }

        if (timerIsRunning)
        {
            int currentTime = (int)pdTICKS_TO_MS(xTaskGetTickCount());
            SevenSegmentDisplay toSend;
            toSend.type = SEVEN_SEGMENT_SET_TIME;
            toSend.time = currentTime - timerTime;
            toSend.triggerStation = lastTrigger;
            xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
            lastTrigger = -1; // Reset last trigger after sending it to the display
        }
    }
}