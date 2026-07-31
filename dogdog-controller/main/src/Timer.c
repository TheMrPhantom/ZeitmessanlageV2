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
#include "HornTimer.h"

extern QueueHandle_t sevenSegmentQueue;

extern QueueHandle_t resetQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t timeQueue;
extern QueueHandle_t buzzerQueue;
extern QueueHandle_t buttonQueue;

extern QueueSetHandle_t triggerAndResetQueue;
extern char *pc_programm;

char *TIMER_TAG = "TIMER";
TimerTrigger timerTriggerCause;
int64_t timerTime = 0;
int64_t lastTriggerTime = 0;
int resetCause = 0;
bool timerIsRunning = false;
extern volatile bool sensors_active;
static int64_t last_horn_broadcast_time = 0;
static portMUX_TYPE timer_state_lock = portMUX_INITIALIZER_UNLOCKED;

void getTimerState(bool *is_running, int64_t *start_timestamp)
{
    portENTER_CRITICAL(&timer_state_lock);
    if (is_running)
    {
        *is_running = timerIsRunning;
    }
    if (start_timestamp)
    {
        *start_timestamp = timerTime;
    }
    portEXIT_CRITICAL(&timer_state_lock);
}

bool restartTimerFromLastTrigger(void)
{
    int64_t timestamp;
    portENTER_CRITICAL(&timer_state_lock);
    timestamp = lastTriggerTime;
    portEXIT_CRITICAL(&timer_state_lock);

    if (timestamp == 0)
    {
        return false;
    }

    TimerTrigger trigger = {
        .is_start = true,
        .timestamp = timestamp,
        .is_final_time = false,
        .force_restart = true,
    };
    return xQueueSend(triggerQueue, &trigger, pdMS_TO_TICKS(50)) == pdTRUE;
}

void startTimer(int64_t timestamp)
{
    portENTER_CRITICAL(&timer_state_lock);
    timerTime = timestamp;
    timerIsRunning = true;
    portEXIT_CRITICAL(&timer_state_lock);

    timeval_t current_time;
    gettimeofday(&current_time, NULL);
    int64_t elapsed_time = TIME_US(current_time) - timestamp;

    if (elapsed_time < 0)
    {
        ESP_LOGW(TIMER_TAG, "Negative elapsed time detected: %lld", elapsed_time);
        elapsed_time = 0; // Reset to zero if negative
    }

    horn_timer_broadcast_elapsed_us(elapsed_time);
    last_horn_broadcast_time = elapsed_time;

    if (sensors_active)
    {
        if (IS_SIMPLE_AGILITY_MODE || IS_THS_MODE)
        {
            // the programm is simple-agility
            // output the message: 's00024,65\n' (the time 24,65s padded to 5 digits with leading zeros)
            // time text is the time in seconds with 2 decimals and comma as decimal separator

            int64_t elapsed_time_ms = elapsed_time / 1000;

            float time_float = elapsed_time_ms / 1000.0f;

            char padded_time[10]; // 8 digits + null terminator
            // fill padded time with zeros
            snprintf(padded_time, sizeof(padded_time), "s%08.2f", time_float);
            padded_time[6] = ','; // replace decimal point with comma

            printf("%s\n", padded_time);
        }

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
    portENTER_CRITICAL(&timer_state_lock);
    timerIsRunning = false;
    portEXIT_CRITICAL(&timer_state_lock);
}

void Timer_Task(void *params)
{

    bool is_start_hurdle = false;

    while (true)
    {

        QueueHandle_t selectedQueue = xQueueSelectFromSet(triggerAndResetQueue, pdMS_TO_TICKS(10));
        if (selectedQueue != NULL)
        {
            if (selectedQueue == triggerQueue)
            {
                xQueueReceive(triggerQueue, &timerTriggerCause, 0);
                portENTER_CRITICAL(&timer_state_lock);
                lastTriggerTime = timerTriggerCause.timestamp;
                portEXIT_CRITICAL(&timer_state_lock);
                ESP_LOGI(TIMER_TAG, "Received trigger! From start? -> %d", timerTriggerCause.is_start);
                ESP_LOGI(TIMER_TAG, "Trigger is final time? -> %d", timerTriggerCause.is_final_time);

                if (timerTriggerCause.force_restart)
                {
                    startTimer(timerTriggerCause.timestamp);
                    int64_t restart_marker = -1;
                    xQueueOverwrite(timeQueue, &restart_marker);
                    ESP_LOGI(TIMER_TAG, "Restarted timer from last trigger timestamp: %" PRId64,
                             timerTriggerCause.timestamp);
                    continue;
                }

                bool timer_is_running;
                int64_t timer_start;
                getTimerState(&timer_is_running, &timer_start);
                // The trigger was the sensor
                // Time not running
                if (!timer_is_running)
                {
                    if (!IS_THS_MODE || timerTriggerCause.is_start) // Not THS mode or dedicated start trigger
                    {
                        is_start_hurdle = timerTriggerCause.is_start;

                        startTimer(timerTriggerCause.timestamp);
                        int64_t start_marker = -1;
                        xQueueOverwrite(timeQueue, &start_marker);
                        ESP_LOGI(TIMER_TAG, "Started timer");
                    }
                    else if (IS_THS_MODE && !timerTriggerCause.is_start && timerTriggerCause.is_final_time)
                    {
                        int64_t timeElapsedLocal = (timerTriggerCause.timestamp - timer_start) / 1000;
                        if (timeElapsedLocal < 0)
                        {
                            ESP_LOGW(TIMER_TAG, "Negative elapsed time detected: %lld", timeElapsedLocal);
                            timeElapsedLocal = 0; // Reset to zero if negative
                        }
                        stopTimer();

                        xQueueOverwrite(timeQueue, &timeElapsedLocal);
                        ESP_LOGI(TIMER_TAG, "Timer stopped. Elapsed time: %lld ms", timeElapsedLocal);

                        SevenSegmentDisplay toSend;
                        toSend.type = SEVEN_SEGMENT_STORE_TO_HISTORY;
                        toSend.time = timeElapsedLocal;
                        xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));

                        glow_state_t glow_state;
                        glow_state.state = 0;
                        glow_state.pinNumber = BUTTON_GLOW_GPIO_TYPE_RESET;
                        xQueueSend(buttonQueue, &glow_state, pdMS_TO_TICKS(50));
                    }
                }
                else if (timer_is_running && (!IS_THS_MODE || !timerTriggerCause.is_start)) // In THS mode we only stop on the dedicated stop trigger
                {
                    int64_t timeElapsedLocal = (timerTriggerCause.timestamp - timer_start) / 1000;
                    if (timeElapsedLocal < 0)
                    {
                        ESP_LOGW(TIMER_TAG, "Negative elapsed time detected: %lld", timeElapsedLocal);
                        timeElapsedLocal = 0; // Reset to zero if negative
                    }
                    stopTimer();

                    xQueueOverwrite(timeQueue, &timeElapsedLocal);
                    ESP_LOGI(TIMER_TAG, "Timer stopped. Elapsed time: %lld ms", timeElapsedLocal);

                    if (!IS_THS_MODE)
                    {
                        SevenSegmentDisplay toSend;
                        toSend.type = SEVEN_SEGMENT_STORE_TO_HISTORY;
                        toSend.time = timeElapsedLocal;
                        xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                    }
                    else
                    {
                        SevenSegmentDisplay toSend;
                        toSend.type = SEVEN_SEGMENT_TEMP_TIME;
                        toSend.time = timeElapsedLocal;
                        xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                    }
                    ESP_LOGI(TIMER_TAG, "Stopped timer. Run was %" PRId64 "ms", timeElapsedLocal);
                }
                else if (timer_is_running && timerTriggerCause.is_start == is_start_hurdle)
                {
                    // If we reach this point, it means the timer was running and we received a conflicting start/stop signal
                    ESP_LOGW(TIMER_TAG, "Conflicting timer signal received");
                }
            }
            else if (selectedQueue == resetQueue)
            {
                xQueueReceive(resetQueue, &resetCause, 0);
                stopTimer();
                horn_timer_broadcast_reset();

                int64_t reset_marker = -2;
                xQueueOverwrite(timeQueue, &reset_marker);

                SevenSegmentDisplay toSend;
                toSend.type = SEVEN_SEGMENT_SET_TIME;
                toSend.time = 0;
                xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
                ESP_LOGI(TIMER_TAG, "Reset timer");
            }
        }

        bool timer_is_running;
        int64_t timer_start;
        getTimerState(&timer_is_running, &timer_start);
        if (timer_is_running)
        {
            timeval_t current_time;
            gettimeofday(&current_time, NULL);
            int64_t elapsed_time = TIME_US(current_time) - timer_start;

            if (elapsed_time < 0)
            {
                ESP_LOGW(TIMER_TAG, "Negative elapsed time detected: %lld", elapsed_time);
                elapsed_time = 0; // Reset to zero if negative
            }

            if (elapsed_time - last_horn_broadcast_time >= 250000)
            {
                horn_timer_broadcast_elapsed_us(elapsed_time);
                last_horn_broadcast_time = elapsed_time;
            }

            SevenSegmentDisplay toSend;
            toSend.type = SEVEN_SEGMENT_SET_TIME;
            toSend.time = elapsed_time / 1000;
            xQueueSend(sevenSegmentQueue, &toSend, pdMS_TO_TICKS(500));
        }
    }
}
