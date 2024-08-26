#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>
#include "NetworkFault.h"
#include <esp_system.h>
#include "Display.h"

extern QueueHandle_t networkFaultQueue;
extern QueueHandle_t displayQueue;

const char *NETWORK_FAUT_TAG = "NETWORK_FAULT";

void sendFaultInformation(bool start, bool stop)
{
    LCDDisplay faultInformation;
    faultInformation.type = SEVEN_SEGMENT_NETWORK_FAULT;
    faultInformation.startFault = start;
    faultInformation.stopFault = stop;
    if (xQueueSend(displayQueue, &faultInformation, 0) != pdTRUE)
    {
        xQueueOverwrite(displayQueue, &faultInformation);
    }
}

void Network_Fault_Task(void *params)
{

    const int timeoutTime = pdMS_TO_TICKS(7000);

    int lastSeenStart = -timeoutTime;
    int lastSeenStop = -timeoutTime;

    bool fault = true;

    while (1)
    {
        int received = NOTHING_ALIVE;
        if (xQueueReceive(networkFaultQueue, &received, pdMS_TO_TICKS(100)))
        {
            if (received == START_ALIVE)
            {
                lastSeenStart = xTaskGetTickCount();
            }
            else if (received == STOP_ALIVE)
            {
                lastSeenStop = xTaskGetTickCount();
            }
        }

        int currentTime = xTaskGetTickCount();

        bool startFault = currentTime - lastSeenStart > timeoutTime;
        bool stopFault = currentTime - lastSeenStop > timeoutTime;

        if (startFault || stopFault)
        {
            fault = true;
            sendFaultInformation(startFault, stopFault);
        }
        else
        {
            if (fault)
            {
                fault = false;
                sendFaultInformation(startFault, stopFault);
            }
        }
    }
}