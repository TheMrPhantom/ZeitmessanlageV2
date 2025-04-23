#include "Network.h"

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
extern QueueHandle_t sendQueue;

void Network_Task(void *params)
{

    ESP_LOGI(NETWORK_TAG, "Initialising Network");

    init_wifi();

    if (esp_now_init() == ESP_OK)
    {
        ESP_LOGI(NETWORK_TAG, "ESP-NOW Init Success");
        esp_err_t result = ESP_OK;
        result |= esp_now_register_recv_cb(receiveCallback);
        result |= esp_now_register_send_cb(sentCallback);

        if (STATION_TYPE == 0)
        {
            uint8_t mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x01};
            result |= add_peer(mac);
        }
        else
        {
            uint8_t mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x02};
            result |= add_peer(mac);
        }
        uint8_t mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x00};
        result |= add_peer(mac);

        if (result != ESP_OK)
        {
            ESP_LOGE(NETWORK_TAG, "Error adding peer: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
    }
    else
    {
        ESP_LOGE(NETWORK_TAG, "ESP-NOW Init Failed");
        vTaskDelay(pdMS_TO_TICKS(3000));
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
                queue_to_send("trigger-start");
            }
            else if (STATION_TYPE == 1)
            {
                queue_to_send("trigger-stop");
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
                queue_to_send("alive-start");
            }
            else if (STATION_TYPE == 1)
            {
                queue_to_send("alive-stop");
            }
        }
    }
}

void Network_Send_Task(void *params)
{
    while (true)
    {
        char *message = NULL;
        if (xQueueReceive(sendQueue, &message, portMAX_DELAY))
        {
            send_to_all(message);
        }
        else
        {
            ESP_LOGE(NETWORK_TAG, "Failed to receive message from queue");
        }
    }
}

void init_wifi(void)
{
    ESP_LOGI(NETWORK_TAG, "Configuring and starting WIFI");

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t error = esp_netif_init();
    error |= esp_event_loop_create_default();
    error |= nvs_flash_init();
    error |= esp_wifi_init(&wifi_config);
    error |= esp_wifi_set_mode(WIFI_MODE_STA);
    error |= esp_wifi_set_storage(WIFI_STORAGE_RAM);
    error |= esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

    if (STATION_TYPE == 0)
    {
        uint8_t custom_mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x02}; // MAC with "dog"
        error |= esp_wifi_set_mac(WIFI_IF_STA, custom_mac);
    }
    else
    {
        uint8_t custom_mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x01}; // MAC with "dog"
        error |= esp_wifi_set_mac(WIFI_IF_STA, custom_mac);
    }
    error |= esp_wifi_start();

    if (error != ESP_OK)
    {
        ESP_LOGE(NETWORK_TAG, "Error starting wifi: %s", esp_err_to_name(error));
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }
    ESP_LOGI(NETWORK_TAG, "Wifi configured and started");
}

esp_err_t add_peer(uint8_t *peer_addr)
// Add a peer to the peer list
{
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
    peerInfo.channel = 0; // Use the current channel
    return esp_now_add_peer(&peerInfo);
}

void receiveCallback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
// Called when data is received
{
    // Only allow a maximum of 250 characters in the message + a null terminating byte
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, data_len);
    strncpy(buffer, (const char *)data, msgLen);

    // Make sure we are null terminated
    buffer[msgLen] = 0;

    ESP_LOGI(NETWORK_TAG, "Received Package: %s", buffer);

    if (STATION_TYPE == 0)
    {
        // Is start
        if (strncmp(buffer, "trigger-stop", 12) == 0)
        {
            queue_to_send("trigger-stop");
        }
        if (strncmp(buffer, "alive-stop", 13) == 0)
        {
            queue_to_send("alive-stop");
        }
    }
    else if (STATION_TYPE == 1)
    {
        // Is stop
        if (strncmp(buffer, "trigger-start", 13) == 0)
        {
            queue_to_send("trigger-start");
        }

        if (strncmp(buffer, "alive-start", 11) == 0)
        {
            queue_to_send("alive-start");
        }
    }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
    ESP_LOGI(NETWORK_TAG, "Last Packet Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Delivery Success" : "Delivery Fail");
}

void queue_to_send(char *message)
// Emulates a broadcast
{
    char *buffer = malloc(strlen(message) + 1);
    if (buffer == NULL)
    {
        ESP_LOGE(NETWORK_TAG, "Failed to allocate memory for message");
        return;
    }
    strcpy(buffer, message);
    // Send message
    esp_err_t result = xQueueSend(sendQueue, &buffer, 0);
    if (result != pdTRUE)
    {
        ESP_LOGE(NETWORK_TAG, "Failed to send message to queue");
        free(buffer);
        return;
    }
}

void send_to_all(char *message)
// Emulates a broadcast
{
    const char *BROADCAST_TAG = "NETWORK-BROADCAST";

    ESP_LOGI(BROADCAST_TAG, "Sending message: %s", message);
    // Broadcast a message to every device in range

    // Send message
    esp_err_t result = esp_now_send(NULL, (uint8_t *)message, strlen(message));
    free(message);

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