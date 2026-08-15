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
#include "SevenSegment.h"
#include "NetworkFault.h"
#include "KeyValue.h"
#include "Buzzer.h"
#include "Keyboard.h"
#include "ButtonInput.h"
#include "Button.h"
#include "ra01s.h"
#include "LoraNetwork.h"
#include "Timer.h"
#include <stdio.h>
#include "esp-idf-ds3231.h"
#include "GPIOPins.h"
#include "Clock.h"
#include "Lora.h"
#include "Sensor.h"
#include "HornTimer.h"
#include "TimepanelClient.h"
#include "PcSerial.h"
#include "OTA.h"
#include "sdkconfig.h"
#include "esp_random.h"
#include <inttypes.h>
#include <sys/time.h>

QueueHandle_t sensorInterruptQueue;
QueueHandle_t buttonInterruptQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;
QueueHandle_t sevenSegmentQueue;
QueueHandle_t networkFaultQueue;
QueueHandle_t timeQueue;
QueueHandle_t sendQueue;
QueueHandle_t buzzerQueue;
QueueSetHandle_t triggerAndResetQueue;
TaskHandle_t buttonTask;
TaskHandle_t sevenSegmentTask;

static const char *TAG = "Main";

char *pc_programm = "simple-agility";

int station_id = 0;
int controller_id = 0;
int start_id = 0;
int stop_id = 0;

static void controller_ota_status(dogdog_ota_event_t event, void *user_ctx)
{
    (void)user_ctx;

    switch (event)
    {
    case DOGDOG_OTA_EVENT_WIFI_FOUND:
    case DOGDOG_OTA_EVENT_CONNECTING:
    case DOGDOG_OTA_EVENT_UPDATING:
        show_firmware_upgrade_screen();
        break;
    default:
        break;
    }
}

#if CONFIG_TIMEPANEL_TEST_TIMER_SEQUENCE
static int64_t current_time_us(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (int64_t)now.tv_sec * 1000000LL + now.tv_usec;
}

static void send_timer_test_trigger(bool is_start, int64_t timestamp_us)
{
    TimerTrigger trigger = {
        .is_start = is_start,
        .timestamp = timestamp_us,
        .is_final_time = !is_start,
    };

    if (xQueueSend(triggerQueue, &trigger, pdMS_TO_TICKS(250)) != pdTRUE)
    {
        ESP_LOGW(TAG, "Temporary timer test trigger queue is full");
    }
}

static uint32_t random_timer_test_run_ms(void)
{
    const uint32_t configured_min_ms = CONFIG_TIMEPANEL_TEST_TIMER_RUN_MS;
    const uint32_t configured_max_ms = CONFIG_TIMEPANEL_TEST_TIMER_RUN_MAX_MS;
    const uint32_t min_ms = configured_min_ms < configured_max_ms
                                ? configured_min_ms
                                : configured_max_ms;
    const uint32_t max_ms = configured_min_ms < configured_max_ms
                                ? configured_max_ms
                                : configured_min_ms;
    const uint32_t span_ms = max_ms - min_ms + 1;

    return min_ms + (esp_random() % span_ms);
}

static void timepanel_test_timer_sequence_task(void *params)
{
    (void)params;

    ESP_LOGW(TAG,
             "Temporary timer test enabled: start in %d ms, %d repetitions, runs %d..%d ms, gap %d ms",
             CONFIG_TIMEPANEL_TEST_TIMER_START_DELAY_MS,
             CONFIG_TIMEPANEL_TEST_TIMER_REPETITIONS,
             CONFIG_TIMEPANEL_TEST_TIMER_RUN_MS,
             CONFIG_TIMEPANEL_TEST_TIMER_RUN_MAX_MS,
             CONFIG_TIMEPANEL_TEST_TIMER_GAP_MS);

    vTaskDelay(pdMS_TO_TICKS(CONFIG_TIMEPANEL_TEST_TIMER_START_DELAY_MS));

    for (int repetition = 0;
         repetition < CONFIG_TIMEPANEL_TEST_TIMER_REPETITIONS;
         repetition++)
    {
        const uint32_t run_ms = random_timer_test_run_ms();
        const int64_t start_timestamp_us = current_time_us();
        send_timer_test_trigger(true, start_timestamp_us);
        ESP_LOGW(TAG,
                 "Temporary timer test run %d/%d sent start trigger, stopping after %" PRIu32 " ms",
                 repetition + 1,
                 CONFIG_TIMEPANEL_TEST_TIMER_REPETITIONS,
                 run_ms);

        vTaskDelay(pdMS_TO_TICKS(run_ms));

        const int64_t stop_timestamp_us =
            start_timestamp_us + (int64_t)run_ms * 1000LL;
        send_timer_test_trigger(false, stop_timestamp_us);
        ESP_LOGW(TAG,
                 "Temporary timer test run %d/%d sent stop trigger at %" PRId64 " ms",
                 repetition + 1,
                 CONFIG_TIMEPANEL_TEST_TIMER_REPETITIONS,
                 (stop_timestamp_us - start_timestamp_us) / 1000);

        if (repetition + 1 < CONFIG_TIMEPANEL_TEST_TIMER_REPETITIONS)
        {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_TIMEPANEL_TEST_TIMER_GAP_MS));
        }
    }

    vTaskDelete(NULL);
}

static void start_timepanel_test_timer_sequence(void)
{
    const BaseType_t result =
        xTaskCreate(timepanel_test_timer_sequence_task,
                    "timepanel_test",
                    3072,
                    NULL,
                    6,
                    NULL);
    if (result != pdPASS)
    {
        ESP_LOGW(TAG, "Temporary timer test task could not be started");
    }
}
#endif

void app_main(void)
{

    // Initialize LoRa
    nvs_flash_init();
    const dogdog_ota_config_t ota_config = {
        .device_name = "dogdog-controller",
        .status_cb = controller_ota_status,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(dogdog_ota_check_and_update_ex(&ota_config));

    gpio_install_isr_service(0);
    InitLoraHandlers(HandleReceivedPacket);

    ESP_LOGI(TAG, "Starting...");

    sensorInterruptQueue = xQueueCreate(1, sizeof(int));
    buttonInterruptQueue = xQueueCreate(1, sizeof(int));
    resetQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(TimerTrigger));
    networkFaultQueue = xQueueCreate(2, sizeof(StationConnectivityStatus));
    sevenSegmentQueue = xQueueCreate(10, sizeof(SevenSegmentDisplay));
    timeQueue = xQueueCreate(1, sizeof(int64_t));
    sendQueue = xQueueCreate(50, sizeof(char *));
    buzzerQueue = xQueueCreate(10, sizeof(int));
    triggerAndResetQueue = xQueueCreateSet(2);
    xQueueAddToSet(triggerQueue, triggerAndResetQueue);
    xQueueAddToSet(resetQueue, triggerAndResetQueue);

    increaseKey("startups");

    // Configure device as cable or radio controller permamently -------
    int is_lora_controller = getValue("is_lora_c");
    bool config_is_lora_controller = false;

#ifdef CONFIG_IS_LORA_CONTROLLER
    config_is_lora_controller = true;
#endif

    if (config_is_lora_controller && is_lora_controller != 1)
    {
        storeValue("is_lora_c", 1);
    }
    is_lora_controller = getValue("is_lora_c");
    // ---------------------------------------------------------------------

    // Configure IDs

    controller_id = getValue("controller_id");

    if (controller_id == 0)
    {
        controller_id = CONFIG_LORA_CONTROLLER_ID;
        storeValue("controller_id", CONFIG_LORA_CONTROLLER_ID);
    }

    start_id = getValue("start_id");

    if (start_id == 0)
    {
        start_id = CONFIG_START_LORA_ID;
        storeValue("start_id", CONFIG_START_LORA_ID);
    }

    stop_id = getValue("stop_id");

    if (stop_id == 0)
    {
        stop_id = CONFIG_STOP_LORA_ID;
        storeValue("stop_id", CONFIG_STOP_LORA_ID);
    }

    station_id = getValue("station_id");
    if (station_id == 0)
    {
        station_id = CONFIG_LORA_STATION_ID;
        storeValue("station_id", CONFIG_LORA_STATION_ID);
    }

    //-------

    BaseType_t clock_initialized = init_external_clock();
    ESP_LOGI(TAG, "Clock initialized: %d", clock_initialized);
    ESP_ERROR_CHECK_WITHOUT_ABORT(init_horn_timer_broadcast());
    ESP_ERROR_CHECK_WITHOUT_ABORT(init_timepanel_client());
    init_keyboard();
    init_glow_pins();

    xTaskCreate(Timer_Task, "Timer_Task", 4048, NULL, 12, NULL);
    xTaskCreate(Network_Fault_Task, "Network_Fault_Task", 4048, NULL, 9, NULL);
    xTaskCreatePinnedToCore(Seven_Segment_Task, "Seven_Segment_Task", 16096, NULL, 8, &sevenSegmentTask, 1);
    xTaskCreate(Buzzer_Task, "Buzzer_Task", 4048, NULL, 7, NULL);
    xTaskCreate(Pc_Serial_Task, "Pc_Serial_Task", 4096, NULL, 6, NULL);

    if (!config_is_lora_controller)
    {
        xTaskCreatePinnedToCore(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 4048, NULL, 23, NULL, 0);
    }
    else
    {
        xTaskCreate(LoraStartupTask, "LoraStartupTask", 4048, NULL, 10, NULL);
    }

    xTaskCreate(Button_Input_Task, "Button_Input_Task", 8192, NULL, 8, NULL);
    xTaskCreate(Button_Task, "Button_Task", 8192, NULL, 3, &buttonTask);

#if CONFIG_TIMEPANEL_TEST_TIMER_SEQUENCE
    start_timepanel_test_timer_sequence();
#endif

    if (clock_initialized == pdTRUE)
    {
        xTaskCreate(ClockTask, "ClockTask", 4048, NULL, 24, NULL);
    }
}
