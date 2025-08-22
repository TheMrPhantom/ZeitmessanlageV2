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

extern QueueHandle_t networkFaultQueue;
extern QueueHandle_t sevenSegmentQueue;

const char *NETWORK_FAUT_TAG = "NETWORK_FAULT";

void sendFaultInformation(int start, int stop)
{
    SevenSegmentDisplay faultInformation;
    faultInformation.type = SEVEN_SEGMENT_NETWORK_FAULT;
    faultInformation.startFault = start;
    faultInformation.stopFault = stop;
    xQueueSend(sevenSegmentQueue, &faultInformation, 0);
}

int last_start_state = 2;
int last_stop_state = 2;

void Network_Fault_Task(void *params)
{

    const int timeoutTime = pdMS_TO_TICKS(7000);

    int64_t lastSeenStart = -timeoutTime;
    int64_t lastSeenStop = -timeoutTime;

    while (1)
    {
        StationConnectivityStatus received;
        received.station = NOTHING_ALIVE;
        if (xQueueReceive(networkFaultQueue, &received, pdMS_TO_TICKS(500)))
        {
            timeval_t now;
            gettimeofday(&now, NULL);
            if (received.station == START_ALIVE)
            {

                lastSeenStart = TIME_US(now) / 1000;
            }
            else if (received.station == STOP_ALIVE)
            {
                lastSeenStop = TIME_US(now) / 1000;
            }
        }

        timeval_t now;
        gettimeofday(&now, NULL);

        int currentTime = TIME_US(now) / 1000;

        bool startFault = currentTime - lastSeenStart > timeoutTime;
        bool stopFault = currentTime - lastSeenStop > timeoutTime;

        int to_send_for_start = startFault ? 2 : last_start_state;
        int to_send_for_stop = stopFault ? 2 : last_stop_state;

        if (received.station != NOTHING_ALIVE)
        {
            if (received.station == START_ALIVE)
            {
                to_send_for_start = received.signal;
                to_send_for_stop = last_stop_state;
                last_start_state = received.signal;
            }
            else if (received.station == STOP_ALIVE)
            {
                to_send_for_stop = received.signal;
                to_send_for_start = last_start_state;
                last_stop_state = received.signal;
            }
        }

        sendFaultInformation(to_send_for_start, to_send_for_stop);
    }
}