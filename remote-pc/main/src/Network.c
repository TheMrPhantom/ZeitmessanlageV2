#include "Network.h"

#define min(x, y) (((x) < (y)) ? (x) : (y))

#if CONFIG_START
#define STATION_TYPE 0
#elif CONFIG_STOP
#define STATION_TYPE 1
#endif

static const char *NETWORK_TAG = "NETWORK";
QueueHandle_t receivedTimeQueue;
extern QueueHandle_t sendQueue;
extern QueueHandle_t buttonQueue;
extern bool sensors_active;

void init_wifi(void)
{
    ESP_LOGI(NETWORK_TAG, "Configuring and starting WIFI");

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    uint8_t custom_mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x03}; // MAC with "dog"
    esp_err_t error = esp_netif_init();
    error |= esp_event_loop_create_default();
    error |= esp_wifi_init(&wifi_config);
    error |= esp_wifi_set_mode(WIFI_MODE_STA);
    error |= esp_wifi_set_storage(WIFI_STORAGE_RAM);
    error |= esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
    error |= esp_wifi_set_mac(WIFI_IF_STA, custom_mac);
    error |= esp_wifi_start();

    if (esp_now_init() == ESP_OK)
    {
        ESP_LOGI(NETWORK_TAG, "ESP-NOW Init Success");
        error |= esp_now_register_recv_cb(receiveCallback);
        error |= esp_now_register_send_cb(sentCallback);

        {
            uint8_t mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x00};
            error |= add_peer(mac);
        }
        {
            uint8_t mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x01};
            error |= add_peer(mac);
        }
        {
            uint8_t mac[6] = {0x06, 0x64, 0x6F, 0x67, 0x2D, 0x02};
            error |= add_peer(mac);
        }

        if (error != ESP_OK)
        {
            ESP_LOGE(NETWORK_TAG, "Failed to add peer: %s", esp_err_to_name(error));
            vTaskDelay(pdTICKS_TO_MS(3000));
            esp_restart();
        }
    }
    else
    {
        ESP_LOGE(NETWORK_TAG, "ESP-NOW Init Failed");
        vTaskDelay(pdTICKS_TO_MS(3000));
        esp_restart();
    }

    ESP_LOGI(NETWORK_TAG, "Wifi configured and started");
}

void receiveCallback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int dataLen)
// Called when data is received
{
    // Only allow a maximum of 250 characters in the message + a null terminating byte
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, dataLen);
    strncpy(buffer, (const char *)data, msgLen);

    // Make sure we are null terminated
    buffer[msgLen] = 0;

    ESP_LOGI(NETWORK_TAG, "Received Package: %s", buffer);
    if (strncmp(buffer, "timer-time", 10) == 0 && sensors_active)
    {
        // Extract the rest of the message without the command
        char *message = buffer + 10;
        ESP_LOGI(NETWORK_TAG, "Received timer-time: %s", message);
        int time = atoi(message);
        xQueueSend(receivedTimeQueue, &time, 0);

        glow_state_t glow_state;
        glow_state.state = 0;
        glow_state.pinNumber = BUTTON_GLOW_TYPE_RESET;
        xQueueSend(buttonQueue, &glow_state, 0);
    }
    else if (strncmp(buffer, "timer-start", 11) == 0)
    {
        glow_state_t glow_state;
        glow_state.state = 1;
        glow_state.pinNumber = BUTTON_GLOW_TYPE_RESET;
        xQueueSend(buttonQueue, &glow_state, 0);
    }
    else if (strncmp(buffer, "timer-reset", 11) == 0)
    {
        glow_state_t glow_state;
        glow_state.state = 0;
        glow_state.pinNumber = BUTTON_GLOW_TYPE_RESET;
        xQueueSend(buttonQueue, &glow_state, 0);
    }
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
    ESP_LOGI(NETWORK_TAG, "Last Packet Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Delivery Success" : "Delivery Fail");
}

void Network_Task(void *params)
{

    ESP_LOGI(NETWORK_TAG, "Initialising Network");

    init_wifi();

    while (true)
    {
        int ms = 0;
        if (xQueueReceive(receivedTimeQueue, &ms, portMAX_DELAY))
        {
            // Convert time wich is is milliseconds to seconds string with comma like ss,ss
            char *output;
            int seconds = ms / 1000;
            int fractional = ms % 1000 / 10; // Get two decimal places

            ESP_LOGD(NETWORK_TAG, "Calculated times: %d, %d", seconds, fractional);

            // Determine the required length for the string
            int length = snprintf(NULL, 0, "%d,%02d", seconds, fractional);

            ESP_LOGD(NETWORK_TAG, "Length of time: %d", length);

            // Allocate exact amount of space needed
            output = (char *)malloc((length + 1) * sizeof(char));

            if (output == NULL)
            {
                ESP_LOGE(NETWORK_TAG, "Failed to allocate memory for time");
                continue;
            }

            ESP_LOGD(NETWORK_TAG, "Allocated memory for time: %d", length + 1);

            // Format the result as a string with comma
            snprintf(output, length + 1, "%d,%02d", seconds, fractional);
            ESP_LOGI(NETWORK_TAG, "Sending time: %s", output);

            // Print the result using the usb keyboard
            sendText(output);
            sendKey(HID_KEY_ENTER);

            free(output);
        }
    }
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

esp_err_t add_peer(uint8_t *peer_addr)
// Add a peer to the peer list
{
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peer_addr, ESP_NOW_ETH_ALEN);
    peerInfo.channel = 0; // Use the current channel
    return esp_now_add_peer(&peerInfo);
}
