#include "Lora.h"
#include "SevenSegment.h"
#include "Timer.h"
#include "NetworkFault.h"
#include "esp_err.h"

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

uint8_t last_start_trigger = 255;
uint8_t last_stop_trigger = 255;

extern int controller_id;
extern int start_id;
extern int stop_id;
extern int station_id;

typedef struct OutgoingAck
{
    PacketTypeAck ack;
    TickType_t queued_at;
} OutgoingAck;

static QueueHandle_t outgoingAckQueue;

static void LoraAckSendTask(void *pvParameters)
{
    const TickType_t ack_delay = pdMS_TO_TICKS(1000);
    OutgoingAck outgoing_ack;

    while (true)
    {
        if (xQueueReceive(outgoingAckQueue, &outgoing_ack, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        // Measure the delay from enqueue time so queued ACKs do not each add a second.
        TickType_t elapsed = xTaskGetTickCount() - outgoing_ack.queued_at;
        if (elapsed < ack_delay)
        {
            vTaskDelay(ack_delay - elapsed);
        }

        DogDogPacket *ack_packet = create_dogdog_packet_from_ack_information(&outgoing_ack.ack);
        if (!ack_packet)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate DogDogPacket for ACK");
            continue;
        }

        if (xQueueSend(loraSendQueue, &ack_packet, portMAX_DELAY) != pdTRUE)
        {
            ESP_LOGW(pcTaskGetName(NULL), "Failed to queue outgoing ACK");
            free(ack_packet->payload);
            free(ack_packet);
        }
    }
}

static void queue_ack_for_packet(const DogDogPacket *packet)
{
    OutgoingAck outgoing_ack = {
        .ack = {
            .station_id = packet->station_id,
            .packet_id = packet->packet_id,
        },
        .queued_at = xTaskGetTickCount(),
    };

    // Copy the ACK details; the receive task frees the original packet on return.
    if (xQueueSend(outgoingAckQueue, &outgoing_ack, 0) != pdTRUE)
    {
        ESP_LOGW(pcTaskGetName(NULL), "Outgoing ACK queue full for station: %d packet: %d",
                 packet->station_id, packet->packet_id);
    }
}

void LoraStartupTask(void *pvParameters)
{
    init_lora();

    outgoingAckQueue = xQueueCreate(40, sizeof(OutgoingAck));
    ESP_ERROR_CHECK(outgoingAckQueue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    BaseType_t ack_task_created = xTaskCreate(LoraAckSendTask, "LoraAckSendTask", 4048, NULL, 23, NULL);
    ESP_ERROR_CHECK(ack_task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(xTaskCreate(LoraSendTask, "LoraSendTask", 4048, NULL, 23, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(LoraReceiveTask, "LoraReceiveTask", 4048, NULL, 23, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(LoraSyncTask, "LoraSyncTask", 4048, NULL, 8, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    vTaskDelete(NULL);
}

static void queue_sensor_status(PacketTypeSensorState *state, uint8_t source_station, bool is_trigger)
{
    SevenSegmentDisplay display = {.type = SEVEN_SEGMENT_SENSOR_STATUS};
    if (!populate_sensor_status(&display.sensorStatus, state, source_station, is_trigger))
    {
        ESP_LOGW(pcTaskGetName(NULL), "Unable to create sensor status display");
        return;
    }
    // The display owns the allocation only after the queue accepted it.
    if (xQueueSend(sevenSegmentQueue, &display, 0) != pdTRUE)
    {
        free(display.sensorStatus.status);
    }
}

void HandleReceivedPacket(DogDogPacket *packet)
{
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
        PacketTypeTrigger *trigger = create_trigger_information(packet);
        if (!trigger)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate PacketTypeTrigger");
            break;
        }
        TimerTrigger timerTriggerCause;
        timerTriggerCause.is_start = packet->station_id == start_id;
        timerTriggerCause.timestamp = trigger->timestamp;
        timerTriggerCause.is_final_time = false;

        // Only send ack it time makes sense (i.e. the timestamp is not too far in the past or future compared to the current time)
        timeval_t current_time;
        gettimeofday(&current_time, NULL);

        int64_t now_us = TIME_US(current_time);
        if (timerTriggerCause.timestamp <= 0 ||
            (timerTriggerCause.timestamp > now_us && timerTriggerCause.timestamp - now_us > 20000000) ||
            (timerTriggerCause.timestamp <= now_us && now_us - timerTriggerCause.timestamp > 20000000))
        {
            ESP_LOGW(pcTaskGetName(NULL), "Received trigger with timestamp too far from current time. Current time: %lld, trigger time: %lld", now_us, timerTriggerCause.timestamp);
            free(trigger);
            break;
        }

        if (packet->station_id == start_id)
        {
            if (last_start_trigger != packet->packet_id)
            {
                last_start_trigger = packet->packet_id;
                xQueueSend(triggerQueue, &timerTriggerCause, portMAX_DELAY);
            }
            else
            {
                ESP_LOGW(pcTaskGetName(NULL), "Duplicate start trigger ignored for packet %d", packet->packet_id);
            }
        }
        else
        {
            if (last_stop_trigger != packet->packet_id)
            {
                last_stop_trigger = packet->packet_id;
                xQueueSend(triggerQueue, &timerTriggerCause, portMAX_DELAY);
            }
            else
            {
                ESP_LOGW(pcTaskGetName(NULL), "Duplicate stop trigger ignored for packet %d", packet->packet_id);
            }
        }

        confirm_station_alive(packet);

        queue_sensor_status(&trigger->sensor_state, packet->station_id, true);

        queue_ack_for_packet(packet);

        // Process trigger information
        free(trigger);
        break;
    }
    case LORA_FINAL_TIME:
    {
        PacketTypeFinalTime *trigger = create_final_time_information(packet);
        if (!trigger)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate PacketTypeTrigger");
            break;
        }
        TimerTrigger timerTriggerCause;
        timerTriggerCause.is_start = packet->station_id == start_id;
        timerTriggerCause.timestamp = trigger->timestamp;
        timerTriggerCause.is_final_time = true;

        ESP_LOGI(pcTaskGetName(NULL), "Received final time trigger");

        if (packet->station_id == start_id)
        {
            if (last_start_trigger != packet->packet_id)
            {
                last_start_trigger = packet->packet_id;
                xQueueSend(triggerQueue, &timerTriggerCause, portMAX_DELAY);
            }
            else
            {
                ESP_LOGW(pcTaskGetName(NULL), "Duplicate start trigger ignored for packet %d", packet->packet_id);
            }
        }
        else
        {
            if (last_stop_trigger != packet->packet_id)
            {
                last_stop_trigger = packet->packet_id;
                xQueueSend(triggerQueue, &timerTriggerCause, portMAX_DELAY);
            }
            else
            {
                ESP_LOGW(pcTaskGetName(NULL), "Duplicate stop trigger ignored for packet %d", packet->packet_id);
            }
        }

        free(trigger);
        queue_ack_for_packet(packet);

        break;
    }
    case LORA_SENSOR_STATE:
    {
        PacketTypeSensorState *sensor_state = create_sensor_state_information(packet);
        if (!sensor_state)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate PacketTypeSensorState");
            break;
        }
        // Process sensor state information
        confirm_station_alive(packet);

        queue_sensor_status(sensor_state, packet->station_id, false);

        free(sensor_state);
        break;
    }
    case LORA_ACK:
    {
        PacketTypeAck *ack = create_ack_information(packet);
        if (ack == NULL)
        {
            ESP_LOGW(pcTaskGetName(NULL), "Invalid ACK or insufficient memory");
            break;
        }
        ESP_LOGI(pcTaskGetName(NULL), "ACK received for station: %d packet: %d", ack->station_id, ack->packet_id);
        xQueueSend(ackQueue, ack, 0);
        free(ack);
        break;
    }
    default:
        ESP_LOGW(pcTaskGetName(NULL), "Unknown packet type: %d", packet->type);
    }
}

void LoraSyncTask(void *pvParameters)
{
    while (1)
    {
        timeval_t timestamp;
        PacketTypeTimeSync time_sync;
        gettimeofday(&timestamp, NULL);
        time_sync.timestamp = TIME_US(timestamp);
        DogDogPacket *packet = create_dogdog_packet_from_time_sync_information(&time_sync);
        if (send_dogdog_packet(packet) != pdTRUE)
        {
            ESP_LOGW(pcTaskGetName(NULL), "Unable to queue time sync");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void confirm_station_alive(DogDogPacket *packet)
{
    int is_start = packet->station_id == start_id ? START_ALIVE : STOP_ALIVE;

    StationConnectivityStatus status;
    status.station = is_start;
    status.signal = 0;
    if (packet->rssi < -100 || packet->snr < -10)
    {
        status.signal = 1; // Bad signal (warning)
    }
    xQueueSend(networkFaultQueue, &status, portMAX_DELAY);
}
