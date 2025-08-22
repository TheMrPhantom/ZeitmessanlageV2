#ifndef __LORA_NETWORK_H
#define __LORA_NETWORK_H

#include "freertos/FreeRTOS.h"
#include <sys/time.h>

#define CONTROLLER_ID 0x01
#define START_ID 0x02
#define STOP_ID 0x03

// Packet Header
#define LORA_MAGIC 0xDD09
#define LORA_PROTOCOL_VERSION 0x01
#define LORA_STATION_ID 0x02

// Packet Types
#define LORA_TIME_SYNC 0x01
#define LORA_TRIGGER 0x02
#define LORA_START_FAKE_TIME 0x03
#define LORA_FINAL_TIME 0x04
#define LORA_SENSOR_STATE 0x05
#define LORA_ACK 0x06

typedef struct DogDogPacket
{
    uint32_t magic;           // Magic number to identify packet type
    uint8_t protocol_version; // Protocol version
    uint8_t station_id;       // Station ID
    uint8_t packet_id;        // Packet ID
    uint8_t type;             // Packet type
    int64_t local_time_received;
    int8_t rssi;
    int8_t snr;
    uint16_t payload_length;  // Length of the payload
    uint8_t *payload; // Variable length payload
} DogDogPacket;

typedef struct PacketTypeTimeSync
{
    int64_t timestamp;
} PacketTypeTimeSync;

typedef struct PacketTypeTrigger
{
    int64_t timestamp;
} PacketTypeTrigger;

typedef struct PacketTypeFinalTime
{
    int64_t timestamp;
} PacketTypeFinalTime;

typedef struct PacketTypeSensorState
{
    uint8_t num_sensors;
    uint64_t sensor_states; // Bitfield of sensor states, 0 for inactive, 1 for active
} PacketTypeSensorState;

typedef struct PacketTypeAck
{
    uint8_t station_id;
    uint8_t packet_id;
} PacketTypeAck;

typedef struct timeval timeval_t;

#define TIME_US(t) ((int64_t)t.tv_sec * 1000000L + (int64_t)t.tv_usec)

DogDogPacket *create_dogdog_packet_from_bytes(uint8_t *data, uint16_t length);
int create_bytes_from_dogdog_packet(DogDogPacket *packet, uint8_t *buf, size_t buf_len);

bool is_packet_from_dogdog(uint8_t *data);

PacketTypeTimeSync *create_time_sync_information(DogDogPacket *packet);
PacketTypeTrigger *create_trigger_information(DogDogPacket *packet);
PacketTypeFinalTime *create_final_time_information(DogDogPacket *packet);
PacketTypeSensorState *create_sensor_state_information(DogDogPacket *packet);
PacketTypeAck *create_ack_information(DogDogPacket *packet);

DogDogPacket *create_dogdog_packet_from_time_sync_information(PacketTypeTimeSync *time_sync);
DogDogPacket *create_dogdog_packet_from_trigger_information(PacketTypeTrigger *trigger);
DogDogPacket *create_dogdog_packet_from_final_time_information(PacketTypeFinalTime *final_time);
DogDogPacket *create_dogdog_packet_from_sensor_state_information(PacketTypeSensorState *sensor_state);
DogDogPacket *create_dogdog_packet_from_ack_information(PacketTypeAck *ack);

void log_dogdog_packet(DogDogPacket *packet);

static void IRAM_ATTR lora_module_rx_isr(void *arg);

BaseType_t send_dogdog_packet(DogDogPacket *packet);
void init_lora(void);
void LoraReceiveTask(void *pvParameters);
void LoraSendTask(void *pvParameters);
void LoraSyncTask(void *pvParameters);
void HandleReceivedPacket(DogDogPacket *packet);

#endif // __LORA_NETWORK_H