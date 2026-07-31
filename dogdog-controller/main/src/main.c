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
#include "esp_timer.h"
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
#include "OTA.h"
#include "Startup.h"
#include "sdkconfig.h"
#include <inttypes.h>

QueueHandle_t sensorInterruptQueue;
QueueHandle_t buttonInterruptQueue;
QueueHandle_t resetQueue;
QueueHandle_t triggerQueue;
QueueHandle_t sevenSegmentQueue;
QueueHandle_t networkFaultQueue;
QueueHandle_t timeQueue;
QueueHandle_t buzzerQueue;
QueueHandle_t buttonQueue;
QueueSetHandle_t triggerAndResetQueue;
TaskHandle_t buttonTask;
TaskHandle_t sevenSegmentTask;
EventGroupHandle_t startupEventGroup;

int64_t otaGestureDeadlineUs;

static const char *TAG = "Main";

#define STARTUP_READY_TIMEOUT_MS 30000
#define STARTUP_RESTART_DELAY_MS 250

char *pc_programm = "simple-agility";

int station_id = 0;
int controller_id = 0;
int start_id = 0;
int stop_id = 0;

void dogdog_startup_signal_ready(EventBits_t ready_bit)
{
    if (startupEventGroup != NULL)
    {
        xEventGroupSetBits(startupEventGroup, ready_bit);
    }
}

void dogdog_startup_signal_failure(void)
{
    if (startupEventGroup != NULL)
    {
        xEventGroupSetBits(startupEventGroup, DOGDOG_STARTUP_FAILED_BIT);
    }
}

static void restart_without_validating(const char *reason)
{
    ESP_LOGE(TAG, "%s; restarting without validating this image", reason);
    vTaskDelay(pdMS_TO_TICKS(STARTUP_RESTART_DELAY_MS));
    esp_restart();
}

static void validate_image_after_critical_startup(void)
{
    const TickType_t wait_started = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(STARTUP_READY_TIMEOUT_MS);

    while (true)
    {
        const EventBits_t bits = xEventGroupGetBits(startupEventGroup);
        if ((bits & DOGDOG_STARTUP_FAILED_BIT) != 0)
        {
            restart_without_validating("A critical startup task reported failure");
        }
        if ((bits & DOGDOG_STARTUP_REQUIRED_BITS) == DOGDOG_STARTUP_REQUIRED_BITS)
        {
            break;
        }
        if ((TickType_t)(xTaskGetTickCount() - wait_started) >= timeout_ticks)
        {
            ESP_LOGE(TAG, "Critical startup timed out (ready bits 0x%" PRIx32 ")",
                     (uint32_t)(bits & DOGDOG_STARTUP_REQUIRED_BITS));
            restart_without_validating("Critical startup readiness timeout");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    const esp_err_t err = dd_ota_mark_app_valid();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not mark running firmware valid: %s", esp_err_to_name(err));
        restart_without_validating("Application validation failed");
    }
    ESP_LOGI(TAG, "Critical startup complete; running firmware is valid");
}

static void dogdog_ota_status(dd_ota_state_t state, esp_err_t error, void *context)
{
    (void)context;

    switch (state)
    {
    case DD_OTA_CONNECTING:
    case DD_OTA_DOWNLOADING:
    case DD_OTA_VALIDATING:
        display_firmware_upgrade_status("Firmware Upgrade in progress");
        break;
    case DD_OTA_SUCCEEDED:
        display_firmware_upgrade_status("Firmware Upgrade complete\nRestarting...");
        break;
    case DD_OTA_FAILED:
    default:
    {
        char message[96];
        snprintf(message, sizeof(message),
                 "Firmware Upgrade failed\n%s\nRestarting...",
                 esp_err_to_name(error));
        display_firmware_upgrade_status(message);
        break;
    }
    }
}

static void start_dogdog_ota_mode(void)
{
    esp_err_t display_err = init_firmware_upgrade_screen();
    if (display_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not initialize firmware upgrade screen: %s",
                 esp_err_to_name(display_err));
    }

    const dd_ota_options_t options = {
        .status_cb = dogdog_ota_status,
        .context = NULL,
    };
    esp_err_t err = dd_ota_start(&options);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not start OTA worker: %s", esp_err_to_name(err));
        dogdog_ota_status(DD_OTA_FAILED, err, NULL);
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // The shared worker owns the remainder of OTA mode and always restarts.
    vTaskDelete(NULL);
}

static int load_lora_id(const char *key, int configured_value)
{
    int32_t stored_value = 0;
    esp_err_t err = getValueChecked(key, &stored_value);
    if (err == ESP_OK && stored_value >= 0 && stored_value <= UINT8_MAX)
    {
        return (int)stored_value;
    }

    if (err == ESP_OK)
    {
        ESP_LOGW(TAG, "Ignoring out-of-range LoRa ID '%s': %" PRId32, key, stored_value);
    }
    else if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Could not read LoRa ID '%s': %s", key, esp_err_to_name(err));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(storeValue(key, configured_value));
    return configured_value;
}

static bool configure_lora_ids(void)
{
    controller_id = load_lora_id("controller_id", CONFIG_LORA_CONTROLLER_ID);
    start_id = load_lora_id("start_id", CONFIG_START_LORA_ID);
    stop_id = load_lora_id("stop_id", CONFIG_STOP_LORA_ID);

    if (controller_id == start_id || controller_id == stop_id || start_id == stop_id)
    {
        ESP_LOGW(TAG, "Stored LoRa IDs are ambiguous; restoring configured IDs");
        controller_id = CONFIG_LORA_CONTROLLER_ID;
        start_id = CONFIG_START_LORA_ID;
        stop_id = CONFIG_STOP_LORA_ID;
        ESP_ERROR_CHECK_WITHOUT_ABORT(storeValue("controller_id", controller_id));
        ESP_ERROR_CHECK_WITHOUT_ABORT(storeValue("start_id", start_id));
        ESP_ERROR_CHECK_WITHOUT_ABORT(storeValue("stop_id", stop_id));
    }

    if (controller_id == start_id || controller_id == stop_id || start_id == stop_id)
    {
        ESP_LOGE(TAG,
                 "LoRa controller/start/stop IDs must be distinct (controller=%d start=%d stop=%d)",
                 controller_id, start_id, stop_id);
        return false;
    }

    station_id = load_lora_id("station_id", CONFIG_LORA_STATION_ID);
    if (station_id != controller_id)
    {
        /* On the controller, the packet sender ID is the controller ID.  A
           separate/stale station ID makes peers reject packets or mistake the
           controller for a measurement station. */
        ESP_LOGW(TAG, "Correcting controller sender ID from %d to %d", station_id, controller_id);
        station_id = controller_id;
        ESP_ERROR_CHECK_WITHOUT_ABORT(storeValue("station_id", station_id));
    }
    return true;
}

void app_main(void)
{
    if (dd_ota_consume_reboot_request())
    {
        ESP_LOGI(TAG, "Starting clean firmware upgrade boot");
        start_dogdog_ota_mode();
        return;
    }

    otaGestureDeadlineUs = esp_timer_get_time() + 10000000LL;

    startupEventGroup = xEventGroupCreate();
    if (startupEventGroup == NULL)
    {
        restart_without_validating("Failed to create startup event group");
    }

    esp_err_t err = start_ota_gesture_monitor();
    if (err != ESP_OK)
    {
        restart_without_validating("Failed to start early OTA gesture monitor");
    }

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs reinitialization: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        restart_without_validating("Failed to install GPIO ISR service");
    }

    InitLoraHandlers(HandleReceivedPacket);

    ESP_LOGI(TAG, "Starting...");

    sensorInterruptQueue = xQueueCreate(4, sizeof(SensorTriggerEvent));
    buttonInterruptQueue = xQueueCreate(10, sizeof(sensor_interrupt_t));
    resetQueue = xQueueCreate(1, sizeof(int));
    triggerQueue = xQueueCreate(1, sizeof(TimerTrigger));
    networkFaultQueue = xQueueCreate(8, sizeof(StationConnectivityStatus));
    sevenSegmentQueue = xQueueCreate(10, sizeof(SevenSegmentDisplay));
    timeQueue = xQueueCreate(1, sizeof(int64_t));
    buzzerQueue = xQueueCreate(10, sizeof(int));
    buttonQueue = xQueueCreate(15, sizeof(glow_state_t));
    triggerAndResetQueue = xQueueCreateSet(2);

    if (!sensorInterruptQueue || !buttonInterruptQueue || !resetQueue || !triggerQueue ||
        !networkFaultQueue || !sevenSegmentQueue || !timeQueue || !buzzerQueue ||
        !buttonQueue || !triggerAndResetQueue)
    {
        ESP_LOGE(TAG, "Failed to allocate one or more application queues");
        restart_without_validating("Failed to allocate application queues");
    }

    if (xQueueAddToSet(triggerQueue, triggerAndResetQueue) != pdPASS ||
        xQueueAddToSet(resetQueue, triggerAndResetQueue) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to initialize timer queue set");
        restart_without_validating("Failed to initialize timer queue set");
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(increaseKey("startups"));

    // The built firmware selects the hardware role; stale NVS must not override it.
    bool config_is_lora_controller = false;

#ifdef CONFIG_IS_LORA_CONTROLLER
    config_is_lora_controller = true;
#endif

    const int is_lora_controller = config_is_lora_controller ? 1 : 0;
    int32_t stored_controller_mode = -1;
    if (getValueChecked("is_lora_c", &stored_controller_mode) != ESP_OK ||
        stored_controller_mode != is_lora_controller)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(storeValue("is_lora_c", is_lora_controller));
    }
    // ---------------------------------------------------------------------

    // Configure IDs
    if (!configure_lora_ids())
    {
        restart_without_validating("Invalid LoRa identifier configuration");
    }

    //-------

    BaseType_t clock_initialized = init_external_clock();
    ESP_LOGI(TAG, "Clock initialized: %d", clock_initialized);
    if (clock_initialized == pdTRUE &&
        xTaskCreate(ClockTask, "ClockTask", 4048, NULL, 24, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create external clock task");
        deinit_external_clock();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(init_horn_timer_broadcast());
    init_keyboard();
    init_glow_pins();

    if (xTaskCreate(Timer_Task, "Timer_Task", 4048, NULL, 12, NULL) != pdPASS ||
        xTaskCreate(Network_Fault_Task, "Network_Fault_Task", 4048, NULL, 9, NULL) != pdPASS ||
        xTaskCreate(Buzzer_Task, "Buzzer_Task", 4048, NULL, 7, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create a core application task");
        restart_without_validating("Failed to create a core application task");
    }

    /* The display and button tasks notify each other during startup. Create the
       button task first so the display can never notify a null task handle. */
    if (xTaskCreate(Button_Task, "Button_Task", 8192, NULL, 3, &buttonTask) != pdPASS ||
        xTaskCreatePinnedToCore(Seven_Segment_Task, "Seven_Segment_Task", 16096, NULL, 8, &sevenSegmentTask, 1) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create display/button task");
        restart_without_validating("Failed to create display/button task");
    }
    if (is_lora_controller != 1)
    {
        if (xTaskCreatePinnedToCore(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 4048, NULL, 23, NULL, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create sensor task");
            restart_without_validating("Failed to create cable sensor task");
        }
    }
    else
    {
        if (xTaskCreate(LoraStartupTask, "LoraStartupTask", 4048, NULL, 10, NULL) != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create LoRa startup task");
            restart_without_validating("Failed to create LoRa startup task");
        }
    }

    if (xTaskCreate(Button_Input_Task, "Button_Input_Task", 8192, NULL, 8, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create button input task");
        restart_without_validating("Failed to create button input task");
    }

    validate_image_after_critical_startup();
}
