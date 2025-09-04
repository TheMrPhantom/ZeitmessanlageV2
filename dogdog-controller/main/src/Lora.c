#include "Lora.h"
#include "SevenSegment.h"
#include "Timer.h"
#include "NetworkFault.h"

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
        timerTriggerCause.is_start = packet->station_id == START_ID;
        timerTriggerCause.timestamp = trigger->timestamp;

        if (packet->station_id == START_ID)
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

        SensorStatus sensorStatus;
        populate_sensor_status(&sensorStatus, &trigger->sensor_state, packet->station_id, true);

        SevenSegmentDisplay toSend;
        toSend.type = SEVEN_SEGMENT_SENSOR_STATUS;
        toSend.sensorStatus = sensorStatus;
        xQueueSend(sevenSegmentQueue, &toSend, 0);

        // Send ack
        PacketTypeAck ack;
        ack.station_id = packet->station_id;
        ack.packet_id = packet->packet_id;

        DogDogPacket *ack_packet = create_dogdog_packet_from_ack_information(&ack);
        if (!ack_packet)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate DogDogPacket for ACK");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        xQueueSend(loraSendQueue, &ack_packet, portMAX_DELAY);

        // Process trigger information
        free(trigger);
        break;
    }
    case LORA_FINAL_TIME:
    {
        // The controller should not receive final time information
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

        SensorStatus sensorStatus;
        populate_sensor_status(&sensorStatus, sensor_state, packet->station_id, false);

        SevenSegmentDisplay toSend;
        toSend.type = SEVEN_SEGMENT_SENSOR_STATUS;
        toSend.sensorStatus = sensorStatus;
        xQueueSend(sevenSegmentQueue, &toSend, 0);

        free(sensor_state);
        break;
    }
    case LORA_ACK:
    {
        // The controller should not receive ACK information
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
        if (!packet)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate DogDogPacket for time sync");
            continue;
        }
        xQueueSend(loraSendQueue, &packet, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void confirm_station_alive(DogDogPacket *packet)
{
    int is_start = packet->station_id == START_ID ? START_ALIVE : STOP_ALIVE;

    StationConnectivityStatus status;
    status.station = is_start;
    status.signal = 0;
    if (packet->rssi < -100 || packet->snr < -10)
    {
        status.signal = 1; // Bad signal (warning)
    }
    xQueueSend(networkFaultQueue, &status, portMAX_DELAY);
}