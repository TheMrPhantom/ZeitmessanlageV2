#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
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

QueueHandle_t sensorInterruptQueue;
QueueHandle_t buzzerQueue;

TaskHandle_t sensorInterruptTaskHandle;

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

#ifdef CONFIG_IS_XLR
static const int xlr_sensor_pins[] = {GPIO_NUM_47};
#else
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
#endif

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

// Checks during the first ten seconds whether the boot button is held long
// enough to request OTA mode.
static void ota_check_task(void *params)
{
    (void)params;

    const gpio_config_t button_config = {
        .pin_bit_mask = BIT64(GPIO_NUM_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&button_config);
    if (err != ESP_OK)
    {
        ESP_LOGE("OTA_CHECK", "Failed to configure boot button: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    int pressed_count = 0;
    for (int i = 0; i < 100; i++)
    {
        if (gpio_get_level(GPIO_NUM_0) == 0)
        {
            pressed_count++;
        }
        else
        {
            pressed_count = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (pressed_count >= 50)
    {
        ESP_LOGI("OTA_CHECK", "Boot button held for 5 seconds, entering OTA mode");
        const int indication = Buzzer_INDICATE_OTA;
        if (xQueueSend(buzzerQueue, &indication, pdMS_TO_TICKS(100)) != pdPASS)
        {
            ESP_LOGW("OTA_CHECK", "Buzzer queue full; OTA indication was skipped");
        }

        if (xTaskCreate(ota_task, "ota_task", 16384, NULL, 5, NULL) != pdPASS)
        {
            ESP_LOGE("OTA_CHECK", "Failed to create OTA task");
        }
    }
    else
    {
        ESP_LOGI("OTA_CHECK", "Boot button not held long enough, starting normally");
    }

    vTaskDelete(NULL);
}

void app_main(void)
{
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

#ifdef CONFIG_IS_XLR
    is_xrl = 1;
    num_fake_sensors = CONFIG_NUM_FAKE_SENSORS;
    num_sensors_required_for_trigger = 1;
    triggerLevel = 1;
    num_sensors = 1;
    sensorPins = xlr_sensor_pins;
#else
    is_xrl = 0;
    num_fake_sensors = 0;
    num_sensors_required_for_trigger = CONFIG_NUM_SENSORS_REQUIRED_FOR_TRIGGER;
    triggerLevel = 0;
    num_sensors = sizeof(standard_sensor_pins) / sizeof(standard_sensor_pins[0]);
    sensorPins = standard_sensor_pins;
#endif

    // Keep diagnostic values in NVS, but make the hardware build configuration
    // authoritative so stale NVS from another board type cannot select bad pins.
    const esp_err_t store_xlr_err = storeValue("is_xrl", is_xrl);
    const esp_err_t store_fake_err = storeValue("num_fake_s", num_fake_sensors);
    const esp_err_t store_required_err = storeValue("num_s_req", num_sensors_required_for_trigger);
    if (store_xlr_err != ESP_OK || store_fake_err != ESP_OK || store_required_err != ESP_OK)
    {
        ESP_LOGW(TAG, "One or more sensor configuration values could not be persisted");
    }

    sensorInterruptQueue = xQueueCreate(32, sizeof(PinTrigger));
    buzzerQueue = xQueueCreate(8, sizeof(int));
    if (sensorInterruptQueue == NULL || buzzerQueue == NULL)
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

    require_task_created(xTaskCreate(Buzzer_Task, "Buzzer_Task", 8192, NULL, 12, NULL), "Buzzer_Task");
    require_task_created(xTaskCreate(ota_check_task, "ota_check_task", 4096, NULL, 5, NULL), "ota_check_task");

    InitLoraHandlers(HandleReceivedPacket);
    ESP_ERROR_CHECK(init_lora());

    set_all_leds(255, 0, 255);

    require_task_created(
        xTaskCreatePinnedToCore(Sensor_Interrupt_Task, "Sensor_Interrupt_Task", 8192 * 2, NULL, 3,
                                &sensorInterruptTaskHandle, 0),
        "Sensor_Interrupt_Task");
    require_task_created(xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 24, NULL), "LoraSendTask");
    require_task_created(xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048, NULL, 12, NULL), "LoraReceiveTask");

    const int buzzer_type = BUZZER_STARTUP;
    if (xQueueSend(buzzerQueue, &buzzer_type, pdMS_TO_TICKS(100)) != pdPASS)
    {
        ESP_LOGW(TAG, "Buzzer queue full; startup indication was skipped");
    }
}
