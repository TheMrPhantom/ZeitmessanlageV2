#include "Sensor.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "Buzzer.h"
#include "LED.h"
#include "LoraNetwork.h"

static const char *TAG = "SENSOR";
static const uint32_t SENSOR_COOLDOWN_MS = 1500;
static const uint32_t FAULT_COOLDOWN_MS = 3000;
static const uint32_t FAULT_CHECK_START_DELAY_MS = 4000;

extern QueueHandle_t sensorInterruptQueue;
extern QueueHandle_t buzzerQueue;

extern int is_xrl;
extern int num_fake_sensors;
extern int num_sensors_required_for_trigger;
extern int num_sensors;
extern int triggerLevel;
extern const int *sensorPins;

extern int64_t time_offset_to_controller;
extern portMUX_TYPE timesync_spinlock;

static QueueHandle_t sensorStatusQueue;
static volatile uint32_t dropped_sensor_interrupts;

static bool fault_warning;
static bool sensor_fault;
static TickType_t fault_started_at;

static portMUX_TYPE release_time_spinlock = portMUX_INITIALIZER_UNLOCKED;
static int64_t last_release_timestamp;
static int64_t last_release_event_time_us;
static int64_t last_trigger_event_time_us;
static bool last_release_event_recorded;
static bool last_trigger_event_valid;
static bool last_release_timestamp_valid;

static portMUX_TYPE last_send_spinlock = portMUX_INITIALIZER_UNLOCKED;
static int64_t last_send_time_us;

static void free_dogdog_packet(DogDogPacket *packet)
{
    if (packet != NULL)
    {
        free(packet->payload);
        free(packet);
    }
}

// On success ownership transfers to lora-network. On failure it remains ours.
static bool queue_dogdog_packet(DogDogPacket *packet)
{
    if (packet == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate LoRa packet");
        return false;
    }

    if (send_dogdog_packet(packet) != pdPASS)
    {
        ESP_LOGW(TAG, "LoRa send queue full; dropping packet type %u", packet->type);
        free_dogdog_packet(packet);
        return false;
    }
    return true;
}

static void mark_packet_sent(int64_t now_us)
{
    taskENTER_CRITICAL(&last_send_spinlock);
    last_send_time_us = now_us;
    taskEXIT_CRITICAL(&last_send_spinlock);
}

static int64_t get_last_send_time(void)
{
    taskENTER_CRITICAL(&last_send_spinlock);
    const int64_t result = last_send_time_us;
    taskEXIT_CRITICAL(&last_send_spinlock);
    return result;
}

static void set_last_release_timestamp(int64_t timestamp, int64_t event_time_us)
{
    taskENTER_CRITICAL(&release_time_spinlock);
    if (!last_release_event_recorded || event_time_us >= last_release_event_time_us)
    {
        last_release_timestamp = timestamp;
        last_release_event_time_us = event_time_us;
        last_release_event_recorded = true;
        last_release_timestamp_valid =
            last_trigger_event_valid && event_time_us > last_trigger_event_time_us;
    }
    taskEXIT_CRITICAL(&release_time_spinlock);
}

static void mark_trigger_event(int64_t event_time_us)
{
    taskENTER_CRITICAL(&release_time_spinlock);
    if (event_time_us >= last_trigger_event_time_us)
    {
        last_trigger_event_time_us = event_time_us;
        last_trigger_event_valid = true;
        // A release may already have been observed by the status task running on
        // the other core. Keep it if it is strictly newer than this trigger;
        // otherwise invalidate stale state from an earlier measurement.
        last_release_timestamp_valid =
            last_release_event_recorded && last_release_event_time_us > event_time_us;
    }
    taskEXIT_CRITICAL(&release_time_spinlock);
}

bool get_last_release_timestamp(int64_t *timestamp)
{
    if (timestamp == NULL)
    {
        return false;
    }

    taskENTER_CRITICAL(&release_time_spinlock);
    const bool valid = last_release_timestamp_valid;
    *timestamp = last_release_timestamp;
    taskEXIT_CRITICAL(&release_time_spinlock);

    if (!valid || sensorPins == NULL)
    {
        return false;
    }
    for (int i = 0; i < num_sensors; i++)
    {
        if (gpio_get_level(sensorPins[i]) == triggerLevel)
        {
            // At least one input is still active, so this cannot yet be the
            // final release for the current measurement.
            return false;
        }
    }
    return true;
}

static bool get_event_timestamp(int64_t event_time_us, int64_t *timestamp)
{
    if (timestamp == NULL)
    {
        return false;
    }

    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    int64_t offset;
    taskENTER_CRITICAL(&timesync_spinlock);
    offset = time_offset_to_controller;
    taskEXIT_CRITICAL(&timesync_spinlock);

    const int64_t monotonic_now_us = esp_timer_get_time();
    int64_t latency_us;
    int64_t adjusted_now_us;
    if (__builtin_sub_overflow(monotonic_now_us, event_time_us, &latency_us) || latency_us < 0 ||
        __builtin_add_overflow(TIME_US(current_time), offset, &adjusted_now_us) ||
        __builtin_sub_overflow(adjusted_now_us, latency_us, timestamp))
    {
        return false;
    }
    return true;
}

static void send_buzzer_event(int event)
{
    if (xQueueSend(buzzerQueue, &event, pdMS_TO_TICKS(100)) != pdPASS)
    {
        ESP_LOGW(TAG, "Buzzer queue full; event %d was dropped", event);
    }
}

static void IRAM_ATTR gpio_interrupt_handler(void *args)
{
    const int pin_number = (int)(intptr_t)args;
    PinTrigger trigger = {
        .pin = pin_number,
        .state = gpio_get_level(pin_number),
        .triggered_at_us = esp_timer_get_time(),
    };

    BaseType_t higher_priority_task_woken = pdFALSE;
    if (xQueueSendFromISR(sensorInterruptQueue, &trigger, &higher_priority_task_woken) != pdPASS)
    {
        __atomic_fetch_add(&dropped_sensor_interrupts, 1, __ATOMIC_RELAXED);
    }

    // This queue is deliberately length one: status work is coalesced while the
    // latest GPIO levels are read by the consumer.
    xQueueOverwriteFromISR(sensorStatusQueue, &trigger, &higher_priority_task_woken);

    if (higher_priority_task_woken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

esp_err_t init_Pins(void)
{
    if (sensorPins == NULL || num_sensors <= 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t pin_mask = 0;
    for (int i = 0; i < num_sensors; i++)
    {
        if (!GPIO_IS_VALID_GPIO(sensorPins[i]))
        {
            ESP_LOGE(TAG, "Invalid sensor GPIO %d", sensorPins[i]);
            return ESP_ERR_INVALID_ARG;
        }
        pin_mask |= BIT64(sensorPins[i]);
    }

    const gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK)
    {
        return err;
    }

    for (int i = 0; i < num_sensors; i++)
    {
        err = gpio_isr_handler_add(sensorPins[i], gpio_interrupt_handler, (void *)(intptr_t)sensorPins[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add ISR for GPIO %d: %s", sensorPins[i], esp_err_to_name(err));
            for (int added = 0; added < i; added++)
            {
                gpio_isr_handler_remove(sensorPins[added]);
            }
            return err;
        }
    }

    return ESP_OK;
}

void Sensor_Interrupt_Task(void *params)
{
    (void)params;
    ESP_LOGI(TAG, "Setting up sensors");

    // The queue must exist before handlers are registered: an edge can arrive
    // immediately after gpio_isr_handler_add().
    sensorStatusQueue = xQueueCreate(1, sizeof(PinTrigger));
    if (sensorStatusQueue == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate sensor status queue");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    ESP_ERROR_CHECK(init_Pins());
    PinTrigger pin_trigger = {0};

    ESP_LOGI(TAG, "Waiting for time sync...");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Time synced");

    // Edges queued before time synchronization have no trustworthy controller
    // timestamp and may be arbitrarily old. Start measurement from a clean queue.
    while (xQueueReceive(sensorInterruptQueue, &pin_trigger, 0) == pdPASS)
    {
    }
    xQueueReset(sensorStatusQueue);
    __atomic_store_n(&dropped_sensor_interrupts, 0, __ATOMIC_RELAXED);

    if (xTaskCreate(Sensor_Status_Task, "Sensor_Status_Task", 4096, NULL, 1, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create sensor status task");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    const TickType_t cooldown_ticks = pdMS_TO_TICKS(SENSOR_COOLDOWN_MS);
    TickType_t last_trigger_tick = xTaskGetTickCount() - cooldown_ticks;
    const TickType_t task_started_at = xTaskGetTickCount();
    uint64_t observed_active_states = 0;
    for (int i = 0; i < num_sensors; i++)
    {
        if (gpio_get_level(sensorPins[i]) == triggerLevel)
        {
            observed_active_states |= 1ULL << i;
        }
    }

    while (true)
    {
        const BaseType_t received = xQueueReceive(sensorInterruptQueue, &pin_trigger, pdMS_TO_TICKS(500));

        const uint32_t dropped = __atomic_exchange_n(&dropped_sensor_interrupts, 0, __ATOMIC_RELAXED);
        if (dropped > 0)
        {
            ESP_LOGW(TAG, "Dropped %" PRIu32 " sensor interrupts because the queue was full", dropped);
            // The queued edge order is no longer complete, so it cannot safely
            // establish which edge crossed the trigger threshold. Discard the
            // backlog and rebuild the observed state from the GPIOs.
            xQueueReset(sensorInterruptQueue);
            observed_active_states = 0;
            for (int i = 0; i < num_sensors; i++)
            {
                if (gpio_get_level(sensorPins[i]) == triggerLevel)
                {
                    observed_active_states |= 1ULL << i;
                }
            }
        }

        if (received == pdPASS)
        {
            int sensor_index = -1;
            for (int i = 0; i < num_sensors; i++)
            {
                if (sensorPins[i] == pin_trigger.pin)
                {
                    sensor_index = i;
                    break;
                }
            }

            int64_t event_timestamp;
            const bool timestamp_valid = get_event_timestamp(pin_trigger.triggered_at_us, &event_timestamp);

            if (timestamp_valid && pin_trigger.state != triggerLevel)
            {
                set_last_release_timestamp(event_timestamp, pin_trigger.triggered_at_us);
            }

            if (dropped == 0 && sensor_index >= 0)
            {
                if (pin_trigger.state == triggerLevel)
                {
                    observed_active_states |= 1ULL << sensor_index;
                }
                else
                {
                    observed_active_states &= ~(1ULL << sensor_index);
                }
            }

            // Confirm that a short bounce did not disappear while queued.
            if (!timestamp_valid)
            {
                ESP_LOGW(TAG, "Discarding sensor edge with invalid timestamp");
            }
            else if (sensor_index < 0)
            {
                ESP_LOGW(TAG, "Ignoring edge from unknown sensor GPIO %d", pin_trigger.pin);
            }
            else if (dropped == 0 && pin_trigger.state == triggerLevel &&
                     gpio_get_level(pin_trigger.pin) == triggerLevel)
            {
                const TickType_t now_tick = xTaskGetTickCount();
                if ((TickType_t)(now_tick - last_trigger_tick) >= cooldown_ticks && !sensor_fault)
                {
                    const int num_triggered_sensors = __builtin_popcountll(observed_active_states);

                    if (is_xrl || num_triggered_sensors >= num_sensors_required_for_trigger)
                    {
                        PacketTypeTrigger trigger_packet = {
                            .timestamp = event_timestamp,
                            .sensor_state = {
                                .num_sensors = (uint8_t)num_sensors,
                                .sensor_states = 0,
                            },
                        };

                        for (int i = 0; i < num_sensors; i++)
                        {
                            const uint64_t level = gpio_get_level(sensorPins[i]) == triggerLevel ? 1ULL : 0ULL;
                            trigger_packet.sensor_state.sensor_states |= level << i;
                        }

                        if (is_xrl)
                        {
                            trigger_packet.sensor_state.num_sensors = (uint8_t)num_fake_sensors;
                            for (int i = 1; i < num_fake_sensors; i++)
                            {
                                trigger_packet.sensor_state.sensor_states |=
                                    (trigger_packet.sensor_state.sensor_states & 1ULL) << i;
                            }
                        }

                        trigger_packet.sensor_state.sensor_states = ~trigger_packet.sensor_state.sensor_states;

                        DogDogPacket *packet = create_dogdog_packet_from_trigger_information(&trigger_packet);
                        if (queue_dogdog_packet(packet))
                        {
                            last_trigger_tick = now_tick;
                            mark_trigger_event(pin_trigger.triggered_at_us);
                            struct timeval now;
                            gettimeofday(&now, NULL);
                            mark_packet_sent(TIME_US(now));
                        }
                        send_buzzer_event(Buzzer_TRIGGER);
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Not enough sensors triggered (%d/%d)", num_triggered_sensors,
                                 num_sensors_required_for_trigger);
                    }
                }
            }
        }

        const TickType_t now_tick = xTaskGetTickCount();
        if ((TickType_t)(now_tick - task_started_at) < pdMS_TO_TICKS(FAULT_CHECK_START_DELAY_MS))
        {
            continue;
        }

        int current_faults = 0;
        for (int i = 0; i < num_sensors; i++)
        {
            current_faults += gpio_get_level(sensorPins[i]) == triggerLevel ? 1 : 0;
        }
        const bool sensors_good = current_faults == 0;

        if (!fault_warning && !sensors_good)
        {
            ESP_LOGW(TAG, "Sensor connection warning started (%d active fault inputs)", current_faults);
            fault_started_at = now_tick;
            fault_warning = true;
        }

        if (fault_warning && !sensor_fault &&
            (TickType_t)(now_tick - fault_started_at) >= pdMS_TO_TICKS(FAULT_COOLDOWN_MS))
        {
            sensor_fault = true;
            send_buzzer_event(Buzzer_ERROR_START);
            ESP_LOGE(TAG, "Sensor connection is lost");
        }

        if (sensors_good)
        {
            if (sensor_fault)
            {
                send_buzzer_event(Buzzer_ERROR_STOP);
                ESP_LOGI(TAG, "Sensor connection restored");
            }
            fault_warning = false;
            sensor_fault = false;
        }
    }
}

void Sensor_Status_Task(void *params)
{
    (void)params;

    int64_t last_clean_time_us[num_sensors];
    struct timeval now;
    gettimeofday(&now, NULL);
    const int64_t initial_time_us = TIME_US(now);
    mark_packet_sent(initial_time_us);

    for (int i = 0; i < num_sensors; i++)
    {
        last_clean_time_us[i] = initial_time_us;
    }

    const PinTrigger initial_notification = {.pin = -1};
    xQueueOverwrite(sensorStatusQueue, &initial_notification);

    while (true)
    {
        PinTrigger status_trigger;
        const TickType_t wait_ticks = pdMS_TO_TICKS(5000 - (esp_random() % 500));
        const BaseType_t new_data_received = xQueueReceive(sensorStatusQueue, &status_trigger, wait_ticks);

        // The overwrite queue preserves the newest edge even when the primary
        // interrupt queue overflows during a burst. This keeps final release
        // time accurate for the controller's later request.
        if (new_data_received == pdPASS && status_trigger.pin >= 0 && status_trigger.state != triggerLevel)
        {
            int64_t release_timestamp;
            if (get_event_timestamp(status_trigger.triggered_at_us, &release_timestamp))
            {
                set_last_release_timestamp(release_timestamp, status_trigger.triggered_at_us);
            }
        }

        gettimeofday(&now, NULL);
        const int64_t now_us = TIME_US(now);

        PacketTypeSensorState sensor_state = {
            .num_sensors = (uint8_t)num_sensors,
            .sensor_states = 0,
        };

        for (int i = 0; i < num_sensors; i++)
        {
            const bool active = gpio_get_level(sensorPins[i]) == triggerLevel;
            sensor_state.sensor_states |= (uint64_t)active << i;

            if (active)
            {
                last_clean_time_us[i] = now_us;
                set_led(i, 255, 0, 0);
            }
            else if (now_us - last_clean_time_us[i] > (is_xrl ? 300000000LL : 8000000LL))
            {
                set_led(i, 0, 0, 0);
            }
            else
            {
                set_led(i, 0, 255, 0);
            }
        }

        const int64_t randomized_send_interval_us = 4500000LL - (esp_random() % 500000);
        if (new_data_received != pdPASS || now_us - get_last_send_time() > randomized_send_interval_us)
        {
            if (is_xrl)
            {
                sensor_state.num_sensors = (uint8_t)num_fake_sensors;
                for (int i = 1; i < num_fake_sensors; i++)
                {
                    sensor_state.sensor_states |= (sensor_state.sensor_states & 1ULL) << i;
                }
            }

            sensor_state.sensor_states = ~sensor_state.sensor_states;
            DogDogPacket *packet = create_dogdog_packet_from_sensor_state_information(&sensor_state);
            if (queue_dogdog_packet(packet))
            {
                mark_packet_sent(now_us);
            }
        }
    }
}
