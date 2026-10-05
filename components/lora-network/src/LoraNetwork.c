#include "LoraNetwork.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
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

#define MAX_PENDING_ACKS 40

typedef struct PendingAck
{
    bool active;
    uint8_t station_id;
    uint8_t packet_id;
    TaskHandle_t task;
} PendingAck;

static PendingAck pending_acks[MAX_PENDING_ACKS];
// A task mutex protects notification targets until notification has completed.
static SemaphoreHandle_t pending_ack_mutex;

extern int controller_id;
extern int start_id;
extern int stop_id;
extern int station_id;

static bool register_pending_ack(uint8_t ack_station_id, uint8_t ack_packet_id, TaskHandle_t task)
{
    bool registered = false;

    xSemaphoreTake(pending_ack_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (!pending_acks[i].active)
        {
            pending_acks[i].active = true;
            pending_acks[i].station_id = ack_station_id;
            pending_acks[i].packet_id = ack_packet_id;
            pending_acks[i].task = task;
            registered = true;
            break;
        }
    }
    xSemaphoreGive(pending_ack_mutex);

    return registered;
}

static void unregister_pending_ack(uint8_t ack_station_id, uint8_t ack_packet_id, TaskHandle_t task)
{
    xSemaphoreTake(pending_ack_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (pending_acks[i].active &&
            pending_acks[i].station_id == ack_station_id &&
            pending_acks[i].packet_id == ack_packet_id &&
            pending_acks[i].task == task)
        {
            pending_acks[i].active = false;
            break;
        }
    }
    xSemaphoreGive(pending_ack_mutex);
}

static bool dispatch_pending_ack(uint8_t ack_station_id, uint8_t ack_packet_id)
{
    bool dispatched = false;

    xSemaphoreTake(pending_ack_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PENDING_ACKS; i++)
    {
        if (pending_acks[i].active &&
            pending_acks[i].station_id == ack_station_id &&
            pending_acks[i].packet_id == ack_packet_id)
        {
            // The retry task cannot unregister/delete itself while this mutex is held.
            xTaskNotifyGive(pending_acks[i].task);
            pending_acks[i].active = false;
            dispatched = true;
            break;
        }
    }
    xSemaphoreGive(pending_ack_mutex);

    return dispatched;
}

DogDogPacket *create_dogdog_packet_from_bytes(uint8_t *data, uint16_t length)
{
    if (data == NULL || length < 10)
    {
        return NULL;
    }

    uint32_t magic;
    memcpy(&magic, data, sizeof(magic));
    uint16_t payload_length = (data[8] << 8) | data[9];
    if (magic != LORA_MAGIC || payload_length != length - 10)
    {
        ESP_LOGW(TAG_LORA, "Invalid packet header or payload length");
        return NULL;
    }

    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    // First four bytes of data is magic
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

    packet->payload = calloc(1, packet->payload_length);
    if (!packet->payload)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for packet payload");
        free(packet);
        return NULL;
    }
    memcpy(packet->payload, data + 10, packet->payload_length);

    return packet;
}

bool is_packet_from_dogdog(uint8_t *data)
{
    uint32_t magic;
    memcpy(&magic, data, sizeof(magic));
    return magic == LORA_MAGIC;
}

static bool has_payload_length(const DogDogPacket *packet, size_t expected_length)
{
    return packet != NULL && packet->payload != NULL && packet->payload_length == expected_length;
}

PacketTypeTimeSync *create_time_sync_information(DogDogPacket *packet)
{
    if (!has_payload_length(packet, sizeof(int64_t)))
    {
        return NULL;
    }
    PacketTypeTimeSync *packet_type = calloc(1, sizeof(PacketTypeTimeSync));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeTimeSync");
        return NULL;
    }
    // four bytes of the packet payload are int64 time stamp
    memcpy(&packet_type->timestamp, packet->payload, sizeof(packet_type->timestamp));
    return packet_type;
}

PacketTypeTrigger *create_trigger_information(DogDogPacket *packet)
{
    if (!has_payload_length(packet, sizeof(int64_t) + sizeof(PacketTypeSensorState)))
    {
        return NULL;
    }
    PacketTypeTrigger *packet_type = calloc(1, sizeof(PacketTypeTrigger));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeTrigger");
        return NULL;
    }
    // four bytes of the packet payload are int64 time stamp
    memcpy(&packet_type->timestamp, packet->payload, sizeof(packet_type->timestamp));
    memcpy(&packet_type->sensor_state, packet->payload + sizeof(int64_t), sizeof(PacketTypeSensorState));
    return packet_type;
}

PacketTypeFinalTime *create_final_time_information(DogDogPacket *packet)
{
    if (!has_payload_length(packet, sizeof(int64_t)))
    {
        return NULL;
    }
    PacketTypeFinalTime *packet_type = calloc(1, sizeof(PacketTypeFinalTime));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeFinalTime");
        return NULL;
    }
    // four bytes of the packet payload are int64 time stamp
    memcpy(&packet_type->timestamp, packet->payload, sizeof(packet_type->timestamp));
    return packet_type;
}

PacketTypeSensorState *create_sensor_state_information(DogDogPacket *packet)
{
    if (!has_payload_length(packet, sizeof(uint8_t) + sizeof(uint64_t)))
    {
        return NULL;
    }
    PacketTypeSensorState *packet_type = calloc(1, sizeof(PacketTypeSensorState));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeSensorState");
        return NULL;
    }
    // first byte of the packet payload is the number of sensors
    packet_type->num_sensors = packet->payload[0];
    // remaining bytes are the sensor states
    memcpy(&packet_type->sensor_states, packet->payload + 1, sizeof(packet_type->sensor_states));
    return packet_type;
}

PacketTypeAck *create_ack_information(DogDogPacket *packet)
{
    if (!has_payload_length(packet, sizeof(PacketTypeAck)))
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
    memcpy(packet->payload, &trigger->timestamp, sizeof(int64_t));
    memcpy(packet->payload + sizeof(int64_t), &trigger->sensor_state, sizeof(PacketTypeSensorState));

    return packet;
}

DogDogPacket *create_dogdog_packet_from_final_time_information(PacketTypeFinalTime *final_time)
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
    for (int i = 0; i < packet->payload_length; i++)
    {
        ESP_LOGD(pcTaskGetName(NULL), "  [%d]: 0x%02X", i, packet->payload[i]);
    }
}

BaseType_t send_dogdog_packet(DogDogPacket *packet)
{
    if (packet != NULL)
    {
        return xQueueSend(loraSendQueue, &packet, 0);
    }
    return pdFAIL;
}

void LoraInterruptTask(void *pvParameters)
{
    while (true)
    {
        int i = 0;
        if (xQueueReceive(loraInterruptQueue, &i, portMAX_DELAY))
        {
            timeval_t timestamp;
            gettimeofday(&timestamp, NULL);
            int64_t local_time_received = TIME_US(timestamp);
            BaseType_t sent = xQueueSend(localReceiveTimestampQueue, &local_time_received, 0);
            if (sent != pdTRUE)
            {
                ESP_LOGW(pcTaskGetName(NULL), "Warning: localReceiveTimestampQueue full, timestamp lost");
            }
        }
    }
}

void AckDispatchTask(void *pvParameters)
{
    PacketTypeAck ack;

    while (true)
    {
        if (xQueueReceive(ackQueue, &ack, portMAX_DELAY))
        {
            if (dispatch_pending_ack(ack.station_id, ack.packet_id))
            {
                ESP_LOGI(pcTaskGetName(NULL), "Dispatching ACK for station: %d packet: %d", ack.station_id, ack.packet_id);
            }
            else
            {
                ESP_LOGW(pcTaskGetName(NULL), "ACK received for station: %d packet: %d, but no pending packet is waiting",
                         ack.station_id, ack.packet_id);
            }
        }
    }
}

void init_lora(void)
{
    ESP_LOGI("LORA", "Initializing LoRa");
    loraSendQueue = xQueueCreate(40, sizeof(DogDogPacket *));
    loraInterruptQueue = xQueueCreate(10, sizeof(int));
    localReceiveTimestampQueue = xQueueCreate(40, sizeof(int64_t));
    pending_ack_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(loraSendQueue && loraInterruptQueue && localReceiveTimestampQueue && pending_ack_mutex ? ESP_OK : ESP_ERR_NO_MEM);
    xTaskCreate(LoraInterruptTask, "LoraInterruptTask", 8192, NULL, 24, NULL);
    LoRaInit();
    int8_t txPowerInDbm = 22;
    uint32_t frequencyInHz = 868000000;
    ESP_LOGI("LORA", "Frequency is 868MHz");
    float tcxoVoltage = 3.3;     // use TCXO
    bool useRegulatorLDO = true; // use DCDC + LDO

    // LoRaDebugPrint(true);
    if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0)
    {
        ESP_LOGE("LORA", "Does not recognize the module");
        while (1)
        {
            vTaskDelay(1);
        }
    }

    uint8_t spreadingFactor = 9;
    uint8_t bandwidth = 5;
    uint8_t codingRate = 1;
    uint16_t preambleLength = 8;
    uint8_t payloadLen = 0;
    bool crcOn = true;
    bool invertIrq = false;

    LoRaConfig(spreadingFactor, bandwidth, codingRate, preambleLength, payloadLen, crcOn, invertIrq);
    ackQueue = xQueueCreate(40, sizeof(PacketTypeAck));
    ESP_ERROR_CHECK(ackQueue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    xTaskCreate(AckDispatchTask, "AckDispatchTask", 4048, NULL, 24, NULL);
}

int create_bytes_from_dogdog_packet(DogDogPacket *packet, uint8_t *buf, size_t buf_len)
{
    if (packet == NULL || buf == NULL || (packet->payload_length > 0 && packet->payload == NULL) ||
        buf_len < packet->payload_length + 10)
    {
        ESP_LOGE(TAG_LORA, "Buffer too small to hold the packet");
        return -1;
    }

    memcpy(buf, &packet->magic, sizeof(packet->magic));
    buf[4] = packet->protocol_version;
    buf[5] = packet->station_id;
    buf[6] = packet->packet_id;
    buf[7] = packet->type;
    buf[8] = (packet->payload_length >> 8) & 0xFF; // High byte
    buf[9] = packet->payload_length & 0xFF;        // Low byte

    memcpy(buf + 10, packet->payload, packet->payload_length);
    return packet->payload_length + 10; // Return total length of the buffer
}

static void IRAM_ATTR lora_module_rx_isr(void *arg)
{
    int i = 0;
    xQueueSendFromISR(loraInterruptQueue, &i, NULL);
}

void LoraReceiveTask(void *pvParameters)
{
    gpio_reset_pin(CONFIG_LORA_GPIO_DIO1);
    gpio_set_direction(CONFIG_LORA_GPIO_DIO1, GPIO_MODE_INPUT);
    gpio_set_intr_type(CONFIG_LORA_GPIO_DIO1, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(CONFIG_LORA_GPIO_DIO1, lora_module_rx_isr, (void *)CONFIG_LORA_GPIO_DIO1);

    ESP_LOGI(pcTaskGetName(NULL), "Starting");
    uint8_t buf[255]; // Maximum Payload size of SX1261/62/68 is 255
    while (1)
    {
        int64_t local_time_received;
        if (xQueueReceive(localReceiveTimestampQueue, &local_time_received, portMAX_DELAY))
        {
            // DIO1 also signals CRC/header errors; LoRaReceive discards those and their timestamps.
            uint8_t rxLen = LoRaReceive(buf, sizeof(buf));
            if (rxLen > 0)
            {
                if (rxLen < 10 || is_packet_from_dogdog(buf) == false)
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
                GetPacketStatus(&packet->rssi, &packet->snr);

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
    return packet->type == LORA_TRIGGER || packet->type == LORA_FINAL_TIME || packet->type == LORA_REQUEST_FINAL_TIME;
}

void LoraSendTask(void *pvParameters)
{

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
                ESP_LOGW(pcTaskGetName(NULL), "Ignoring null outgoing packet");
                continue;
            }
            if (packet->retries == 0)
            {
                packet->packet_id = packet_id++;
            }

            // Prepare the buffer for transmission
            if (packet->type == LORA_TIME_SYNC)
            {
                if (!has_payload_length(packet, sizeof(int64_t)))
                {
                    free(packet->payload);
                    free(packet);
                    continue;
                }
                struct timeval tv;
                gettimeofday(&tv, NULL);
                int64_t timestamp = TIME_US(tv);
                memcpy(packet->payload, &timestamp, sizeof(int64_t));
            }

            log_dogdog_packet(packet);

            int txLen = create_bytes_from_dogdog_packet(packet, buf, sizeof(buf));
            if (txLen < 0)
            {
                ESP_LOGE(pcTaskGetName(NULL), "Failed to create packet");
                free(packet->payload);
                free(packet);
                continue;
            }
            // clean up packet
            if (!requires_ack(packet) || packet->retries >= 6)
            {
                free(packet->payload);
                free(packet);
            }
            else
            {
                if (packet->retries >= 6)
                {
                    ESP_LOGW(pcTaskGetName(NULL), "Packet of type LORA_TRIGGER has been retried too many times, deleting packet");
                    free(packet->payload);
                    free(packet);
                    continue;
                }
                packet->retries++;
                TaskHandle_t resend_task = NULL;
                BaseType_t resend_task_created = xTaskCreate(ResendTask, "ResendTask", 4048, packet, 5, &resend_task);
                if (resend_task_created != pdPASS || resend_task == NULL)
                {
                    ESP_LOGE(pcTaskGetName(NULL), "Failed to create resend task for station: %d packet: %d", packet->station_id, packet->packet_id);
                    free(packet->payload);
                    free(packet);
                }
            }

            // Wait for transmission to complete
            if (LoRaSend(buf, txLen, SX126x_TXMODE_SYNC) == false)
            {
                ESP_LOGE(pcTaskGetName(NULL), "LoRaSend fail");
            }

            int lost = GetPacketLost();
            if (lost != 0)
            {
                ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
            }
        }
    }
}

bool populate_sensor_status(SensorStatus *sensorStatus, PacketTypeSensorState *sensor_state, uint8_t station_id, bool is_trigger)
{
    memset(sensorStatus, 0, sizeof(*sensorStatus));
    if (sensor_state == NULL || sensor_state->num_sensors > 64)
    {
        return false;
    }
    sensorStatus->sensor = station_id == start_id ? SENSOR_START : SENSOR_STOP;
    sensorStatus->num_sensors = sensor_state->num_sensors;
    sensorStatus->is_trigger = is_trigger;
    if (sensorStatus->num_sensors == 0)
    {
        return true;
    }
    sensorStatus->status = calloc(sensorStatus->num_sensors, sizeof(bool));
    if (sensorStatus->status == NULL)
    {
        sensorStatus->num_sensors = 0;
        return false;
    }

    ESP_LOGI(pcTaskGetName(NULL), "Connected sensors amount: %d, is_trigger: %d", sensorStatus->num_sensors, sensorStatus->is_trigger);
    for (int i = 0; i < sensorStatus->num_sensors; i++)
    {
        sensorStatus->status[i] = (sensor_state->sensor_states & (1ULL << i)) != 0;
    }
    return true;
}

void ResendTask(void *pvParameters)
{
    DogDogPacket *waiting_for_ack = (DogDogPacket *)pvParameters;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    // Register from the owning task so the sender never reads a packet after transfer.
    if (!register_pending_ack(waiting_for_ack->station_id, waiting_for_ack->packet_id, self))
    {
        ESP_LOGE(pcTaskGetName(NULL), "No pending ACK slot available");
        free(waiting_for_ack->payload);
        free(waiting_for_ack);
        vTaskDelete(NULL);
        return;
    }

    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
    // Serialize with dispatch before freeing the packet or deleting this task.
    unregister_pending_ack(waiting_for_ack->station_id, waiting_for_ack->packet_id, self);
    if (notified > 0 || ulTaskNotifyTake(pdTRUE, 0) > 0)
    {
        ESP_LOGI(pcTaskGetName(NULL), "ACK received for station: %d packet: %d, deleting task",
                 waiting_for_ack->station_id, waiting_for_ack->packet_id);
        free(waiting_for_ack->payload);
        free(waiting_for_ack);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(pcTaskGetName(NULL), "No ACK received for station: %d packet: %d, resending",
             waiting_for_ack->station_id, waiting_for_ack->packet_id);

    if (xQueueSend(loraSendQueue, &waiting_for_ack, portMAX_DELAY) != pdTRUE)
    {
        free(waiting_for_ack->payload);
        free(waiting_for_ack);
    }

    vTaskDelete(NULL);
}

void InitLoraHandlers(void (*function)(DogDogPacket *packet))
{
    handle_dogdog_packet = function;
}
