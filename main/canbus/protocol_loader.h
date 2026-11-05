#pragma once

#include <stdint.h>

#define MAX_PROTOCOLS 8
#define MAX_FRAMES    64
#define MAX_SIGNALS   48
#define CAN_ID_MAX    2048

typedef enum {
    ENDIAN_BIG,
    ENDIAN_LITTLE
} endian_t;

typedef struct {
    volatile float *target;   /* core dashboard value, or NULL */
    int16_t ecu_channel;      /* extended ECU channel (ecu_channels.h), or -1 */
    uint8_t offset;
    uint8_t len;              /* 1, 2 or 4 bytes */
    uint8_t bit;              /* first (least significant) bit of a bit field */
    uint8_t bits;             /* bit-field width; 0 = use the whole value */
    uint8_t is_signed;
    float scale;
    float offset_val;
    endian_t endian;
} can_signal_t;

typedef struct {
    uint32_t id;
    int signal_count;
    can_signal_t *signals;    /* heap array of signal_count entries */
} can_frame_def_t;

typedef struct {
    char name[32];
    int bitrate;
    int frame_count;
    can_frame_def_t frames[MAX_FRAMES];
} can_protocol_t;

extern can_protocol_t *active_protocol;
extern can_frame_def_t *frame_lookup[CAN_ID_MAX];

void protocol_loader_init(void);
void protocol_detect(uint32_t id);
