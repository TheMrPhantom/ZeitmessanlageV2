#include "Timer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "SevenSegment.h"
#include "KeyValue.h"
#include "Buzzer.h"
#include "Button.h"
#include "GPIOPins.h"

extern QueueHandle_t sevenSegmentQueue;

extern QueueHandle_t resetQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t timeQueue;
extern QueueHandle_t buzzerQueue;
extern QueueHandle_t buttonQueue;

extern QueueSetHandle_t triggerAndResetQueue;

char *TIMER_TAG = "TIMER";
TimerTrigger timerTriggerCause;
int64_t timerTime = 0;
int resetCause = 0;
bool timerIsRunning = false;
extern bool sensors_active;

void startTimer(int64_t timestamp)
{
    timerTime = timestamp;
    timerIsRunning = true;

    if (sensors_active)
    {
        xQueueSend(buzzerQueue, &(int){BUZZER_TRIGGER}, 0);
    }

    glow_state_t glow_state;
    glow_state.state = 1;
    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_RESET;
    xQueueSend(buttonQueue, &glow_state, 0);

    SevenSegmentDisplay toSend;
    toSend.type = SEVEN_SEGMENT_RESET_FAULT_REFUSAL;
    xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
    increaseKey("triggers");
}

void stopTimer()
{
    if (sensors_active)
    {
        xQueueSend(buzzerQueue, &(int){BUZZER_TRIGGER}, 0);
    }

    glow_state_t glow_state;
    glow_state.state = 0;
    glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_RESET;
    xQueueSend(buttonQueue, &glow_state, 0);
    timerIsRunning = false;
}

void Timer_Task(void *params)
{

    while (true)
    {
        bool start_hurdle = false;

        QueueHandle_t selectedQueue = xQueueSelectFromSet(triggerAndResetQueue, pdMS_TO_TICKS(10));
        if (selectedQueue != NULL)
        {
            if (selectedQueue == triggerQueue)
            {
                xQueueReceive(triggerQueue, &timerTriggerCause, 0);
                ESP_LOGI(TIMER_TAG, "Received trigger! From start? -> %d", timerTriggerCause.is_start);

                // The trigger was the sensor
                if (!timerIsRunning)
                {
                    start_hurdle = timerTriggerCause.is_start;
                    startTimer(timerTriggerCause.timestamp);
                    int x = -1;
                    xQueueSend(timeQueue, &x, 0);
                    ESP_LOGI(TIMER_TAG, "Started timer");
                }
                else if (timerIsRunning && timerTriggerCause.is_start != start_hurdle)
                {
                    int timeElapsedLocal = timerTriggerCause.timestamp - timerTime;
                    stopTimer();

                    xQueueSend(timeQueue, &timeElapsedLocal, 0);

                    SevenSegmentDisplay toSend;
                    toSend.type = SEVEN_SEGMENT_STORE_TO_HISTORY;
                    toSend.time = timeElapsedLocal;
                    xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                    ESP_LOGI(TIMER_TAG, "Stopped timer. Run was %ims", timeElapsedLocal);
                }
            }
            else if (selectedQueue == resetQueue)
            {
                xQueueReceive(resetQueue, &resetCause, 0);
                stopTimer();

                int x = -2;
                xQueueSend(timeQueue, &x, 0);

                SevenSegmentDisplay toSend;
                toSend.type = SEVEN_SEGMENT_SET_TIME;
                toSend.time = 0;
                xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                ESP_LOGI(TIMER_TAG, "Reset timer");
            }
        }

        if (timerIsRunning)
        {
            timeval_t current_time;
            gettimeofday(&current_time, NULL);
            int64_t elapsed_time = TIME_US(current_time) - timerTime;

            if (elapsed_time < 0)
            {
                ESP_LOGW(TIMER_TAG, "Negative elapsed time detected: %lld", elapsed_time);
                elapsed_time = 0; // Reset to zero if negative
            }

            SevenSegmentDisplay toSend;
            toSend.type = SEVEN_SEGMENT_SET_TIME;
            toSend.time = elapsed_time / 1000;
            xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
        }
    }
}