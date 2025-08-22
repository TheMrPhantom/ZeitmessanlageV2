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

void sendFaultInformation(bool start, bool stop)
{
    SevenSegmentDisplay faultInformation;
    faultInformation.type = SEVEN_SEGMENT_NETWORK_FAULT;
    faultInformation.startFault = start;
    faultInformation.stopFault = stop;
    xQueueSend(sevenSegmentQueue, &faultInformation, 0);
}

void Network_Fault_Task(void *params)
{

    const int timeoutTime = pdMS_TO_TICKS(7000);

    int64_t lastSeenStart = -timeoutTime;
    int64_t lastSeenStop = -timeoutTime;

    while (1)
    {
        int received = NOTHING_ALIVE;
        if (xQueueReceive(networkFaultQueue, &received, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(NETWORK_FAUT_TAG, "Received network fault message: %d", received);
            timeval_t now;
            gettimeofday(&now, NULL);
            if (received == START_ALIVE)
            {

                lastSeenStart = TIME_US(now) / 1000;
            }
            else if (received == STOP_ALIVE)
            {
                lastSeenStop = TIME_US(now) / 1000;
            }
        }

        timeval_t now;
        gettimeofday(&now, NULL);

        int currentTime = TIME_US(now) / 1000;

        bool startFault = currentTime - lastSeenStart > timeoutTime;
        bool stopFault = currentTime - lastSeenStop > timeoutTime;

        sendFaultInformation(startFault, stopFault);
    }
}