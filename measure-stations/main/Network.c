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
#include "Buzzer.h"

#define min(x, y) (((x) < (y)) ? (x) : (y))

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

const char *NETWORK_TAG = "NETWORK";
extern QueueHandle_t networkQueue;
extern QueueHandle_t resetQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t faultQueue;

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

    if (STATION_TYPE == 0)
    {
        // Is start
        if (strncmp(buffer, "trigger-stop", 12) == 0)
        {
            broadcast("trigger-stop");
        }
        if (strncmp(buffer, "alive-stop", 13) == 0)
        {
            broadcast("alive-stop");
        }
    }
    else if (STATION_TYPE == 1)
    {
        // Is stop
        if (strncmp(buffer, "trigger-start", 13) == 0)
        {
            broadcast("trigger-start");
        }

        if (strncmp(buffer, "alive-start", 11) == 0)
        {
            broadcast("alive-start");
        }
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

    bool sensorFault = false;

    while (true)
    {
        int trigger = 0;
        if (xQueueReceive(triggerQueue, &trigger, pdMS_TO_TICKS(5000)))
        {
            if (STATION_TYPE == 0)
            {
                broadcast("trigger-start");
            }
            else if (STATION_TYPE == 1)
            {
                broadcast("trigger-stop");
            }
        }
        trigger = 0;
        BaseType_t faultMessage = xQueueReceive(faultQueue, &trigger, 0);
        if (faultMessage)
        {
            if (trigger == Buzzer_INDICATE_ERROR)
            {
                sensorFault = !sensorFault;
            }
        }
        if (!sensorFault)
        {
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
}