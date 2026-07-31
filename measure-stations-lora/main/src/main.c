#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "Buzzer.h"
#include "KeyValue.h"
#include "LED.h"
#include "Lora.h"
#include "LoraNetwork.h"
#include "OTA.h"
#include "Sensor.h"
#include "sdkconfig.h"

static const char *TAG = "MAIN";

#define OTA_TRIGGER_WINDOW_US 10000000LL
#define OTA_TRIGGER_DEBOUNCE_US 100000LL
#define LORA_RECEIVER_STARTUP_TIMEOUT_MS 5000

QueueHandle_t sensorInterruptQueue;
QueueHandle_t buzzerQueue;
EventGroupHandle_t measurementStartupEvents;

TaskHandle_t sensorInterruptTaskHandle;

static int64_t ota_trigger_deadline_us;

int station_id = 0;
int controller_id = 0;
int start_id = 0;
int stop_id = 0;
int is_xrl = 0;
int num_fake_sensors = 0;
int num_sensors_required_for_trigger = 0;
int num_sensors = 0;
int triggerLevel = 0;
const int *sensorPins = NULL;

static const int xlr_sensor_pins[] = {GPIO_NUM_47};
static const int standard_sensor_pins[] = {
    GPIO_NUM_15,
    GPIO_NUM_16,
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_8,
    GPIO_NUM_19,
    GPIO_NUM_20,
    GPIO_NUM_39,
    GPIO_NUM_38,
    GPIO_NUM_37,
};

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS needs recovery; erasing the NVS partition");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static int load_station_id(const char *key, int default_value)
{
    int32_t stored_value = 0;
    esp_err_t err = getValue(key, &stored_value);
    if (err == ESP_OK && stored_value >= 0 && stored_value <= UINT8_MAX)
    {
        return (int)stored_value;
    }

    if (err == ESP_OK)
    {
        ESP_LOGW(TAG, "Ignoring out-of-range value %" PRId32 " for NVS key '%s'", stored_value, key);
    }
    else if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Could not read NVS key '%s': %s", key, esp_err_to_name(err));
    }

    err = storeValue(key, default_value);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Could not persist default for NVS key '%s': %s", key, esp_err_to_name(err));
    }
    return default_value;
}

static int load_bounded_setting(const char *key, int minimum, int maximum,
                                int default_value)
{
    int32_t stored_value = 0;
    esp_err_t err = getValue(key, &stored_value);
    if (err == ESP_OK && stored_value >= minimum && stored_value <= maximum)
    {
        return (int)stored_value;
    }

    if (err == ESP_OK)
    {
        ESP_LOGW(TAG, "Ignoring out-of-range value %" PRId32 " for NVS key '%s'",
                 stored_value, key);
    }
    else if (err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Could not read NVS key '%s': %s", key, esp_err_to_name(err));
    }

    err = storeValue(key, default_value);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Could not persist default for NVS key '%s': %s",
                 key, esp_err_to_name(err));
    }
    return default_value;
}

static void load_sensor_hardware_configuration(void)
{
    // Keep the historical key spelling for compatibility with already
    // provisioned standard and XLR stations.
    is_xrl = load_bounded_setting("is_xrl", 0, 1, 0);
    num_fake_sensors = load_bounded_setting(
        "num_fake_s", 1, 64, CONFIG_NUM_FAKE_SENSORS);
    num_sensors_required_for_trigger = load_bounded_setting(
        "num_s_req", 1, 10, CONFIG_NUM_SENSORS_REQUIRED_FOR_TRIGGER);

    if (is_xrl)
    {
        triggerLevel = 1;
        num_sensors = (int)(sizeof(xlr_sensor_pins) / sizeof(xlr_sensor_pins[0]));
        sensorPins = xlr_sensor_pins;
        ESP_LOGI(TAG, "Using XLR input with %d virtual sensors", num_fake_sensors);
    }
    else
    {
        triggerLevel = 0;
        num_sensors = (int)(sizeof(standard_sensor_pins) /
                            sizeof(standard_sensor_pins[0]));
        sensorPins = standard_sensor_pins;
        ESP_LOGI(TAG, "Using %d standard inputs; %d required for a trigger",
                 num_sensors, num_sensors_required_for_trigger);
    }
}

static bool station_ids_are_valid(int controller, int start, int stop, int station)
{
    return controller != start && controller != stop && start != stop &&
           (station == start || station == stop);
}

static void require_task_created(BaseType_t result, const char *task_name)
{
    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create critical task '%s'", task_name);
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}

static void measurement_ota_status(dd_ota_state_t state, esp_err_t error, void *context)
{
    (void)context;
    if (state == DD_OTA_FAILED)
    {
        ESP_LOGE("OTA", "Firmware upgrade failed: %s", esp_err_to_name(error));
    }
    else if (state == DD_OTA_SUCCEEDED)
    {
        ESP_LOGI("OTA", "Firmware upgrade complete; restarting");
    }
}

static void start_measurement_ota_mode(void)
{
    buzzerQueue = xQueueCreate(4, sizeof(int));
    if (buzzerQueue != NULL &&
        xTaskCreate(Buzzer_Task, "Buzzer_Task", 8192, NULL, 12, NULL) == pdPASS)
    {
        const int indication = Buzzer_INDICATE_OTA;
        (void)xQueueSend(buzzerQueue, &indication, 0);
    }
    else
    {
        ESP_LOGW("OTA", "OTA buzzer indication is unavailable");
    }

    const dd_ota_options_t options = {
        .status_cb = measurement_ota_status,
        .context = NULL,
    };
    esp_err_t err = dd_ota_start(&options);
    if (err != ESP_OK)
    {
        ESP_LOGE("OTA", "Could not start OTA worker: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    vTaskDelete(NULL);
}

// A debounced press at any point during the first ten seconds requests a clean
// OTA-only reboot. Normal station initialization continues in parallel.
static void ota_check_task(void *params)
{
    (void)params;

    int64_t pressed_since_us = 0;
    while (esp_timer_get_time() < ota_trigger_deadline_us)
    {
        if (gpio_get_level(GPIO_NUM_0) == 0)
        {
            const int64_t now_us = esp_timer_get_time();
            if (pressed_since_us == 0)
            {
                pressed_since_us = now_us;
            }
            else if (now_us - pressed_since_us >= OTA_TRIGGER_DEBOUNCE_US)
            {
                ESP_LOGI("OTA_CHECK", "Boot button pressed; requesting firmware upgrade");
                const int indication = Buzzer_INDICATE_OTA;
                if (buzzerQueue == NULL ||
                    xQueueSend(buzzerQueue, &indication, pdMS_TO_TICKS(100)) != pdPASS)
                {
                    ESP_LOGW("OTA_CHECK", "Buzzer queue full; OTA indication was skipped");
                }

                // GPIO0 is a boot strap. Require a continuously stable release
                // before the software reset so contact bounce cannot select the
                // ROM downloader on the following boot.
                int64_t released_since_us = -1;
                while (true)
                {
                    const int64_t release_now_us = esp_timer_get_time();
                    if (gpio_get_level(GPIO_NUM_0) != 0)
                    {
                        if (released_since_us < 0)
                        {
                            released_since_us = release_now_us;
                        }
                        else if (release_now_us - released_since_us >=
                                 OTA_TRIGGER_DEBOUNCE_US)
                        {
                            break;
                        }
                    }
                    else
                    {
                        released_since_us = -1;
                    }
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                dd_ota_request_reboot();
            }
        }
        else
        {
            pressed_since_us = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI("OTA_CHECK", "Firmware upgrade trigger window closed");
    vTaskDelete(NULL);
}

void app_main(void)
{
    if (dd_ota_consume_reboot_request())
    {
        ESP_LOGI(TAG, "Starting clean firmware upgrade boot");
        start_measurement_ota_mode();
        return;
    }

    ota_trigger_deadline_us = esp_timer_get_time() + OTA_TRIGGER_WINDOW_US;

    // Start the gesture monitor before NVS, LoRa, LEDs, or sensor setup so a
    // short BOOT press near the beginning of the ten-second window is latched.
    const gpio_config_t ota_button_config = {
        .pin_bit_mask = BIT64(GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&ota_button_config));
    require_task_created(
        xTaskCreate(ota_check_task, "ota_check_task", 4096, NULL, 5, NULL),
        "ota_check_task");

    // Bring up the acknowledgement path immediately after the monitor. The
    // monitor has a 100 ms debounce period, so this is ready before a valid
    // gesture can complete without delaying normal initialization.
    buzzerQueue = xQueueCreate(8, sizeof(int));
    if (buzzerQueue == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buzzer queue");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    require_task_created(
        xTaskCreate(Buzzer_Task, "Buzzer_Task", 8192, NULL, 12, NULL),
        "Buzzer_Task");

    ESP_LOGI(TAG, "Starting...");
    initialize_nvs();

    controller_id = load_station_id("controller_id", CONFIG_LORA_CONTROLLER_ID);
    start_id = load_station_id("start_id", CONFIG_START_LORA_ID);
    stop_id = load_station_id("stop_id", CONFIG_STOP_LORA_ID);
    station_id = load_station_id("station_id", CONFIG_LORA_STATION_ID);

    if (!station_ids_are_valid(controller_id, start_id, stop_id, station_id))
    {
        ESP_LOGW(TAG, "Stored LoRa IDs are inconsistent; restoring build defaults");
        controller_id = CONFIG_LORA_CONTROLLER_ID;
        start_id = CONFIG_START_LORA_ID;
        stop_id = CONFIG_STOP_LORA_ID;
        station_id = CONFIG_LORA_STATION_ID;

        if (!station_ids_are_valid(controller_id, start_id, stop_id, station_id))
        {
            ESP_LOGE(TAG, "Build-time LoRa IDs must be distinct and the station ID must identify start or stop");
            ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
        }

        storeValue("controller_id", controller_id);
        storeValue("start_id", start_id);
        storeValue("stop_id", stop_id);
        storeValue("station_id", station_id);
    }

    load_sensor_hardware_configuration();

    sensorInterruptQueue = xQueueCreate(32, sizeof(PinTrigger));
    measurementStartupEvents = xEventGroupCreate();
    if (sensorInterruptQueue == NULL || measurementStartupEvents == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate application queues");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    init_led(num_sensors);

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(err);
    }

    InitLoraHandlers(HandleReceivedPacket);
    ESP_ERROR_CHECK(init_lora());

    set_all_leds(255, 0, 255);

    require_task_created(
        xTaskCreatePinnedToCore(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 8192 * 2, NULL, 3,
                                &sensorInterruptTaskHandle, 0),
        "Sensor_Interrupt_Task");
    require_task_created(xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 24, NULL), "LoraSendTask");
    require_task_created(
        xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048,
                    xTaskGetCurrentTaskHandle(), 12, NULL),
        "LoraReceiveTask");

    uint32_t receive_status = (uint32_t)ESP_FAIL;
    if (xTaskNotifyWait(0, UINT32_MAX, &receive_status,
                        pdMS_TO_TICKS(LORA_RECEIVER_STARTUP_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGE(TAG, "LoRa receive task did not report readiness; keeping the image pending for rollback");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    if ((esp_err_t)receive_status != ESP_OK)
    {
        ESP_LOGE(TAG, "LoRa receive task failed to start: %s; keeping the image pending for rollback",
                 esp_err_to_name((esp_err_t)receive_status));
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    // Do not cancel ESP-IDF rollback merely because task creation succeeded.
    // The sensor task reports ready only after its queue, GPIO configuration,
    // and interrupt handlers are live. Time synchronization is intentionally
    // not part of local boot readiness because it depends on another device.
    const EventBits_t ready_bits = xEventGroupWaitBits(
        measurementStartupEvents,
        MEASUREMENT_SENSOR_READY,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(10000));
    if ((ready_bits & MEASUREMENT_SENSOR_READY) == 0)
    {
        ESP_LOGE(TAG, "Sensor subsystem did not become ready; keeping the image pending for rollback");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    const int buzzer_type = BUZZER_STARTUP;
    if (xQueueSend(buzzerQueue, &buzzer_type, pdMS_TO_TICKS(100)) != pdPASS)
    {
        ESP_LOGW(TAG, "Buzzer queue full; startup indication was skipped");
    }

    esp_err_t valid_err = dd_ota_mark_app_valid();
    if (valid_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not mark running firmware valid: %s; restarting for rollback",
                 esp_err_to_name(valid_err));
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
    }
}
