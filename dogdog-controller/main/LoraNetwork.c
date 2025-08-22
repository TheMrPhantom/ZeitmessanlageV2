#include "LoraNetwork.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "ra01s.h" // For LoRaSend/LoRaReceive
#include "driver/gpio.h"
#include "GPIOPins.h"
#include "Timer.h"
#include "NetworkFault.h"
#include "SevenSegment.h"
#include "esp_timer.h"

static const char *TAG_LORA = "LoraNetwork";

extern QueueHandle_t loraSendQueue;
QueueHandle_t localReceiveTimestampQueue;
extern QueueHandle_t triggerQueue;
extern QueueHandle_t networkFaultQueue;
extern QueueHandle_t sevenSegmentQueue;

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
    // four bytes of the packet payload are int64 time stamp
    packet_type->timestamp = *((int64_t *)packet->payload);
    return packet_type;
}

PacketTypeTrigger *create_trigger_information(DogDogPacket *packet)
{
    PacketTypeTrigger *packet_type = calloc(1, sizeof(PacketTypeTrigger));
    // four bytes of the packet payload are int64 time stamp
    packet_type->timestamp = *((int64_t *)packet->payload);
    memcpy(&packet_type->sensor_state, packet->payload + sizeof(int64_t), sizeof(PacketTypeSensorState));
    return packet_type;
}

PacketTypeFinalTime *create_final_time_information(DogDogPacket *packet)
{
    PacketTypeFinalTime *packet_type = calloc(1, sizeof(PacketTypeFinalTime));
    // four bytes of the packet payload are int64 time stamp
    packet_type->timestamp = ((int64_t *)packet->payload);
    return packet_type;
}

PacketTypeSensorState *create_sensor_state_information(DogDogPacket *packet)
{
    PacketTypeSensorState *packet_type = calloc(1, sizeof(PacketTypeSensorState));
    // first byte of the packet payload is the number of sensors
    packet_type->num_sensors = packet->payload[0];
    // remaining bytes are the sensor states
    for (int i = 0; i < packet_type->num_sensors; i++)
    {
        packet_type->sensor_states |= ((uint64_t)packet->payload[i + 1] << i);
    }
    return packet_type;
}

PacketTypeAck *create_ack_information(DogDogPacket *packet)
{

    PacketTypeAck *packet_type = calloc(1, sizeof(PacketTypeAck));
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

void confirm_station_alive(DogDogPacket *packet)
{
    int is_start = packet->station_id == START_ID ? START_ALIVE : STOP_ALIVE;

    StationConnectivityStatus status;
    status.station = is_start;
    status.signal = 0;
    if (packet->rssi < -100)
    {
        status.signal = 1;
    }
    if (packet->snr < -10)
    {
        status.signal = -9;
    }
    xQueueSend(networkFaultQueue, &status, portMAX_DELAY);
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
    return xQueueSend(loraSendQueue, &packet, 0);
}

void init_lora(void)
{
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
    // You can add your interrupt handling code here
    uint32_t gpio_num = (uint32_t)arg;
    // Use ISR-safe timer function
    int64_t local_time_received = esp_timer_get_time();
    BaseType_t sent = xQueueSendFromISR(localReceiveTimestampQueue, &local_time_received, NULL);
    if (sent != pdTRUE)
    {
        ESP_LOGW(pcTaskGetName(NULL), "Warning: localReceiveTimestampQueue full, timestamp lost");
    }
}

void LoraReceiveTask(void *pvParameters)
{
    gpio_reset_pin(LORA_GPIO_DIO1);
    gpio_set_direction(LORA_GPIO_DIO1, GPIO_MODE_INPUT);
    gpio_set_intr_type(LORA_GPIO_DIO1, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(LORA_GPIO_DIO1, lora_module_rx_isr, (void *)LORA_GPIO_DIO1);

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

                HandleReceivedPacket(packet);

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
            packet->packet_id = packet_id++;

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
            free(packet->payload);
            free(packet);

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

            // Do not wait for the transmission to be completed

            int lost = GetPacketLost();
            if (lost != 0)
            {
                ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
            }
        }
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

        xQueueSend(loraSendQueue, &packet, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

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
        ESP_LOGW(TAG_LORA, "Unknown packet type: %d", packet->type);
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