#ifndef __NETWORK_H
#define __NETWORK_H

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
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_http_server.h>
#include <string.h>
#include <esp_system.h>
#include "NetworkFault.h"
#include "SevenSegment.h"

void Network_Task(void *params);
void enableOTA();
void init_wifi(void);
void receiveCallback(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
void sentCallback(const uint8_t *macAddr, esp_now_send_status_t status);
esp_err_t add_peer(uint8_t *peer_addr);
void Network_Task(void *params);
void queue_to_send(char *message);
void send_to_all(char *message);
void Network_Send_Task(void *params);
#endif