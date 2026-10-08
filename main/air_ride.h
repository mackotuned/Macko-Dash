#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AIR_RIDE_FRONT_LEFT_UP = 0,
    AIR_RIDE_FRONT_LEFT_DOWN,
    AIR_RIDE_FRONT_RIGHT_UP,
    AIR_RIDE_FRONT_RIGHT_DOWN,
    AIR_RIDE_REAR_LEFT_UP,
    AIR_RIDE_REAR_LEFT_DOWN,
    AIR_RIDE_REAR_RIGHT_UP,
    AIR_RIDE_REAR_RIGHT_DOWN,
    AIR_RIDE_ALL_UP,
    AIR_RIDE_ALL_DOWN,
    AIR_RIDE_PRESET_1,
    AIR_RIDE_PRESET_2,
    AIR_RIDE_PRESET_3,
    AIR_RIDE_PRESET_4,
    AIR_RIDE_PRESET_5,
    AIR_RIDE_PRESET_6,
    AIR_RIDE_FRONT_UP,
    AIR_RIDE_FRONT_DOWN,
    AIR_RIDE_REAR_UP,
    AIR_RIDE_REAR_DOWN,
    AIR_RIDE_COMPRESSOR_ON,
    AIR_RIDE_COMPRESSOR_OFF,
    AIR_RIDE_AIR_OUT_10S,
} air_ride_command_t;

typedef struct {
    bool connected;
    bool pressure_valid;
    bool compressor_valid;
    bool compressor_on;
    uint16_t tank_psi;
    uint16_t front_left_psi;
    uint16_t front_right_psi;
    uint16_t rear_left_psi;
    uint16_t rear_right_psi;
    bool tank_warning;
    bool front_left_warning;
    bool front_right_warning;
    bool rear_left_warning;
    bool rear_right_warning;
} air_ride_state_t;

void air_ride_init(void);
void air_ride_get_state(air_ride_state_t *state);
void air_ride_update_state(const air_ride_state_t *state);
esp_err_t air_ride_request(air_ride_command_t command);

#ifdef __cplusplus
}
#endif
