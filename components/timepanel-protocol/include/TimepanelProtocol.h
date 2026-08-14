#ifndef __TIMEPANEL_PROTOCOL_H
#define __TIMEPANEL_PROTOCOL_H

#include <stdint.h>

#define TIMEPANEL_MAC_PREFIX_0 0xde
#define TIMEPANEL_MAC_PREFIX_1 0x09
#define TIMEPANEL_MAC_PREFIX_2 0xdd
#define TIMEPANEL_MAC_PREFIX_3 0x09

#define TIMEPANEL_VENDOR_MARKER 0xdd

#define TIMEPANEL_COMMAND_COMPETITOR 0x01
#define TIMEPANEL_COMMAND_RESET 0x02
#define TIMEPANEL_COMMAND_START 0x03
#define TIMEPANEL_COMMAND_STOP 0x04
#define TIMEPANEL_COMMAND_FAULT 0x05
#define TIMEPANEL_COMMAND_REFUSAL 0x06
#define TIMEPANEL_COMMAND_DIS 0x07
#define TIMEPANEL_COMMAND_PARCOURS 0x08

#define TIMEPANEL_CONTROL_MESSAGE_LEN 2
#define TIMEPANEL_FRAME_HEADER_LEN 24
#define TIMEPANEL_NAME_FIELD_LEN 32
#define TIMEPANEL_DOG_FIELD_LEN 32
#define TIMEPANEL_MAX_PAYLOAD_LEN 96
#define TIMEPANEL_MAX_MESSAGE_LEN \
    (TIMEPANEL_CONTROL_MESSAGE_LEN + TIMEPANEL_MAX_PAYLOAD_LEN)

typedef struct __attribute__((packed)) TimepanelCompetitorPayload
{
    char first_name[TIMEPANEL_NAME_FIELD_LEN];
    char last_name[TIMEPANEL_NAME_FIELD_LEN];
    char dog_name[TIMEPANEL_DOG_FIELD_LEN];
} TimepanelCompetitorPayload;

typedef struct __attribute__((packed)) TimepanelU16Payload
{
    uint16_t value;
} TimepanelU16Payload;

typedef struct __attribute__((packed)) TimepanelU32Payload
{
    uint32_t value;
} TimepanelU32Payload;

#endif
