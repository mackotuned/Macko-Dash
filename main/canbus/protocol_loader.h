#pragma once

#include <stdint.h>

#define MAX_PROTOCOLS 8
#define MAX_FRAMES    64
#define MAX_SIGNALS   16
#define CAN_ID_MAX    2048

typedef enum {
    ENDIAN_BIG,
    ENDIAN_LITTLE
} endian_t;

typedef struct {
    volatile float *target;
    uint8_t offset;
    uint8_t len;
    float scale;
    float offset_val;
    endian_t endian;
} can_signal_t;

typedef struct {
    uint32_t id;
    int signal_count;
    can_signal_t signals[MAX_SIGNALS];
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