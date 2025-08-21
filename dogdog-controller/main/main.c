/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "lv_conf.h"
#include "esp_lcd_touch_cst816s.h"
#include <esp_system.h>
#include "soc/soc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "Timer.h"
#include "Network.h"
#include "SevenSegment.h"
#include "NetworkFault.h"
#include "KeyValue.h"
#include "Buzzer.h"
#include "Keyboard.h"
#include "ButtonInput.h"
#include "Button.h"
#include "ra01s.h"

QueueHandle_t sensorInterputQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;
QueueHandle_t sevenSegmentQueue;
QueueHandle_t networkFaultQueue;
QueueHandle_t timeQueue;
QueueHandle_t sendQueue;
QueueHandle_t buzzerQueue;
QueueSetHandle_t triggerAndResetQueue;
TaskHandle_t buttonTask;

static const char *TAG = "Main";

timeval_t t, t2;

// Simple GPIO ISR handler
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    // You can add your interrupt handling code here
    uint32_t gpio_num = (uint32_t)arg;
    // For example, just log the interrupt (avoid heavy processing in ISR)
    // ets_printf("GPIO Interrupt on GPIO %d\n", gpio_num);
    gettimeofday(&t, NULL);
}

static void IRAM_ATTR gpio_isr_handler_2(void *arg)
{
    // You can add your interrupt handling code here
    uint32_t gpio_num = (uint32_t)arg;
    // For example, just log the interrupt (avoid heavy processing in ISR)
    // ets_printf("GPIO Interrupt on GPIO %d\n", gpio_num);
    gettimeofday(&t2, NULL);
}

void test(void)
{
    for (int i = 0; i < 15; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
        receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));
    }

    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    }

    for (int i = 0; i < 10; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
        receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));
    }
}

void task_tx(void *pvParameters)
{
    ESP_LOGI(pcTaskGetName(NULL), "Start");
    uint8_t buf[255]; // Maximum Payload size of SX1261/62/68 is 255
    while (1)
    {
        TickType_t nowTick = xTaskGetTickCount();
        int txLen = sprintf((char *)buf, "Hello World %" PRIu32, nowTick);
        ESP_LOGI(pcTaskGetName(NULL), "%d byte packet sent...", txLen);

        // set pin high
        gpio_set_level(12, 1);

        // Wait for transmission to complete
        if (LoRaSend(buf, txLen, SX126x_TXMODE_SYNC) == false)
        {
            ESP_LOGE(pcTaskGetName(NULL), "LoRaSend fail");
        }

        // Do not wait for the transmission to be completed
        // LoRaSend(buf, txLen, SX126x_TXMODE_ASYNC );

        int lost = GetPacketLost();
        if (lost != 0)
        {
            ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
        }

        // set pin low
        gpio_set_level(12, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    } // end while

    // never reach here
    vTaskDelete(NULL);
}

void task_rx(void *pvParameters)
{
    ESP_LOGI(pcTaskGetName(NULL), "Start");
    uint8_t buf[255]; // Maximum Payload size of SX1261/62/68 is 255
    while (1)
    {
        uint8_t rxLen = LoRaReceive(buf, sizeof(buf));
        if (rxLen > 0)
        {
            timeval_t now;
            gettimeofday(&now, NULL);

            // difference between now and t
            int time = TIME_US(t2) - TIME_US(t);
            // ESP_LOGI(pcTaskGetName(NULL), "Time difference: %d us", time);

            ESP_LOGI(pcTaskGetName(NULL), "%d byte packet received:[%.*s]", rxLen, rxLen, buf);

            int8_t rssi, snr;
            GetPacketStatus(&rssi, &snr);
            ESP_LOGI(pcTaskGetName(NULL), "rssi=%d[dBm] snr=%d[dB]", rssi, snr);
        }
        vTaskDelay(1); // Avoid WatchDog alerts
    } // end while

    // never reach here
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Initialize LoRa
    LoRaInit();
    int8_t txPowerInDbm = 22;
    uint32_t frequencyInHz = 868000000;
    ESP_LOGI(TAG, "Frequency is 868MHz");
    float tcxoVoltage = 3.3;     // use TCXO
    bool useRegulatorLDO = true; // use DCDC + LDO

    // LoRaDebugPrint(true);
    if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0)
    {
        ESP_LOGE(TAG, "Does not recognize the module");
        while (1)
        {
            vTaskDelay(1);
        }
    }

    uint8_t spreadingFactor = 10;
    uint8_t bandwidth = 5;
    uint8_t codingRate = 1;
    uint16_t preambleLength = 8;
    uint8_t payloadLen = 0;
    bool crcOn = true;
    bool invertIrq = false;

    LoRaConfig(spreadingFactor, bandwidth, codingRate, preambleLength, payloadLen, crcOn, invertIrq);

    xTaskCreate(&task_rx, "RX", 1024 * 4, NULL, 11, NULL);
    // xTaskCreate(&task_tx, "TX", 1024 * 4, NULL, 24, NULL);

    // // config pin12 as output
    // gpio_reset_pin(12);
    // gpio_set_direction(12, GPIO_MODE_OUTPUT);
    // gpio_set_level(12, 0);

    // gpio_install_isr_service(0);
    // // config pin11 as input
    // gpio_reset_pin(11);
    // gpio_set_direction(11, GPIO_MODE_INPUT);
    // gpio_set_intr_type(11, GPIO_INTR_POSEDGE);

    // gpio_isr_handler_add(11, gpio_isr_handler, (void *)11);

    // // ssame for pin 20
    // gpio_reset_pin(20);
    // gpio_set_direction(20, GPIO_MODE_INPUT);
    // gpio_set_intr_type(20, GPIO_INTR_POSEDGE);
    // gpio_isr_handler_add(20, gpio_isr_handler_2, (void *)20);

    nvs_flash_init();

    ESP_LOGI(TAG, "Starting...");

    sensorInterputQueue = xQueueCreate(1, sizeof(int));
    resetQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(int));
    networkFaultQueue = xQueueCreate(2, sizeof(int));
    sevenSegmentQueue = xQueueCreate(10, sizeof(SevenSegmentDisplay));
    timeQueue = xQueueCreate(1, sizeof(int));
    sendQueue = xQueueCreate(50, sizeof(char *));
    buzzerQueue = xQueueCreate(10, sizeof(int));

    triggerAndResetQueue = xQueueCreateSet(2);
    xQueueAddToSet(triggerQueue, triggerAndResetQueue);
    xQueueAddToSet(resetQueue, triggerAndResetQueue);

    increaseKey("startups");

    init_keyboard();
    init_glow_pins();

    xTaskCreate(Timer_Task, "Timer_Task", 4048, NULL, 12, NULL);
    xTaskCreate(Network_Fault_Task, "Network_Fault_Task", 2048, NULL, 7, NULL);
    xTaskCreatePinnedToCore(Seven_Segment_Task, "Seven_Segment_Task", 16096, NULL, 8, NULL, 1);
    xTaskCreate(Network_Task, "Network_Task", 8192, NULL, 9, NULL);
    xTaskCreate(Network_Send_Task, "Network_Send_Task", 8192, NULL, 10, NULL);
    xTaskCreate(Buzzer_Task, "Buzzer_Task", 4048, NULL, 7, NULL);
    xTaskCreate(Button_Input_Task, "Button_Input_Task", 8192, NULL, 8, NULL);
    xTaskCreate(Button_Task, "Button_Task", 8192, NULL, 3, &buttonTask);

    /*
    vTaskDelay(pdMS_TO_TICKS(4000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-start", strlen("alive-start"));
    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"alive-stop", strlen("alive-stop"));

    xTaskCreate(test, "test", 4048, NULL, 3, NULL);
    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(8746));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(3000 + random() % 1000));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));
    vTaskDelay(pdMS_TO_TICKS(16273));
    receiveCallback(NULL, (const uint8_t *)"trigger-stop", strlen("trigger-stop"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"reset", strlen("reset"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"reset", strlen("reset"));

    vTaskDelay(pdMS_TO_TICKS(5000));
    receiveCallback(NULL, (const uint8_t *)"countdown-7", strlen("countdown-7"));

    vTaskDelay(pdMS_TO_TICKS(1000 * 60 * 7 + 15));
    receiveCallback(NULL, (const uint8_t *)"trigger-start", strlen("trigger-start"));
    */
}
