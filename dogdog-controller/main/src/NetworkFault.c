#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>
#include "NetworkFault.h"
#include <esp_system.h>
#include "SevenSegment.h"
#include "Clock.h"
#include "esp_timer.h"

extern QueueHandle_t networkFaultQueue;
extern QueueHandle_t sevenSegmentQueue;

const char *NETWORK_FAUT_TAG = "NETWORK_FAULT";

bool sendFaultInformation(int start, int stop)
{
    SevenSegmentDisplay faultInformation;
    faultInformation.type = SEVEN_SEGMENT_NETWORK_FAULT;
    faultInformation.startFault = start;
    faultInformation.stopFault = stop;
    return xQueueSend(sevenSegmentQueue, &faultInformation, 0) == pdTRUE;
}

int last_start_state = 2;
int last_stop_state = 2;

void Network_Fault_Task(void *params)
{

    /* Use milliseconds for timeout to match TIME_US(now) / 1000 (which yields ms).
        pdMS_TO_TICKS converts ms to RTOS ticks — mixing those with raw ms values
        caused intermittent false faults. Keep timeoutMs in milliseconds and
        compare ms against ms. */
    const int64_t timeoutMs = 8000;

    int64_t lastSeenStart = -timeoutMs;
    int64_t lastSeenStop = -timeoutMs;
    int last_sent_start = -1;
    int last_sent_stop = -1;

    while (1)
    {
        StationConnectivityStatus received;
        received.station = NOTHING_ALIVE;
        if (xQueueReceive(networkFaultQueue, &received, pdMS_TO_TICKS(500)))
        {
            if (received.station == START_ALIVE)
            {
                lastSeenStart = esp_timer_get_time() / 1000;
            }
            else if (received.station == STOP_ALIVE)
            {
                lastSeenStop = esp_timer_get_time() / 1000;
            }
        }

        /* Connectivity timeouts are durations and must not depend on wall-clock
           corrections made by the RTC/time-sync code. */
        int64_t currentTime = esp_timer_get_time() / 1000;

        if (received.station != NOTHING_ALIVE)
        {
            if (received.station == START_ALIVE)
            {
                last_start_state = received.signal;
            }
            else if (received.station == STOP_ALIVE)
            {
                last_stop_state = received.signal;
            }
        }

        bool startFault = (currentTime - lastSeenStart) > timeoutMs;
        bool stopFault = (currentTime - lastSeenStop) > timeoutMs;

        int to_send_for_start = startFault ? 2 : last_start_state;
        int to_send_for_stop = stopFault ? 2 : last_stop_state;

        if (to_send_for_start != last_sent_start || to_send_for_stop != last_sent_stop)
        {
            if (sendFaultInformation(to_send_for_start, to_send_for_stop))
            {
                last_sent_start = to_send_for_start;
                last_sent_stop = to_send_for_stop;
            }
        }
    }
}
