#include "LoraNetwork.h"
#include "esp_log.h"
#include "LED.h"

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

extern TaskHandle_t sensorInterruptTaskHandle;
bool is_time_synced = false;
extern uint8_t station_id;
extern int64_t last_release_timestamp;

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

        if (!is_time_synced)
        {
            while (sensorInterruptTaskHandle == NULL)
            {
                ESP_LOGW(pcTaskGetName(NULL), "Sensor interrupt task handle not set yet, waiting...");
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            ESP_LOGI(pcTaskGetName(NULL), "Time sync completed");

            xTaskNotifyGive(sensorInterruptTaskHandle);
            is_time_synced = true;
            set_all_leds(0, 0, 0); // Turn off all leds after time sync
        }

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
    case LORA_REQUEST_FINAL_TIME:
    {
        if (packet->payload_length != sizeof(uint8_t))
        {
            ESP_LOGW(pcTaskGetName(NULL), "Invalid payload length for final time request: %d", packet->payload_length);
            break;
        }

        uint8_t requested_station_id = packet->payload[0];
        if (requested_station_id != station_id)
        {
            ESP_LOGW(pcTaskGetName(NULL), "Final time request for different station id: %d", requested_station_id);
            break;
        }

        PacketTypeAck ack;
        ack.station_id = packet->station_id;
        ack.packet_id = packet->packet_id;

        DogDogPacket *ack_packet = create_dogdog_packet_from_ack_information(&ack);
        if (!ack_packet)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate DogDogPacket for ACK");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(250));
        xQueueSend(loraSendQueue, &ack_packet, portMAX_DELAY);

        PacketTypeFinalTime final_time;
        final_time.timestamp = last_release_timestamp;
        DogDogPacket *final_time_packet = create_dogdog_packet_from_final_time_information(&final_time);
        if (!final_time_packet)
        {
            ESP_LOGE(pcTaskGetName(NULL), "Failed to allocate DogDogPacket for final time");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        xQueueSend(loraSendQueue, &final_time_packet, portMAX_DELAY);

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
