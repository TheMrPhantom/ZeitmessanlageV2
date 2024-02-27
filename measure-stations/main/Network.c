#include "Network.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "nvs_flash.h"

#define min(x, y) (((x) < (y)) ? (x) : (y))

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

const char *NETWORK_TAG = "NETWORK";
extern QueueHandle_t timerQueue;
extern QueueHandle_t networkQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t triggerQueue;

extern QueueSetHandle_t networkAndResetQueue;

void init_wifi(void)
{
    ESP_LOGI(NETWORK_TAG, "Configuring and starting WIFI");

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_netif_init();
    esp_event_loop_create_default();

    nvs_flash_init();

    esp_wifi_init(&wifi_config);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
    esp_wifi_start();

    ESP_LOGI(NETWORK_TAG, "Wifi configured and started");
}

void receiveCallback(const uint8_t *macAddr, const uint8_t *data, int dataLen)
// Called when data is received
{
    // Only allow a maximum of 250 characters in the message + a null terminating byte
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
    strncpy(buffer, (const char *)data, msgLen);

    // Make sure we are null terminated
    buffer[msgLen] = 0;

    ESP_LOGI(NETWORK_TAG, "Received Package: %s", buffer);

    int cause = -1;

    if (strncmp(buffer, "trigger", 7) == 0)
    {
        cause = 1;
    }
    else if (strncmp(buffer, "reset", 5) == 0)
    {
        cause = 2;
    }

    if (cause != -1)
    {
        xQueueSend(timerQueue, &cause, 0);
    }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
    ESP_LOGI(NETWORK_TAG, "Last Packet Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Delivery Success" : "Delivery Fail");
}

void broadcast(char *message)
// Emulates a broadcast
{
    const char *BROADCAST_TAG = "NETWORK-BROADCAST";

    ESP_LOGI(BROADCAST_TAG, "Sending message: %s", message);
    // Broadcast a message to every device in range
    uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t peerInfo = {};
    memcpy(&peerInfo.peer_addr, broadcastAddress, 6);
    if (!esp_now_is_peer_exist(broadcastAddress))
    {
        esp_now_add_peer(&peerInfo);
    }

    // Send message
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)message, strlen(message));

    // Print results to serial monitor
    if (result != ESP_OK)
    {
        if (result == ESP_ERR_ESPNOW_NOT_INIT)
        {
            ESP_LOGE(BROADCAST_TAG, "ESP-NOW not Init.");
        }
        else if (result == ESP_ERR_ESPNOW_ARG)
        {
            ESP_LOGE(BROADCAST_TAG, "Invalid Argument");
        }
        else if (result == ESP_ERR_ESPNOW_INTERNAL)
        {
            ESP_LOGE(BROADCAST_TAG, "Internal Error");
        }
        else if (result == ESP_ERR_ESPNOW_NO_MEM)
        {
            ESP_LOGE(BROADCAST_TAG, "ESP_ERR_ESPNOW_NO_MEM");
        }
        else if (result == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            ESP_LOGE(BROADCAST_TAG, "Peer not found.");
        }
        else
        {
            ESP_LOGE(BROADCAST_TAG, "Unknown error");
        }
    }
}

void Network_Task(void *params)
{

    ESP_LOGI(NETWORK_TAG, "Initialising Network");

    init_wifi();

    if (esp_now_init() == ESP_OK)
    {
        ESP_LOGI(NETWORK_TAG, "ESP-NOW Init Success");
        esp_now_register_recv_cb(receiveCallback);
        esp_now_register_send_cb(sentCallback);
    }
    else
    {
        ESP_LOGE(NETWORK_TAG, "ESP-NOW Init Failed");
        vTaskDelay(pdTICKS_TO_MS(3000));
        esp_restart();
    }

    while (true)
    {
        QueueHandle_t queueToProcess = xQueueSelectFromSet(networkAndResetQueue, pdMS_TO_TICKS(5000));

        if (queueToProcess != NULL)
        {
            if (queueToProcess == networkQueue)
            {
                int receivedRuntime = -1;
                xQueueReceive(networkQueue, &receivedRuntime, pdMS_TO_TICKS(5000));

                if (receivedRuntime != -1)
                {
                    /*Determine string lenght of measured time*/
                    int length = 0;
                    long temp = 1;
                    while (temp <= receivedRuntime)
                    {
                        length++;
                        temp *= 10;
                    }

                    /*Create char array with that lenght*/
                    char *message = malloc(sizeof(char) * (length + 1));
                    message[length] = 0x00;
                    sprintf(message, "%i", receivedRuntime);

                    broadcast(message);

                    free(message);
                }
            }
            else if (queueToProcess == resetQueue)
            {
                broadcast("reset-remote");
            }
            else if (queueToProcess == triggerQueue)
            {
                broadcast("trigger");
            }
        }
        if (STATION_TYPE == 0)
        {
            broadcast("alive-start");
        }
        else if (STATION_TYPE == 1)
        {
            broadcast("alive-stop");
        }
    }
}