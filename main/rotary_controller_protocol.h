#ifndef ROTARY_CONTROLLER_PROTOCOL_H
#define ROTARY_CONTROLLER_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define ROTARY_PROTOCOL_MAGIC_0       ((uint8_t)'M')
#define ROTARY_PROTOCOL_MAGIC_1       ((uint8_t)'D')
#define ROTARY_PROTOCOL_VERSION       1
#define ROTARY_PROTOCOL_HEADER_SIZE   7
#define ROTARY_PROTOCOL_CRC_SIZE      2
#define ROTARY_PROTOCOL_MAX_PAYLOAD   64
#define ROTARY_PROTOCOL_MAX_FRAME     (ROTARY_PROTOCOL_HEADER_SIZE + ROTARY_PROTOCOL_MAX_PAYLOAD + ROTARY_PROTOCOL_CRC_SIZE)

#define ROTARY_CAPTURE_CHUNK_HEADER_SIZE 19
#define ROTARY_CAPTURE_CHUNK_DATA_SIZE   (ROTARY_PROTOCOL_MAX_PAYLOAD - ROTARY_CAPTURE_CHUNK_HEADER_SIZE)
#define ROTARY_CAPTURE_FLAG_FIRST        0x01
#define ROTARY_CAPTURE_FLAG_LAST         0x02

typedef enum {
    ROTARY_PROTOCOL_EVENT = 0x01,
    ROTARY_PROTOCOL_GET_STATE = 0x02,
    ROTARY_PROTOCOL_PING = 0x03,
    ROTARY_PROTOCOL_CAPTURE_CONTROL = 0x10,
    ROTARY_PROTOCOL_AIR_RIDE_COMMAND = 0x11,
    ROTARY_PROTOCOL_STATE = 0x81,
    ROTARY_PROTOCOL_PONG = 0x83,
    ROTARY_PROTOCOL_CAPTURE_CHUNK = 0x90,
    ROTARY_PROTOCOL_CAPTURE_STATUS = 0x91,
    ROTARY_PROTOCOL_AIR_RIDE_STATE = 0x92,
} rotary_protocol_type_t;

typedef enum {
    ROTARY_CAPTURE_STOP = 0,
    ROTARY_CAPTURE_START = 1,
} rotary_capture_command_t;

static inline uint16_t rotary_protocol_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;
    for (size_t index = 0; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static inline uint16_t rotary_protocol_get_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline uint32_t rotary_protocol_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static inline uint64_t rotary_protocol_get_u64(const uint8_t *data)
{
    return (uint64_t)rotary_protocol_get_u32(data) |
           ((uint64_t)rotary_protocol_get_u32(data + 4) << 32);
}

static inline void rotary_protocol_put_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static inline void rotary_protocol_put_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static inline void rotary_protocol_put_u64(uint8_t *data, uint64_t value)
{
    rotary_protocol_put_u32(data, (uint32_t)value);
    rotary_protocol_put_u32(data + 4, (uint32_t)(value >> 32));
}

#endif
