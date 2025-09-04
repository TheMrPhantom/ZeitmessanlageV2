#include "LoraNetwork.h"
#include "esp_log.h"

extern QueueHandle_t loraSendQueue;
extern QueueHandle_t localReceiveTimestampQueue;
extern QueueHandle_t ackQueue;

extern int64_t time_offset_to_controller;

extern int64_t timesync_timestamp_current;
extern int64_t timesync_current_time;
extern int64_t timesync_received_time;
extern int64_t timesync_processing_time;
extern int64_t time_of_controller;
extern portMUX_TYPE timesync_spinlock;

void HandleReceivedPacket(DogDogPacket *packet)
{
    // Handle the received packet based on its type
    switch (packet->type)
    {
    case LORA_TIME_SYNC:
    {
        PacketTypeTimeSync *time_sync = create_time_sync_information(packet);
        // ESP_LOGI(pcTaskGetName(NULL), "Time sync packet received: %" PRId64, time_sync->timestamp);

        taskENTER_CRITICAL(&timesync_spinlock);
        timeval_t tv_current;
        gettimeofday(&tv_current, NULL);
        timesync_current_time = TIME_US(tv_current);
        timesync_received_time = packet->local_time_received;
        timesync_processing_time = timesync_current_time - timesync_received_time;
        time_of_controller = time_sync->timestamp + timesync_processing_time;
        time_offset_to_controller = time_of_controller - TIME_US(tv_current);

        // Normal operation again
        taskEXIT_CRITICAL(&timesync_spinlock);

        // ESP_LOGI(pcTaskGetName(NULL), "Current offset: %" PRId64, time_offset_to_controller);

        break;
    }
    case LORA_TRIGGER:
    {
        // Measure station never gets this info
        break;
    }
    case LORA_FINAL_TIME:
    {
        // Measure station never gets this info
        break;
    }
    case LORA_SENSOR_STATE:
    {
        // Measure station never gets this info
        break;
    }
    case LORA_ACK:
    {
        PacketTypeAck *ack = create_ack_information(packet);
        ESP_LOGI(pcTaskGetName(NULL), "ACK received for packet: %d", ack->packet_id);
        xQueueSend(ackQueue, &ack->packet_id, 0);
        free(ack);
        break;
    }
    default:
        ESP_LOGW(pcTaskGetName(NULL), "Unknown packet type: %d", packet->type);
    }
}
