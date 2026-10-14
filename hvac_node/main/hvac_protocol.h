#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HVAC_PROTOCOL_MAGIC UINT32_C(0x43415648)
#define HVAC_PROTOCOL_VERSION 1
#define HVAC_ESPNOW_CHANNEL 6

#define HVAC_FAN_MAX 10
#define HVAC_MODE_MAX 4
#define HVAC_TEMP_MAX 14

typedef enum {
    HVAC_MESSAGE_COMMAND = 1,
    HVAC_MESSAGE_STATE = 2,
} hvac_message_type_t;

typedef enum {
    HVAC_MODE_FACE = 0,
    HVAC_MODE_FACE_FEET,
    HVAC_MODE_FEET,
    HVAC_MODE_FEET_DEFROST,
    HVAC_MODE_DEFROST,
} hvac_mode_t;

enum {
    HVAC_MOVING_FAN = 1 << 0,
    HVAC_MOVING_MODE = 1 << 1,
    HVAC_MOVING_TEMP = 1 << 2,
};

enum {
    HVAC_FAULT_LINK_TIMEOUT = 1 << 0,
    HVAC_FAULT_BAD_COMMAND = 1 << 1,
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t sequence;
    uint8_t fan;
    uint8_t mode;
    uint8_t temp;
    uint8_t reserved;
    uint16_t crc;
} hvac_command_packet_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t sequence;
    uint8_t fan;
    uint8_t mode;
    uint8_t temp;
    uint8_t target_fan;
    uint8_t target_mode;
    uint8_t target_temp;
    uint8_t moving_mask;
    uint8_t fault_flags;
    uint16_t crc;
} hvac_state_packet_t;

static inline uint16_t hvac_crc16(const void *buffer, size_t length)
{
    const uint8_t *data = (const uint8_t *)buffer;
    uint16_t crc = 0xffff;
    for (size_t index = 0; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) :
                                   (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static inline bool hvac_command_valid(const hvac_command_packet_t *packet)
{
    return packet && packet->magic == HVAC_PROTOCOL_MAGIC &&
           packet->version == HVAC_PROTOCOL_VERSION &&
           packet->type == HVAC_MESSAGE_COMMAND &&
           packet->fan <= HVAC_FAN_MAX && packet->mode <= HVAC_MODE_MAX &&
           packet->temp <= HVAC_TEMP_MAX &&
           packet->crc == hvac_crc16(packet, offsetof(hvac_command_packet_t, crc));
}

_Static_assert(sizeof(hvac_command_packet_t) == 14, "Unexpected HVAC command size");
_Static_assert(sizeof(hvac_state_packet_t) == 18, "Unexpected HVAC state size");
