/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "nvs_flash.h"

#define min(x, y) (((x) < (y)) ? (x) : (y))

const char *TAG = "MAIN";

void receiveCallback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
// Called when data is received
{
    // Only allow a maximum of 250 characters in the message + a null terminating byte
    char buffer[ESP_NOW_MAX_DATA_LEN + 1];
    int msgLen = min(ESP_NOW_MAX_DATA_LEN, data_len);
    strncpy(buffer, (const char *)data, msgLen);

    // Make sure we are null terminated
    buffer[msgLen] = 0;

    ESP_LOGI(TAG, "Received Package: %s", buffer);
}

void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status)
// Called when data is sent
{
    ESP_LOGI(TAG, "Last Packet Send Status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "Delivery Success" : "Delivery Fail");
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

void app_main(void)
{
    while (true)
    {
        esp_rom_gpio_pad_select_gpio(GPIO_NUM_33);
        gpio_set_direction(GPIO_NUM_33, GPIO_MODE_INPUT);
        gpio_pulldown_dis(GPIO_NUM_33);
        gpio_pullup_en(GPIO_NUM_33);

        ESP_LOGI(TAG, "Initialising WiFi");
        wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
        esp_netif_init();
        esp_event_loop_create_default();

        nvs_flash_init();

        esp_wifi_init(&wifi_config);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
        esp_wifi_start();

        ESP_LOGI(TAG, "Initialising ESP NOW");
        if (esp_now_init() == ESP_OK)
        {
            ESP_LOGI(TAG, "ESP-NOW Init Success");
            esp_now_register_recv_cb(receiveCallback);
            esp_now_register_send_cb(sentCallback);
        }
        else
        {
            ESP_LOGE(TAG, "ESP-NOW Init Failed");
            vTaskDelay(pdTICKS_TO_MS(3000));
            esp_restart();
        }

        broadcast("reset");

        esp_wifi_stop();

        while (gpio_get_level(GPIO_NUM_33) == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        rtc_gpio_pullup_en(GPIO_NUM_33);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0);
        esp_light_sleep_start();
    }
}