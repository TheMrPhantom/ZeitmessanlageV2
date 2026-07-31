#include "LoraNetwork.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "ra01s.h" // For LoRaSend/LoRaReceive
#include "driver/gpio.h"

static const char *TAG_LORA = "LoraNetwork";

int64_t time_offset_to_controller = 0;

int64_t timesync_timestamp_current = 0;
int64_t timesync_current_time = 0;
int64_t timesync_received_time = 0;
int64_t timesync_processing_time = 0;
int64_t time_of_controller = 0;
portMUX_TYPE timesync_spinlock = portMUX_INITIALIZER_UNLOCKED;

void (*handle_dogdog_packet)(DogDogPacket *packet) = NULL;

QueueHandle_t loraSendQueue;
QueueHandle_t localReceiveTimestampQueue;
QueueHandle_t ackQueue;
QueueHandle_t loraInterruptQueue;
static SemaphoreHandle_t radio_operation_mutex;

#define MAX_PENDING_ACKS 40
#define LORA_PACKET_HEADER_LEN 10U
#define LORA_MAX_FRAME_LEN 255U
#define SENSOR_STATE_WIRE_LEN (sizeof(uint8_t) + sizeof(uint64_t))
#define TRIGGER_WIRE_LEN (sizeof(int64_t) + sizeof(PacketTypeSensorState))
#define ACK_TIMEOUT_MS 2000
#define ACK_SCAN_INTERVAL_MS 50
#define MAX_SEND_ATTEMPTS 6

typedef struct PendingAck
{
    bool active;
    uint8_t station_id;
    uint8_t packet_id;
    TickType_t waiting_since;
    DogDogPacket *packet;
} PendingAck;

static PendingAck pending_acks[MAX_PENDING_ACKS];
static portMUX_TYPE pending_ack_spinlock = portMUX_INITIALIZER_UNLOCKED;

extern int controller_id;
extern int start_id;
extern int stop_id;
extern int station_id;

static void free_dogdog_packet(DogDogPacket *packet)
{
    if (packet != NULL)
    {
        free(packet->payload);
        free(packet);
    }
}

static bool register_pending_ack(DogDogPacket *packet)
{
    bool registered = false;
    TickType_t waiting_since = xTaskGetTickCount();

    portENTER_CRITICAL(&pending_ack_spinlock);
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (pending_acks[i].active &&
            pending_acks[i].station_id == packet->station_id &&
            pending_acks[i].packet_id == packet->packet_id)
        {
            portEXIT_CRITICAL(&pending_ack_spinlock);
            return false;
        }
    }
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (!pending_acks[i].active)
        {
            pending_acks[i].active = true;
            pending_acks[i].station_id = packet->station_id;
            pending_acks[i].packet_id = packet->packet_id;
            pending_acks[i].waiting_since = waiting_since;
            pending_acks[i].packet = packet;
            registered = true;
            break;
        }
    }
    portEXIT_CRITICAL(&pending_ack_spinlock);

    return registered;
}

static DogDogPacket *take_pending_ack_packet(uint8_t ack_station_id, uint8_t ack_packet_id)
{
    DogDogPacket *packet = NULL;
    portENTER_CRITICAL(&pending_ack_spinlock);
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (pending_acks[i].active &&
            pending_acks[i].station_id == ack_station_id &&
            pending_acks[i].packet_id == ack_packet_id)
        {
            packet = pending_acks[i].packet;
            pending_acks[i].active = false;
            pending_acks[i].packet = NULL;
            break;
        }
    }
    portEXIT_CRITICAL(&pending_ack_spinlock);
    return packet;
}

static size_t take_expired_ack_packets(DogDogPacket **packets, size_t capacity)
{
    const TickType_t timeout_ticks = pdMS_TO_TICKS(ACK_TIMEOUT_MS);
    const TickType_t now = xTaskGetTickCount();
    size_t count = 0;

    portENTER_CRITICAL(&pending_ack_spinlock);
    for (int i = 0; i < MAX_PENDING_ACKS && count < capacity; i++)
    {
        if (pending_acks[i].active &&
            (TickType_t)(now - pending_acks[i].waiting_since) >= timeout_ticks)
        {
            packets[count++] = pending_acks[i].packet;
            pending_acks[i].active = false;
            pending_acks[i].packet = NULL;
        }
    }
    portEXIT_CRITICAL(&pending_ack_spinlock);

    return count;
}

DogDogPacket *create_dogdog_packet_from_bytes(const uint8_t *data, size_t length)
{
    if (data == NULL || length < LORA_PACKET_HEADER_LEN || length > LORA_MAX_FRAME_LEN)
    {
        ESP_LOGW(TAG_LORA, "Invalid LoRa frame length: %u", (unsigned int)length);
        return NULL;
    }

    uint32_t magic = 0;
    memcpy(&magic, data, sizeof(magic));
    if (magic != LORA_MAGIC)
    {
        ESP_LOGW(TAG_LORA, "Received packet with invalid magic: 0x%08lx", (unsigned long)magic);
        return NULL;
    }

    uint16_t payload_length = ((uint16_t)data[8] << 8) | data[9];
    if ((size_t)payload_length != length - LORA_PACKET_HEADER_LEN)
    {
        ESP_LOGW(TAG_LORA,
                 "Invalid payload length: declared=%u received=%u",
                 (unsigned int)payload_length,
                 (unsigned int)(length - LORA_PACKET_HEADER_LEN));
        return NULL;
    }

    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    packet->magic = magic;
    packet->protocol_version = data[4];

    if (packet->protocol_version != LORA_PROTOCOL_VERSION)
    {
        ESP_LOGW(TAG_LORA, "Received packet with unsupported protocol version: %d", packet->protocol_version);
        free(packet);
        return NULL;
    }

    packet->station_id = data[5];

    if (packet->station_id != controller_id && packet->station_id != start_id && packet->station_id != stop_id)
    {
        ESP_LOGW(TAG_LORA, "Received packet with invalid station id: %d", packet->station_id);
        free(packet);
        return NULL;
    }

    packet->packet_id = data[6];
    packet->type = data[7];
    packet->payload_length = payload_length;

    if (packet->payload_length > 0)
    {
        packet->payload = malloc(packet->payload_length);
        if (!packet->payload)
        {
            ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
            free(packet);
            return NULL;
        }
        memcpy(packet->payload, data + LORA_PACKET_HEADER_LEN, packet->payload_length);
    }

    return packet;
}

bool is_packet_from_dogdog(const uint8_t *data, size_t length)
{
    uint32_t magic = 0;
    if (data == NULL || length < sizeof(magic))
    {
        return false;
    }
    memcpy(&magic, data, sizeof(magic));
    return magic == LORA_MAGIC;
}

static bool packet_has_payload(const DogDogPacket *packet, uint8_t type, size_t expected_length)
{
    if (packet == NULL || packet->type != type || packet->payload == NULL ||
        packet->payload_length != expected_length)
    {
        ESP_LOGW(TAG_LORA,
                 "Invalid payload for packet type %u (expected %u bytes)",
                 (unsigned int)type,
                 (unsigned int)expected_length);
        return false;
    }
    return true;
}

PacketTypeTimeSync *create_time_sync_information(DogDogPacket *packet)
{
    if (!packet_has_payload(packet, LORA_TIME_SYNC, sizeof(int64_t)))
    {
        return NULL;
    }

    PacketTypeTimeSync *packet_type = calloc(1, sizeof(PacketTypeTimeSync));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeTimeSync");
        return NULL;
    }
    memcpy(&packet_type->timestamp, packet->payload, sizeof(packet_type->timestamp));
    return packet_type;
}

PacketTypeTrigger *create_trigger_information(DogDogPacket *packet)
{
    if (!packet_has_payload(packet, LORA_TRIGGER, TRIGGER_WIRE_LEN))
    {
        return NULL;
    }

    PacketTypeTrigger *packet_type = calloc(1, sizeof(PacketTypeTrigger));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeTrigger");
        return NULL;
    }
    memcpy(&packet_type->timestamp, packet->payload, sizeof(packet_type->timestamp));
    packet_type->sensor_state.num_sensors =
        packet->payload[sizeof(int64_t) + offsetof(PacketTypeSensorState, num_sensors)];
    memcpy(&packet_type->sensor_state.sensor_states,
           packet->payload + sizeof(int64_t) + offsetof(PacketTypeSensorState, sensor_states),
           sizeof(packet_type->sensor_state.sensor_states));
    if (packet_type->sensor_state.num_sensors > 64)
    {
        ESP_LOGW(TAG_LORA,
                 "Invalid sensor count: %u",
                 (unsigned int)packet_type->sensor_state.num_sensors);
        free(packet_type);
        return NULL;
    }
    return packet_type;
}

PacketTypeFinalTime *create_final_time_information(DogDogPacket *packet)
{
    if (!packet_has_payload(packet, LORA_FINAL_TIME, sizeof(int64_t)))
    {
        return NULL;
    }

    PacketTypeFinalTime *packet_type = calloc(1, sizeof(PacketTypeFinalTime));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeFinalTime");
        return NULL;
    }
    memcpy(&packet_type->timestamp, packet->payload, sizeof(packet_type->timestamp));
    return packet_type;
}

PacketTypeSensorState *create_sensor_state_information(DogDogPacket *packet)
{
    if (!packet_has_payload(packet, LORA_SENSOR_STATE, SENSOR_STATE_WIRE_LEN))
    {
        return NULL;
    }

    PacketTypeSensorState *packet_type = calloc(1, sizeof(PacketTypeSensorState));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeSensorState");
        return NULL;
    }
    packet_type->num_sensors = packet->payload[0];
    memcpy(&packet_type->sensor_states, packet->payload + 1, sizeof(packet_type->sensor_states));
    if (packet_type->num_sensors > 64)
    {
        ESP_LOGW(TAG_LORA, "Invalid sensor count: %u", (unsigned int)packet_type->num_sensors);
        free(packet_type);
        return NULL;
    }
    return packet_type;
}

PacketTypeAck *create_ack_information(DogDogPacket *packet)
{
    if (!packet_has_payload(packet, LORA_ACK, sizeof(PacketTypeAck)))
    {
        return NULL;
    }

    PacketTypeAck *packet_type = calloc(1, sizeof(PacketTypeAck));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeAck");
        return NULL;
    }
    packet_type->station_id = packet->payload[0];
    packet_type->packet_id = packet->payload[1];
    return packet_type;
}

DogDogPacket *create_dogdog_packet_from_time_sync_information(PacketTypeTimeSync *time_sync)
{
    if (time_sync == NULL)
    {
        return NULL;
    }

    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    packet->magic = LORA_MAGIC;
    packet->protocol_version = LORA_PROTOCOL_VERSION;
    packet->station_id = station_id;
    packet->packet_id = 0; // Set to 0 for now
    packet->type = LORA_TIME_SYNC;
    packet->payload_length = sizeof(int64_t);
    packet->payload = calloc(1, packet->payload_length);
    if (!packet->payload)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
        free(packet);
        return NULL;
    }
    memcpy(packet->payload, &time_sync->timestamp, sizeof(int64_t));

    return packet;
}

DogDogPacket *create_dogdog_packet_from_trigger_information(PacketTypeTrigger *trigger)
{
    if (trigger == NULL || trigger->sensor_state.num_sensors > 64)
    {
        ESP_LOGE(TAG_LORA, "Invalid trigger information");
        return NULL;
    }

    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    packet->magic = LORA_MAGIC;
    packet->protocol_version = LORA_PROTOCOL_VERSION;
    packet->station_id = station_id;
    packet->packet_id = 0; // Set to 0 for now
    packet->type = LORA_TRIGGER;
    packet->payload_length = sizeof(int64_t) + sizeof(PacketTypeSensorState);
    packet->payload = calloc(1, packet->payload_length);
    if (!packet->payload)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
        free(packet);
        return NULL;
    }
    memcpy(packet->payload, &trigger->timestamp, sizeof(trigger->timestamp));
    packet->payload[sizeof(int64_t) + offsetof(PacketTypeSensorState, num_sensors)] =
        trigger->sensor_state.num_sensors;
    memcpy(packet->payload + sizeof(int64_t) + offsetof(PacketTypeSensorState, sensor_states),
           &trigger->sensor_state.sensor_states,
           sizeof(trigger->sensor_state.sensor_states));

    return packet;
}

DogDogPacket *create_dogdog_packet_from_final_time_information(PacketTypeFinalTime *final_time)
{
    if (final_time == NULL)
    {
        return NULL;
    }

    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    packet->magic = LORA_MAGIC;
    packet->protocol_version = LORA_PROTOCOL_VERSION;
    packet->station_id = station_id;
    packet->packet_id = 0; // Set to 0 for now
    packet->type = LORA_FINAL_TIME;
    packet->payload_length = sizeof(int64_t);
    packet->payload = calloc(1, packet->payload_length);
    if (!packet->payload)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
        free(packet);
        return NULL;
    }
    memcpy(packet->payload, &final_time->timestamp, sizeof(int64_t));

    return packet;
}

DogDogPacket *create_dogdog_packet_from_sensor_state_information(PacketTypeSensorState *sensor_state)
{
    if (sensor_state == NULL || sensor_state->num_sensors > 64)
    {
        ESP_LOGE(TAG_LORA, "Invalid sensor state information");
        return NULL;
    }

    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    packet->magic = LORA_MAGIC;
    packet->protocol_version = LORA_PROTOCOL_VERSION;
    packet->station_id = station_id;
    packet->packet_id = 0; // Set to 0 for now
    packet->type = LORA_SENSOR_STATE;
    packet->payload_length = sizeof(uint8_t) + sizeof(uint64_t);
    packet->payload = calloc(1, packet->payload_length);
    if (!packet->payload)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
        free(packet);
        return NULL;
    }

    packet->payload[0] = sensor_state->num_sensors;
    memcpy(packet->payload + 1, &sensor_state->sensor_states, sizeof(uint64_t));

    return packet;
}

DogDogPacket *create_dogdog_packet_from_ack_information(PacketTypeAck *ack)
{
    if (ack == NULL)
    {
        return NULL;
    }

    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    packet->magic = LORA_MAGIC;
    packet->protocol_version = LORA_PROTOCOL_VERSION;
    packet->station_id = station_id;
    packet->packet_id = 0; // Set to 0 for now
    packet->type = LORA_ACK;
    packet->payload_length = sizeof(uint8_t) + sizeof(uint8_t);
    packet->payload = calloc(1, packet->payload_length);
    if (!packet->payload)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
        free(packet);
        return NULL;
    }

    packet->payload[0] = ack->station_id;
    packet->payload[1] = ack->packet_id;

    return packet;
}

DogDogPacket *create_dogdog_packet_from_request_final_time_information(uint8_t requested_station_id)
{
    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    packet->magic = LORA_MAGIC;
    packet->protocol_version = LORA_PROTOCOL_VERSION;
    packet->station_id = station_id;
    packet->packet_id = 0; // Set to 0 for now
    packet->type = LORA_REQUEST_FINAL_TIME;
    packet->payload_length = sizeof(uint8_t);
    packet->payload = calloc(1, packet->payload_length);
    if (!packet->payload)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
        free(packet);
        return NULL;
    }

    packet->payload[0] = requested_station_id;

    return packet;
}

void log_dogdog_packet(DogDogPacket *packet)
{
    if (packet == NULL)
    {
        return;
    }

    char *packet_type_str;
    switch (packet->type)
    {
    case LORA_TIME_SYNC:
        packet_type_str = "LORA_TIME_SYNC";
        break;
    case LORA_TRIGGER:
        packet_type_str = "LORA_TRIGGER";
        break;
    case LORA_START_FAKE_TIME:
        packet_type_str = "LORA_START_FAKE_TIME";
        break;
    case LORA_FINAL_TIME:
        packet_type_str = "LORA_FINAL_TIME";
        break;
    case LORA_SENSOR_STATE:
        packet_type_str = "LORA_SENSOR_STATE";
        break;
    case LORA_ACK:
        packet_type_str = "LORA_ACK";
        break;
    case LORA_REQUEST_FINAL_TIME:
        packet_type_str = "LORA_REQUEST_FINAL_TIME";
        break;
    default:
        packet_type_str = "UNKNOWN_TYPE";
    }

    ESP_LOGI(pcTaskGetName(NULL), "DogDogPacket: magic=0x%04X, protocol_version=%d, station_id=%d, packet_id=%d, type=%s, length=%d, rssi=%d, snr=%d",
             (unsigned int)packet->magic, packet->protocol_version, packet->station_id, packet->packet_id, packet_type_str, packet->payload_length, packet->rssi, packet->snr);
    if (packet->type == LORA_ACK && packet->payload_length == 2)
    {
        PacketTypeAck *ack = create_ack_information(packet);
        if (ack != NULL)
        {
            ESP_LOGI(pcTaskGetName(NULL), "ACK payload: station_id=%d, packet_id=%d", ack->station_id, ack->packet_id);
            free(ack);
        }
    }
    ESP_LOGD(pcTaskGetName(NULL), "Payload: ");
    for (int i = 0; packet->payload != NULL && i < packet->payload_length; i++)
    {
        ESP_LOGD(pcTaskGetName(NULL), "  [%d]: 0x%02X", i, packet->payload[i]);
    }
}

BaseType_t send_dogdog_packet(DogDogPacket *packet)
{
    if (packet != NULL && loraSendQueue != NULL)
    {
        return xQueueSend(loraSendQueue, &packet, 0);
    }
    return pdFAIL;
}

void LoraInterruptTask(void *pvParameters)
{
    (void)pvParameters;
    while (true)
    {
        int i = 0;
        if (xQueueReceive(loraInterruptQueue, &i, portMAX_DELAY))
        {
            timeval_t timestamp;
            gettimeofday(&timestamp, NULL);
            int64_t local_time_received = TIME_US(timestamp);
            /* RX_DONE is a level which remains asserted until the radio IRQ is
               cleared.  Keep the newest notification instead of dropping it:
               dropping the only queued notification would leave DIO1 high and
               prevent a future positive edge from ever waking the receiver. */
            BaseType_t sent = xQueueOverwrite(localReceiveTimestampQueue, &local_time_received);
            if (sent != pdTRUE)
            {
                ESP_LOGW(pcTaskGetName(NULL), "Warning: localReceiveTimestampQueue full, timestamp lost");
            }
        }
    }
}

void AckDispatchTask(void *pvParameters)
{
    (void)pvParameters;
    PacketTypeAck ack;

    while (true)
    {
        if (xQueueReceive(ackQueue, &ack, portMAX_DELAY))
        {
            DogDogPacket *packet = take_pending_ack_packet(ack.station_id, ack.packet_id);
            if (packet != NULL)
            {
                ESP_LOGI(pcTaskGetName(NULL),
                         "ACK received for station: %d packet: %d",
                         ack.station_id,
                         ack.packet_id);
                free_dogdog_packet(packet);
            }
            else
            {
                ESP_LOGW(pcTaskGetName(NULL), "ACK received for station: %d packet: %d, but no pending packet is waiting",
                         ack.station_id, ack.packet_id);
            }
        }
    }
}

esp_err_t init_lora(void)
{
    ESP_LOGI("LORA", "Initializing LoRa");
    esp_err_t err = LoRaInit();
    if (err != ESP_OK)
    {
        ESP_LOGE("LORA", "Failed to initialize LoRa SPI: %s", esp_err_to_name(err));
        return err;
    }

    int8_t txPowerInDbm = 22;
    uint32_t frequencyInHz = 868000000;
    ESP_LOGI("LORA", "Frequency is 868MHz");
    float tcxoVoltage = 3.3;     // use TCXO
    bool useRegulatorLDO = true; // use DCDC + LDO

    // LoRaDebugPrint(true);
    if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0)
    {
        ESP_LOGE("LORA", "Does not recognize the module");
        LoRaDeinit();
        return ESP_FAIL;
    }

    uint8_t spreadingFactor = 9;
    uint8_t bandwidth = 5;
    uint8_t codingRate = 1;
    uint16_t preambleLength = 8;
    uint8_t payloadLen = 0;
    bool crcOn = true;
    bool invertIrq = false;

    LoRaConfig(spreadingFactor, bandwidth, codingRate, preambleLength, payloadLen, crcOn, invertIrq);
    if (LoRaGetLastError() != ERR_NONE)
    {
        ESP_LOGE("LORA", "LoRa configuration failed with error %d", LoRaGetLastError());
        LoRaDeinit();
        return ESP_FAIL;
    }

    loraSendQueue = xQueueCreate(40, sizeof(DogDogPacket *));
    /* These are coalescing event signals, not packet FIFOs.  The SX126x has a
       single RX buffer and keeps DIO1 asserted until RX_DONE is cleared. */
    loraInterruptQueue = xQueueCreate(1, sizeof(int));
    localReceiveTimestampQueue = xQueueCreate(1, sizeof(int64_t));
    ackQueue = xQueueCreate(40, sizeof(PacketTypeAck));
    radio_operation_mutex = xSemaphoreCreateMutex();
    if (loraSendQueue == NULL || loraInterruptQueue == NULL ||
        localReceiveTimestampQueue == NULL || ackQueue == NULL ||
        radio_operation_mutex == NULL)
    {
        ESP_LOGE("LORA", "Failed to allocate LoRa queues");
        goto queue_init_failed;
    }

    TaskHandle_t interrupt_task = NULL;
    TaskHandle_t ack_task = NULL;
    if (xTaskCreate(LoraInterruptTask, "LoraInterruptTask", 2048, NULL, 24, &interrupt_task) != pdPASS ||
        xTaskCreate(AckDispatchTask, "AckDispatchTask", 3072, NULL, 24, &ack_task) != pdPASS ||
        xTaskCreate(ResendTask, "LoraResendTask", 3072, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE("LORA", "Failed to create LoRa support tasks");
        if (interrupt_task != NULL)
        {
            vTaskDelete(interrupt_task);
        }
        if (ack_task != NULL)
        {
            vTaskDelete(ack_task);
        }
        goto queue_init_failed;
    }

    return ESP_OK;

queue_init_failed:
    if (loraSendQueue != NULL)
    {
        vQueueDelete(loraSendQueue);
        loraSendQueue = NULL;
    }
    if (loraInterruptQueue != NULL)
    {
        vQueueDelete(loraInterruptQueue);
        loraInterruptQueue = NULL;
    }
    if (localReceiveTimestampQueue != NULL)
    {
        vQueueDelete(localReceiveTimestampQueue);
        localReceiveTimestampQueue = NULL;
    }
    if (ackQueue != NULL)
    {
        vQueueDelete(ackQueue);
        ackQueue = NULL;
    }
    if (radio_operation_mutex != NULL)
    {
        vSemaphoreDelete(radio_operation_mutex);
        radio_operation_mutex = NULL;
    }
    LoRaDeinit();
    return ESP_ERR_NO_MEM;
}

int create_bytes_from_dogdog_packet(DogDogPacket *packet, uint8_t *buf, size_t buf_len)
{
    if (packet == NULL || buf == NULL ||
        (packet->payload_length > 0 && packet->payload == NULL) ||
        packet->payload_length > LORA_MAX_FRAME_LEN - LORA_PACKET_HEADER_LEN ||
        buf_len < (size_t)packet->payload_length + LORA_PACKET_HEADER_LEN)
    {
        ESP_LOGE(TAG_LORA, "Invalid packet or output buffer");
        return -1;
    }

    memcpy(buf, &packet->magic, sizeof(packet->magic));
    buf[4] = packet->protocol_version;
    buf[5] = packet->station_id;
    buf[6] = packet->packet_id;
    buf[7] = packet->type;
    buf[8] = (packet->payload_length >> 8) & 0xFF; // High byte
    buf[9] = packet->payload_length & 0xFF;        // Low byte

    if (packet->payload_length > 0)
    {
        memcpy(buf + LORA_PACKET_HEADER_LEN, packet->payload, packet->payload_length);
    }
    return packet->payload_length + LORA_PACKET_HEADER_LEN;
}

static void IRAM_ATTR lora_module_rx_isr(void *arg)
{
    (void)arg;
    int i = 0;
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (loraInterruptQueue != NULL)
    {
        xQueueOverwriteFromISR(loraInterruptQueue, &i, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }
}

void LoraReceiveTask(void *pvParameters)
{
    TaskHandle_t startup_waiter = (TaskHandle_t)pvParameters;
    esp_err_t err = gpio_reset_pin(CONFIG_LORA_GPIO_DIO1);
    if (err == ESP_OK)
    {
        err = gpio_set_direction(CONFIG_LORA_GPIO_DIO1, GPIO_MODE_INPUT);
    }
    if (err == ESP_OK)
    {
        err = gpio_set_intr_type(CONFIG_LORA_GPIO_DIO1, GPIO_INTR_POSEDGE);
    }
    if (err == ESP_OK)
    {
        err = gpio_isr_handler_add(CONFIG_LORA_GPIO_DIO1,
                                   lora_module_rx_isr,
                                   (void *)(intptr_t)CONFIG_LORA_GPIO_DIO1);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_LORA, "Failed to configure LoRa receive interrupt: %s", esp_err_to_name(err));
        if (startup_waiter != NULL)
        {
            (void)xTaskNotify(startup_waiter, (uint32_t)err, eSetValueWithOverwrite);
        }
        vTaskDelete(NULL);
        return;
    }

    if (localReceiveTimestampQueue == NULL)
    {
        ESP_LOGE(TAG_LORA, "LoRa receive timestamp queue is not initialized");
        err = ESP_ERR_INVALID_STATE;
        (void)gpio_intr_disable(CONFIG_LORA_GPIO_DIO1);
        (void)gpio_isr_handler_remove(CONFIG_LORA_GPIO_DIO1);
        if (startup_waiter != NULL)
        {
            (void)xTaskNotify(startup_waiter, (uint32_t)err, eSetValueWithOverwrite);
        }
        vTaskDelete(NULL);
        return;
    }

    /* DIO1 may already be high if a packet arrived before this task installed the ISR. */
    if (gpio_get_level(CONFIG_LORA_GPIO_DIO1) != 0)
    {
        int pending_interrupt = 0;
        if (xQueueOverwrite(loraInterruptQueue, &pending_interrupt) != pdTRUE)
        {
            ESP_LOGW(TAG_LORA, "Could not queue the pending LoRa interrupt");
        }
    }

    if (startup_waiter != NULL)
    {
        (void)xTaskNotify(startup_waiter, (uint32_t)ESP_OK, eSetValueWithOverwrite);
    }

    ESP_LOGI(pcTaskGetName(NULL), "Starting");
    uint8_t buf[255]; // Maximum Payload size of SX1261/62/68 is 255
    while (1)
    {
        int64_t local_time_received;
        if (xQueueReceive(localReceiveTimestampQueue, &local_time_received, portMAX_DELAY))
        {
            if (radio_operation_mutex == NULL ||
                xSemaphoreTake(radio_operation_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
            {
                ESP_LOGE(TAG_LORA, "Timed out waiting for exclusive LoRa access while receiving");
                if (xQueueOverwrite(localReceiveTimestampQueue, &local_time_received) != pdTRUE)
                {
                    ESP_LOGE(TAG_LORA, "Could not retain pending LoRa receive event");
                }
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            uint8_t rxLen = LoRaReceive(buf, sizeof(buf));
            int8_t packet_rssi = 0;
            int8_t packet_snr = 0;
            if (rxLen > 0)
            {
                GetPacketStatus(&packet_rssi, &packet_snr);
            }
            xSemaphoreGive(radio_operation_mutex);

            /* If clearing RX_DONE failed (or a new packet arrived while this
               one was read), DIO1 is still asserted and no new positive edge
               will occur.  Re-arm the coalesced software event explicitly. */
            if (gpio_get_level(CONFIG_LORA_GPIO_DIO1) != 0)
            {
                timeval_t retry_timestamp;
                gettimeofday(&retry_timestamp, NULL);
                int64_t retry_received = TIME_US(retry_timestamp);
                if (xQueueOverwrite(localReceiveTimestampQueue, &retry_received) != pdTRUE)
                {
                    ESP_LOGE(TAG_LORA, "Could not retain asserted LoRa receive event");
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            if (rxLen > 0)
            {
                if (!is_packet_from_dogdog(buf, rxLen))
                {
                    ESP_LOGW(pcTaskGetName(NULL), "Received packet is not from DogDog");
                    continue;
                }

                DogDogPacket *packet = create_dogdog_packet_from_bytes(buf, rxLen);
                if (!packet)
                {
                    ESP_LOGE(TAG_LORA, "Failed to allocate DogDogPacket");
                    continue;
                }
                packet->local_time_received = local_time_received;
                packet->rssi = packet_rssi;
                packet->snr = packet_snr;

                if (packet->station_id != controller_id && packet->station_id != start_id && packet->station_id != stop_id)
                {
                    ESP_LOGW(pcTaskGetName(NULL), "Received packet is not from a valid station");
                    free(packet->payload);
                    free(packet);
                    continue;
                }

                log_dogdog_packet(packet);

                if (handle_dogdog_packet != NULL)
                {
                    handle_dogdog_packet(packet);
                }
                else
                {
                    ESP_LOGE(pcTaskGetName(NULL), "No handler for received DogDogPacket set, dropping packet");
                }

                free(packet->payload);
                free(packet);
            }
        }
    }
}

bool requires_ack(DogDogPacket *packet)
{
    return packet != NULL &&
           (packet->type == LORA_TRIGGER || packet->type == LORA_FINAL_TIME ||
            packet->type == LORA_REQUEST_FINAL_TIME);
}

void LoraSendTask(void *pvParameters)
{
    (void)pvParameters;
    if (loraSendQueue == NULL)
    {
        ESP_LOGE(TAG_LORA, "LoRa send queue is not initialized");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(pcTaskGetName(NULL), "Starting");
    uint8_t buf[255]; // Maximum Payload size of SX1261/62/68 is 255
    uint8_t packet_id = 0;

    while (true)
    {
        DogDogPacket *packet = NULL;
        if (xQueueReceive(loraSendQueue, &packet, portMAX_DELAY) == pdTRUE)
        {
            if (packet == NULL)
            {
                ESP_LOGW(TAG_LORA, "Ignoring null packet from LoRa send queue");
                continue;
            }

            if (packet->retries == 0)
            {
                packet->packet_id = packet_id++;
            }

            // Prepare the buffer for transmission
            if (packet->type == LORA_TIME_SYNC)
            {
                if (packet->payload == NULL || packet->payload_length != sizeof(int64_t))
                {
                    ESP_LOGE(pcTaskGetName(NULL), "Invalid time-sync packet payload");
                    free_dogdog_packet(packet);
                    continue;
                }
                struct timeval tv;
                gettimeofday(&tv, NULL);
                int64_t timestamp = TIME_US(tv);
                memcpy(packet->payload, &timestamp, sizeof(int64_t));
            }

            int txLen = create_bytes_from_dogdog_packet(packet, buf, sizeof(buf));
            if (txLen < 0)
            {
                ESP_LOGE(pcTaskGetName(NULL), "Failed to create packet");
                free_dogdog_packet(packet);
                continue;
            }

            log_dogdog_packet(packet);

            bool retained_for_ack = false;
            if (requires_ack(packet))
            {
                uint8_t attempt = (uint8_t)(packet->retries + 1U);
                packet->retries = attempt;
                if (attempt < MAX_SEND_ATTEMPTS)
                {
                    retained_for_ack = register_pending_ack(packet);
                    if (!retained_for_ack)
                    {
                        ESP_LOGE(pcTaskGetName(NULL),
                                 "No unique pending ACK slot available for station: %d packet: %d",
                                 packet->station_id,
                                 packet->packet_id);
                    }
                }
                else
                {
                    ESP_LOGW(pcTaskGetName(NULL),
                             "No ACK after %u send attempts for station: %d packet: %d",
                             (unsigned int)attempt,
                             packet->station_id,
                             packet->packet_id);
                }
            }

            // A transmit and a receive are multi-command radio operations. Keep
            // their command sequences from interleaving across the two tasks.
            bool sent = false;
            if (radio_operation_mutex != NULL &&
                xSemaphoreTake(radio_operation_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
            {
                sent = LoRaSend(buf, txLen, SX126x_TXMODE_SYNC);
                xSemaphoreGive(radio_operation_mutex);
            }
            else
            {
                ESP_LOGE(pcTaskGetName(NULL), "Timed out waiting for exclusive LoRa access while sending");
            }
            if (!sent)
            {
                ESP_LOGE(pcTaskGetName(NULL), "LoRaSend fail");
            }

            if (!retained_for_ack)
            {
                free_dogdog_packet(packet);
            }

            int lost = GetPacketLost();
            if (lost != 0)
            {
                ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
            }
        }
    }
}

void populate_sensor_status(SensorStatus *sensorStatus, PacketTypeSensorState *sensor_state, uint8_t station_id, bool is_trigger)
{
    if (sensorStatus == NULL)
    {
        return;
    }

    sensorStatus->sensor = station_id == start_id ? SENSOR_START : SENSOR_STOP;
    sensorStatus->num_sensors = 0;
    sensorStatus->status = NULL;
    sensorStatus->is_trigger = is_trigger;

    if (sensor_state == NULL || sensor_state->num_sensors > 64)
    {
        ESP_LOGE(TAG_LORA, "Invalid sensor state");
        return;
    }

    sensorStatus->num_sensors = sensor_state->num_sensors;
    if (sensorStatus->num_sensors > 0)
    {
        sensorStatus->status = calloc(sensorStatus->num_sensors, sizeof(bool));
        if (sensorStatus->status == NULL)
        {
            ESP_LOGE(TAG_LORA, "Failed to allocate sensor status");
            sensorStatus->num_sensors = 0;
            return;
        }
    }

    ESP_LOGI(pcTaskGetName(NULL), "Connected sensors amount: %d, is_trigger: %d", sensorStatus->num_sensors, sensorStatus->is_trigger);
    for (int i = 0; i < sensorStatus->num_sensors; i++)
    {
        sensorStatus->status[i] = (sensor_state->sensor_states & (1ULL << i)) != 0;
    }
}

void ResendTask(void *pvParameters)
{
    (void)pvParameters;
    DogDogPacket *expired[MAX_PENDING_ACKS];

    while (true)
    {
        size_t count = take_expired_ack_packets(expired, MAX_PENDING_ACKS);
        for (size_t i = 0; i < count; i++)
        {
            DogDogPacket *packet = expired[i];
            ESP_LOGW(pcTaskGetName(NULL),
                     "No ACK received for station: %d packet: %d, resending",
                     packet->station_id,
                     packet->packet_id);
            if (xQueueSend(loraSendQueue, &packet, 0) != pdTRUE)
            {
                ESP_LOGE(pcTaskGetName(NULL), "LoRa send queue full; dropping expired packet");
                free_dogdog_packet(packet);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ACK_SCAN_INTERVAL_MS));
    }
}

void InitLoraHandlers(void (*function)(DogDogPacket *packet))
{
    handle_dogdog_packet = function;
}
