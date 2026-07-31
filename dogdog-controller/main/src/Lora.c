#include "Lora.h"
#include "SevenSegment.h"
#include "Timer.h"
#include "NetworkFault.h"
#include "Startup.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern QueueHandle_t loraSendQueue;
extern QueueHandle_t localReceiveTimestampQueue;
extern QueueHandle_t ackQueue;
extern QueueHandle_t sevenSegmentQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t networkFaultQueue;

extern int64_t time_offset_to_controller;

extern int64_t timesync_timestamp_current;
extern int64_t timesync_current_time;
extern int64_t timesync_received_time;
extern int64_t timesync_processing_time;
extern int64_t time_of_controller;
extern portMUX_TYPE timesync_spinlock;

extern int controller_id;
extern int start_id;
extern int stop_id;
extern int station_id;

#define MAX_SENSOR_STATUS_BITS 64
#define LORA_QUEUE_TIMEOUT_MS 100
#define DUPLICATE_WINDOW_MS 15000
#define LORA_RECEIVER_STARTUP_TIMEOUT_MS 5000

typedef struct
{
    bool valid;
    uint8_t packet_id;
    TickType_t accepted_at;
} DuplicateEntry;

/* Keep trigger and final-time IDs separate for each station.  Packet IDs are
   only eight bits and advance for every outbound packet, so an ID remembered
   indefinitely would eventually reject a legitimate event after wraparound. */
static DuplicateEntry duplicate_entries[2][2];

static void free_dogdog_packet(DogDogPacket *packet)
{
    if (packet)
    {
        free(packet->payload);
        free(packet);
    }
}

static bool has_payload_length(const DogDogPacket *packet, size_t expected_length)
{
    if (!packet || packet->payload_length != expected_length ||
        (expected_length > 0 && !packet->payload))
    {
        ESP_LOGW("Lora", "Dropping malformed packet payload (type=%u, length=%u, expected=%u)",
                 packet ? packet->type : 0,
                 packet ? (unsigned int)packet->payload_length : 0,
                 (unsigned int)expected_length);
        return false;
    }
    return true;
}

static bool is_measurement_station(const DogDogPacket *packet)
{
    if (packet->station_id != start_id && packet->station_id != stop_id)
    {
        ESP_LOGW("Lora", "Dropping measurement packet from station %u", packet->station_id);
        return false;
    }
    return true;
}

static bool enqueue_owned_packet(DogDogPacket *packet)
{
    if (!packet)
    {
        return false;
    }

    if (!loraSendQueue ||
        xQueueSend(loraSendQueue, &packet, pdMS_TO_TICKS(LORA_QUEUE_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGW("Lora", "LoRa send queue unavailable; dropping packet type %u", packet->type);
        free_dogdog_packet(packet);
        return false;
    }
    return true;
}

static bool enqueue_trigger_or_duplicate(const DogDogPacket *packet, const TimerTrigger *trigger)
{
    const size_t station_index = packet->station_id == start_id ? 0U : 1U;
    const size_t type_index = packet->type == LORA_TRIGGER ? 0U : 1U;
    DuplicateEntry *entry = &duplicate_entries[station_index][type_index];
    const TickType_t now = xTaskGetTickCount();
    const TickType_t duplicate_window = pdMS_TO_TICKS(DUPLICATE_WINDOW_MS);

    if (entry->valid && entry->packet_id == packet->packet_id &&
        (TickType_t)(now - entry->accepted_at) <= duplicate_window)
    {
        ESP_LOGW("Lora", "Duplicate packet ignored for station %u type %u packet %u",
                 packet->station_id, packet->type, packet->packet_id);
        return true; /* ACK duplicates so the sender can stop retrying. */
    }

    if (xQueueSend(triggerQueue, trigger, pdMS_TO_TICKS(LORA_QUEUE_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGW("Lora", "Timer trigger queue full for station %u packet %u",
                 packet->station_id, packet->packet_id);
        return false;
    }

    /* Record the ID only after the event is safely queued. */
    entry->packet_id = packet->packet_id;
    entry->accepted_at = now;
    entry->valid = true;
    return true;
}

static bool create_sensor_status(DisplaySensorStatus *status, const PacketTypeSensorState *sensor_state,
                                 uint8_t packet_station_id, bool is_trigger)
{
    if (sensor_state->num_sensors > MAX_SENSOR_STATUS_BITS)
    {
        ESP_LOGW("Lora", "Invalid sensor count: %u", sensor_state->num_sensors);
        return false;
    }

    memset(status, 0, sizeof(*status));
    status->sensor = packet_station_id == start_id ? SENSOR_START : SENSOR_STOP;
    status->num_sensors = sensor_state->num_sensors > DISPLAY_MAX_SENSOR_COUNT
                              ? DISPLAY_MAX_SENSOR_COUNT
                              : sensor_state->num_sensors;
    status->is_trigger = is_trigger;

    if (sensor_state->num_sensors > DISPLAY_MAX_SENSOR_COUNT)
    {
        ESP_LOGW("Lora", "Displaying only the first %d of %u sensors",
                 DISPLAY_MAX_SENSOR_COUNT, sensor_state->num_sensors);
    }

    for (int i = 0; i < status->num_sensors; i++)
    {
        status->status[i] = (sensor_state->sensor_states & (1ULL << i)) != 0;
    }
    return true;
}

static void send_sensor_status_to_display(const PacketTypeSensorState *sensor_state,
                                          uint8_t packet_station_id, bool is_trigger)
{
    SevenSegmentDisplay to_send = {
        .type = SEVEN_SEGMENT_SENSOR_STATUS,
    };
    if (!create_sensor_status(&to_send.sensorStatus, sensor_state, packet_station_id, is_trigger))
    {
        return;
    }

    if (xQueueSend(sevenSegmentQueue, &to_send, 0) != pdTRUE)
    {
        ESP_LOGW("Lora", "Display queue full; dropping sensor status");
    }
}

static void send_ack_for_packet(const DogDogPacket *packet)
{
    PacketTypeAck ack = {
        .station_id = packet->station_id,
        .packet_id = packet->packet_id,
    };
    DogDogPacket *ack_packet = create_dogdog_packet_from_ack_information(&ack);
    if (!ack_packet)
    {
        ESP_LOGE("Lora", "Failed to allocate DogDogPacket for ACK");
        return;
    }

    /* LoRaSend returns the measurement station to RX mode before RX_DONE is
       dispatched here, so an additional one-second turnaround delay only
       blocks reception and can make retransmissions pile up. */
    enqueue_owned_packet(ack_packet);
}

void LoraStartupTask(void *pvParameters)
{
    (void)pvParameters;

    esp_err_t err = init_lora();
    if (err != ESP_OK)
    {
        ESP_LOGE(pcTaskGetName(NULL), "LoRa initialization failed: %s", esp_err_to_name(err));
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }

    if (xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 23, NULL) != pdPASS)
    {
        ESP_LOGE(pcTaskGetName(NULL), "Failed to create LoRa send task");
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }
    TaskHandle_t startup_task = xTaskGetCurrentTaskHandle();
    if (xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048, startup_task, 23, NULL) != pdPASS)
    {
        ESP_LOGE(pcTaskGetName(NULL), "Failed to create LoRa receive task");
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }

    uint32_t receive_status = (uint32_t)ESP_FAIL;
    if (xTaskNotifyWait(0, UINT32_MAX, &receive_status,
                        pdMS_TO_TICKS(LORA_RECEIVER_STARTUP_TIMEOUT_MS)) != pdTRUE)
    {
        ESP_LOGE(pcTaskGetName(NULL), "LoRa receive task did not report readiness");
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }
    if ((esp_err_t)receive_status != ESP_OK)
    {
        ESP_LOGE(pcTaskGetName(NULL), "LoRa receive task failed to start: %s",
                 esp_err_to_name((esp_err_t)receive_status));
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }

    if (xTaskCreate(LoraSyncTask, "LoraSyncTask", 4048, NULL, 8, NULL) != pdPASS)
    {
        ESP_LOGE(pcTaskGetName(NULL), "Failed to create LoRa sync task");
        dogdog_startup_signal_failure();
        vTaskDelete(NULL);
        return;
    }

    dogdog_startup_signal_ready(DOGDOG_STARTUP_PRIMARY_IO_READY_BIT);
    vTaskDelete(NULL);
}

void HandleReceivedPacket(DogDogPacket *packet)
{
    if (!packet)
    {
        ESP_LOGE(pcTaskGetName(NULL), "Received null DogDogPacket");
        return;
    }

    // Handle the received packet based on its type
    switch (packet->type)
    {
    case LORA_TIME_SYNC:
    {
        // The controller should not receive time synchronization information
        break;
    }
    case LORA_TRIGGER:
    {
        if (!is_measurement_station(packet) ||
            !has_payload_length(packet, sizeof(int64_t) + sizeof(PacketTypeSensorState)))
        {
            break;
        }

        PacketTypeTrigger trigger = {0};
        memcpy(&trigger.timestamp, packet->payload, sizeof(trigger.timestamp));
        memcpy(&trigger.sensor_state, packet->payload + sizeof(trigger.timestamp),
               sizeof(trigger.sensor_state));
        if (trigger.sensor_state.num_sensors > MAX_SENSOR_STATUS_BITS)
        {
            ESP_LOGW(pcTaskGetName(NULL), "Dropping trigger with invalid sensor count: %u",
                     trigger.sensor_state.num_sensors);
            break;
        }

        TimerTrigger timerTriggerCause = {
            .is_start = packet->station_id == start_id,
            .timestamp = trigger.timestamp,
            .is_final_time = false,
            .force_restart = false,
        };

        // Only send ack it time makes sense (i.e. the timestamp is not too far in the past or future compared to the current time)
        timeval_t current_time;
        gettimeofday(&current_time, NULL);

        bool timer_is_running;
        int64_t timer_start;
        getTimerState(&timer_is_running, &timer_start);
        int64_t current_us = TIME_US(current_time);
        if (timerTriggerCause.timestamp < current_us - 20000000 ||
            timerTriggerCause.timestamp > current_us + 20000000)
        {
            ESP_LOGW(pcTaskGetName(NULL),
                     "Received trigger with timestamp too far from current time. Current time: %lld, trigger time: %lld",
                     current_us, timerTriggerCause.timestamp);
            break;
        }
        if (timer_is_running && timerTriggerCause.timestamp < timer_start)
        {
            ESP_LOGW(pcTaskGetName(NULL),
                     "Received trigger with timestamp before timer start time. Timer start time: %lld, trigger time: %lld",
                     timer_start, timerTriggerCause.timestamp);
            break;
        }

        if (!enqueue_trigger_or_duplicate(packet, &timerTriggerCause))
        {
            /* Do not ACK an event which was not accepted; the station can retry. */
            break;
        }

        confirm_station_alive(packet);
        send_sensor_status_to_display(&trigger.sensor_state, packet->station_id, true);
        send_ack_for_packet(packet);
        break;
    }
    case LORA_FINAL_TIME:
    {
        if (!is_measurement_station(packet) || !has_payload_length(packet, sizeof(int64_t)))
        {
            break;
        }

        int64_t final_timestamp;
        memcpy(&final_timestamp, packet->payload, sizeof(final_timestamp));
        int64_t timer_start;
        getTimerState(NULL, &timer_start);
        timeval_t current_time;
        gettimeofday(&current_time, NULL);
        int64_t current_us = TIME_US(current_time);
        if (timer_start == 0 || final_timestamp < timer_start ||
            final_timestamp < current_us - 20000000 ||
            final_timestamp > current_us + 20000000)
        {
            ESP_LOGW(pcTaskGetName(NULL), "Final timestamp is outside the valid timer window");
            break;
        }

        TimerTrigger timerTriggerCause = {
            .is_start = packet->station_id == start_id,
            .timestamp = final_timestamp,
            .is_final_time = true,
            .force_restart = false,
        };

        ESP_LOGI(pcTaskGetName(NULL), "Received final time trigger");

        if (!enqueue_trigger_or_duplicate(packet, &timerTriggerCause))
        {
            break;
        }

        confirm_station_alive(packet);
        send_ack_for_packet(packet);
        break;
    }
    case LORA_SENSOR_STATE:
    {
        if (!is_measurement_station(packet) ||
            !has_payload_length(packet, sizeof(uint8_t) + sizeof(uint64_t)))
        {
            break;
        }

        PacketTypeSensorState sensor_state = {
            .num_sensors = packet->payload[0],
        };
        memcpy(&sensor_state.sensor_states, packet->payload + sizeof(uint8_t),
               sizeof(sensor_state.sensor_states));

        // Process sensor state information
        confirm_station_alive(packet);
        send_sensor_status_to_display(&sensor_state, packet->station_id, false);
        break;
    }
    case LORA_ACK:
    {
        if (!has_payload_length(packet, sizeof(uint8_t) + sizeof(uint8_t)))
        {
            break;
        }
        PacketTypeAck ack = {
            .station_id = packet->payload[0],
            .packet_id = packet->payload[1],
        };
        ESP_LOGI(pcTaskGetName(NULL), "ACK received for station: %d packet: %d", ack.station_id, ack.packet_id);
        if (!ackQueue || xQueueSend(ackQueue, &ack, 0) != pdTRUE)
        {
            ESP_LOGW(pcTaskGetName(NULL), "ACK queue unavailable/full");
        }
        break;
    }
    default:
        ESP_LOGW(pcTaskGetName(NULL), "Unknown packet type: %d", packet->type);
    }
}

void LoraSyncTask(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    while (1)
    {
        timeval_t timestamp;
        PacketTypeTimeSync time_sync;
        gettimeofday(&timestamp, NULL);
        time_sync.timestamp = TIME_US(timestamp);
        DogDogPacket *packet = create_dogdog_packet_from_time_sync_information(&time_sync);
        if (!packet)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate DogDogPacket for time sync");
        }
        else
        {
            enqueue_owned_packet(packet);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10000));
    }
}

void confirm_station_alive(DogDogPacket *packet)
{
    if (!packet || (packet->station_id != start_id && packet->station_id != stop_id))
    {
        return;
    }

    int is_start = packet->station_id == start_id ? START_ALIVE : STOP_ALIVE;

    StationConnectivityStatus status;
    status.station = is_start;
    status.signal = 0;
    if (packet->rssi < -100 || packet->snr < -10)
    {
        status.signal = 1; // Bad signal (warning)
    }
    if (xQueueSend(networkFaultQueue, &status, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        ESP_LOGW(pcTaskGetName(NULL), "Network status queue full");
    }
}
