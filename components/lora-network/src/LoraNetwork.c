#include "LoraNetwork.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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

DogDogPacket *create_dogdog_packet_from_bytes(uint8_t *data, uint16_t length)
{
    DogDogPacket *packet = calloc(1, sizeof(DogDogPacket));
    if (!packet)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for DogDogPacket");
        return NULL;
    }

    // First four bytes of data is magic
    packet->magic = *((uint32_t *)data);
    packet->protocol_version = data[4];
    packet->station_id = data[5];
    packet->packet_id = data[6];
    packet->type = data[7];
    packet->payload_length = (data[8] << 8) | data[9];

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
    return *((uint32_t *)data) == LORA_MAGIC;
}

PacketTypeTimeSync *create_time_sync_information(DogDogPacket *packet)
{
    PacketTypeTimeSync *packet_type = calloc(1, sizeof(PacketTypeTimeSync));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeTimeSync");
        return NULL;
    }
    // four bytes of the packet payload are int64 time stamp
    packet_type->timestamp = *((int64_t *)packet->payload);
    return packet_type;
}

PacketTypeTrigger *create_trigger_information(DogDogPacket *packet)
{
    PacketTypeTrigger *packet_type = calloc(1, sizeof(PacketTypeTrigger));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeTrigger");
        return NULL;
    }
    // four bytes of the packet payload are int64 time stamp
    packet_type->timestamp = *((int64_t *)packet->payload);
    memcpy(&packet_type->sensor_state, packet->payload + sizeof(int64_t), sizeof(PacketTypeSensorState));
    return packet_type;
}

PacketTypeFinalTime *create_final_time_information(DogDogPacket *packet)
{
    PacketTypeFinalTime *packet_type = calloc(1, sizeof(PacketTypeFinalTime));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeFinalTime");
        return NULL;
    }
    // four bytes of the packet payload are int64 time stamp
    packet_type->timestamp = ((int64_t *)packet->payload);
    return packet_type;
}

PacketTypeSensorState *create_sensor_state_information(DogDogPacket *packet)
{
    PacketTypeSensorState *packet_type = calloc(1, sizeof(PacketTypeSensorState));
    if (!packet_type)
    {
        ESP_LOGE(TAG_LORA, "Failed to allocate memory for PacketTypeSensorState");
        return NULL;
    }
    // first byte of the packet payload is the number of sensors
    packet_type->num_sensors = packet->payload[0];
    // remaining bytes are the sensor states
    packet_type->sensor_states = *((uint64_t *)(packet->payload + 1));
    return packet_type;
}

PacketTypeAck *create_ack_information(DogDogPacket *packet)
{

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
    packet->station_id = LORA_STATION_ID;
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
    packet->station_id = LORA_STATION_ID;
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
    packet->station_id = LORA_STATION_ID;
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
    packet->station_id = LORA_STATION_ID;
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
    packet->station_id = LORA_STATION_ID;
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
    default:
        packet_type_str = "UNKNOWN_TYPE";
    }

    ESP_LOGI(pcTaskGetName(NULL), "DogDogPacket: magic=0x%04X, protocol_version=%d, station_id=%d, packet_id=%d, type=%s, length=%d, rssi=%d, snr=%d",
             (unsigned int)packet->magic, packet->protocol_version, packet->station_id, packet->packet_id, packet_type_str, packet->payload_length, packet->rssi, packet->snr);
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
            BaseType_t sent = xQueueSendFromISR(localReceiveTimestampQueue, &local_time_received, NULL);
            if (sent != pdTRUE)
            {
                ESP_LOGW(pcTaskGetName(NULL), "Warning: localReceiveTimestampQueue full, timestamp lost");
            }
        }
    }
}

void init_lora(void)
{
    ESP_LOGI("LORA", "Initializing LoRa");
    loraSendQueue = xQueueCreate(40, sizeof(DogDogPacket *));
    loraInterruptQueue = xQueueCreate(10, sizeof(int));
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
    ackQueue = xQueueCreate(3, sizeof(uint8_t));
}

int create_bytes_from_dogdog_packet(DogDogPacket *packet, uint8_t *buf, size_t buf_len)
{
    if (buf_len < packet->payload_length + 10)
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

    localReceiveTimestampQueue = xQueueCreate(40, sizeof(int64_t));

    ESP_LOGI(pcTaskGetName(NULL), "Starting");
    uint8_t buf[255]; // Maximum Payload size of SX1261/62/68 is 255
    while (1)
    {
        int64_t local_time_received;
        if (xQueueReceive(localReceiveTimestampQueue, &local_time_received, portMAX_DELAY))
        {
            uint8_t rxLen = LoRaReceive(buf, sizeof(buf));
            if (rxLen > 0)
            {
                if (is_packet_from_dogdog(buf) == false)
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

                if (packet->station_id != CONTROLLER_ID && packet->station_id != START_ID && packet->station_id != STOP_ID)
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
            if (packet->retries == 0)
            {
                packet->packet_id = packet_id++;
            }

            // Prepare the buffer for transmission
            if (packet->type == LORA_TIME_SYNC)
            {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                int64_t timestamp = TIME_US(tv);
                memcpy(packet->payload, &timestamp, sizeof(int64_t));
            }

            log_dogdog_packet(packet);

            int txLen = create_bytes_from_dogdog_packet(packet, buf, sizeof(buf));
            // clean up packet
            if (packet->type != LORA_TRIGGER)
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
                xTaskCreate(ResendTask, "ResendTask", 4048, packet, 5, NULL);
            }

            if (txLen < 0)
            {
                ESP_LOGE(pcTaskGetName(NULL), "Failed to create packet");
                continue;
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

void populate_sensor_status(SensorStatus *sensorStatus, PacketTypeSensorState *sensor_state, uint8_t station_id, bool is_trigger)
{
    sensorStatus->sensor = station_id == START_ID ? SENSOR_START : SENSOR_STOP;
    sensorStatus->num_sensors = sensor_state->num_sensors;
    sensorStatus->status = calloc(sensorStatus->num_sensors, sizeof(bool));
    sensorStatus->is_trigger = is_trigger;

    ESP_LOGI(pcTaskGetName(NULL), "Connected sensors amount: %d, is_trigger: %d", sensorStatus->num_sensors, sensorStatus->is_trigger);
    for (int i = 0; i < sensorStatus->num_sensors; i++)
    {
        sensorStatus->status[i] = (sensor_state->sensor_states & (1ULL << i)) != 0;
    }
}

void ResendTask(void *pvParameters)
{
    DogDogPacket *waiting_for_ack = (DogDogPacket *)pvParameters;

    uint8_t packetid;
    vTaskDelay(pdMS_TO_TICKS(500));
    for (int i = 0; i < 3; i++)
    {
        if (xQueueReceive(ackQueue, &packetid, pdMS_TO_TICKS(500)))
        {
            ESP_LOGI(pcTaskGetName(NULL), "ACK received for packet: %d, waiting for %d", packetid, waiting_for_ack->packet_id);
            if (waiting_for_ack->packet_id == packetid)
            {
                ESP_LOGI(pcTaskGetName(NULL), "ACK received for packet: %d, deleting task", waiting_for_ack->packet_id);
                free(waiting_for_ack->payload);
                free(waiting_for_ack);
                vTaskDelete(NULL);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    ESP_LOGW(pcTaskGetName(NULL), "No ACK received for packet: %d, resending", waiting_for_ack->packet_id);

    xQueueSend(loraSendQueue, &waiting_for_ack, portMAX_DELAY);

    vTaskDelete(NULL);
}

void InitLoraHandlers(void (*function)(DogDogPacket *packet))
{
    handle_dogdog_packet = function;
}