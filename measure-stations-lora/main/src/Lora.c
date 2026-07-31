#include "Lora.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "LED.h"
#include "Sensor.h"

static const char *TAG = "LORA_HANDLER";
static const int64_t FINAL_TIME_VALID_WINDOW_US = 20000000LL;

extern QueueHandle_t loraSendQueue;
extern QueueHandle_t ackQueue;

extern int64_t time_offset_to_controller;
extern int64_t timesync_current_time;
extern int64_t timesync_received_time;
extern int64_t timesync_processing_time;
extern int64_t time_of_controller;
extern portMUX_TYPE timesync_spinlock;

extern TaskHandle_t sensorInterruptTaskHandle;
extern int controller_id;
extern int station_id;

static bool is_time_synced;

static bool is_from_controller(const DogDogPacket *packet)
{
    if (packet->station_id != (uint8_t)controller_id)
    {
        ESP_LOGW(TAG, "Ignoring packet type %u from non-controller station %u",
                 packet->type, packet->station_id);
        return false;
    }
    return true;
}

static bool final_time_is_recent(int64_t final_timestamp)
{
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    int64_t offset;
    taskENTER_CRITICAL(&timesync_spinlock);
    offset = time_offset_to_controller;
    taskEXIT_CRITICAL(&timesync_spinlock);

    int64_t controller_now;
    int64_t age;
    if (__builtin_add_overflow(TIME_US(current_time), offset, &controller_now) ||
        __builtin_sub_overflow(controller_now, final_timestamp, &age) ||
        age < -FINAL_TIME_VALID_WINDOW_US || age > FINAL_TIME_VALID_WINDOW_US)
    {
        return false;
    }
    return true;
}

static void free_dogdog_packet(DogDogPacket *packet)
{
    if (packet != NULL)
    {
        free(packet->payload);
        free(packet);
    }
}

// On success ownership transfers to lora-network. On failure it remains ours.
static bool enqueue_lora_packet(DogDogPacket *packet, TickType_t wait_ticks)
{
    if (packet == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate outbound LoRa packet");
        return false;
    }

    if (loraSendQueue == NULL || xQueueSend(loraSendQueue, &packet, wait_ticks) != pdPASS)
    {
        ESP_LOGW(TAG, "LoRa send queue unavailable/full; dropping packet type %u", packet->type);
        free_dogdog_packet(packet);
        return false;
    }
    return true;
}

void HandleReceivedPacket(DogDogPacket *packet)
{
    if (packet == NULL)
    {
        ESP_LOGW(TAG, "Received a null packet");
        return;
    }

    switch (packet->type)
    {
    case LORA_TIME_SYNC:
    {
        if (!is_from_controller(packet))
        {
            break;
        }
        if (packet->payload == NULL || packet->payload_length != sizeof(int64_t))
        {
            ESP_LOGW(TAG, "Invalid time-sync payload length: %u", packet->payload_length);
            break;
        }

        int64_t controller_timestamp;
        memcpy(&controller_timestamp, packet->payload, sizeof(controller_timestamp));

        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        const int64_t current_time_us = TIME_US(current_time);
        int64_t processing_time;
        int64_t adjusted_controller_time;
        int64_t new_time_offset;
        if (__builtin_sub_overflow(current_time_us, packet->local_time_received, &processing_time) ||
            processing_time < 0 ||
            __builtin_add_overflow(controller_timestamp, processing_time, &adjusted_controller_time) ||
            __builtin_sub_overflow(adjusted_controller_time, current_time_us, &new_time_offset))
        {
            ESP_LOGW(TAG, "Rejecting invalid time-sync timestamp");
            break;
        }

        taskENTER_CRITICAL(&timesync_spinlock);
        timesync_current_time = current_time_us;
        timesync_received_time = packet->local_time_received;
        timesync_processing_time = processing_time;
        time_of_controller = adjusted_controller_time;
        time_offset_to_controller = new_time_offset;
        taskEXIT_CRITICAL(&timesync_spinlock);

        if (!is_time_synced)
        {
            if (sensorInterruptTaskHandle == NULL)
            {
                // Do not block the receive task. A later time-sync packet can
                // complete initialization once the sensor task exists.
                ESP_LOGW(TAG, "Sensor task is not ready; deferring initial time sync notification");
                break;
            }

            xTaskNotifyGive(sensorInterruptTaskHandle);
            is_time_synced = true;
            set_all_leds(0, 0, 0);
            ESP_LOGI(TAG, "Time sync completed");
        }
        break;
    }

    case LORA_TRIGGER:
    case LORA_FINAL_TIME:
    case LORA_SENSOR_STATE:
        // A measurement station does not consume these packet types.
        break;

    case LORA_REQUEST_FINAL_TIME:
    {
        if (!is_from_controller(packet))
        {
            break;
        }
        if (packet->payload == NULL || packet->payload_length != sizeof(uint8_t))
        {
            ESP_LOGW(TAG, "Invalid final-time request payload length: %u", packet->payload_length);
            break;
        }

        const uint8_t requested_station_id = packet->payload[0];
        if (requested_station_id != (uint8_t)station_id)
        {
            ESP_LOGW(TAG, "Final-time request for different station id: %u", requested_station_id);
            break;
        }

        int64_t release_timestamp;
        if (!get_last_release_timestamp(&release_timestamp))
        {
            // Not acknowledging asks the controller's retry logic to try again
            // instead of accepting an invalid timestamp of zero.
            ESP_LOGW(TAG, "No sensor release timestamp is available yet");
            break;
        }
        if (!final_time_is_recent(release_timestamp))
        {
            // The controller applies the same validity window. Keep the request
            // pending instead of ACKing it and sending a timestamp that the
            // controller must reject.
            ESP_LOGW(TAG, "The latest sensor release is outside the valid final-time window");
            break;
        }

        PacketTypeAck ack = {
            .station_id = packet->station_id,
            .packet_id = packet->packet_id,
        };
        DogDogPacket *ack_packet = create_dogdog_packet_from_ack_information(&ack);
        PacketTypeFinalTime final_time = {.timestamp = release_timestamp};
        DogDogPacket *final_time_packet = create_dogdog_packet_from_final_time_information(&final_time);

        if (ack_packet == NULL || final_time_packet == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate final-time response packets");
            free_dogdog_packet(ack_packet);
            free_dogdog_packet(final_time_packet);
            break;
        }

        // Both responses must be accepted before this request is considered
        // handled. In particular, do not ACK the request and then silently lose
        // the final-time packet because the outbound queue was briefly full.
        if (!enqueue_lora_packet(ack_packet, portMAX_DELAY))
        {
            free_dogdog_packet(final_time_packet);
            break;
        }

        enqueue_lora_packet(final_time_packet, portMAX_DELAY);
        break;
    }

    case LORA_ACK:
    {
        if (!is_from_controller(packet))
        {
            break;
        }
        if (packet->payload == NULL || packet->payload_length != sizeof(PacketTypeAck))
        {
            ESP_LOGW(TAG, "Invalid ACK payload length: %u", packet->payload_length);
            break;
        }

        PacketTypeAck ack;
        memcpy(&ack, packet->payload, sizeof(ack));
        if (ack.station_id != (uint8_t)station_id)
        {
            // Both stations hear controller traffic. Do not let ACKs for the
            // peer station consume this station's ACK queue or match a future
            // packet-ID wraparound.
            ESP_LOGD(TAG, "Ignoring ACK for station %u", ack.station_id);
            break;
        }
        ESP_LOGI(TAG, "ACK received for station: %u packet: %u", ack.station_id, ack.packet_id);

        if (ackQueue == NULL || xQueueSend(ackQueue, &ack, 0) != pdPASS)
        {
            ESP_LOGW(TAG, "ACK queue unavailable/full; ACK was dropped");
        }
        break;
    }

    default:
        ESP_LOGW(TAG, "Unknown packet type: %u", packet->type);
        break;
    }
}
