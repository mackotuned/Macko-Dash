// I2C protocol between the S3 dash (master) and the HVAC node (slave).
// Keep this file identical in both projects.
#pragma once
#include <stdint.h>

#define HVAC_I2C_ADDR     0x42
#define HVAC_I2C_HZ       100000

// Packet, master -> node, 5 bytes:
//   [0] HVAC_PKT_HEADER
//   [1] fan   0 = off ... HVAC_FAN_MAX
//   [2] mode  hvac_mode_t
//   [3] temp  0 = full cold ... HVAC_TEMP_MAX = full hot
//   [4] checksum = fan + mode + temp (8-bit)
#define HVAC_PKT_HEADER   0xA5
#define HVAC_PKT_LEN      5

#define HVAC_FAN_MAX      10
#define HVAC_MODE_MAX     4
#define HVAC_TEMP_MAX     14

typedef enum {
    HVAC_MODE_FACE = 0,
    HVAC_MODE_FACE_FEET = 1,
    HVAC_MODE_FEET = 2,
    HVAC_MODE_FEET_DEFROST = 3,
    HVAC_MODE_DEFROST = 4,
} hvac_mode_t;
