#include "air_ride.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "rotary_controller_uart.h"

static const char *TAG = "air_ride";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static air_ride_state_t s_state;

void air_ride_init(void)
{
    portENTER_CRITICAL(&s_state_lock);
    memset(&s_state, 0, sizeof(s_state));
    portEXIT_CRITICAL(&s_state_lock);
}

void air_ride_get_state(air_ride_state_t *state)
{
    if (!state) return;
    portENTER_CRITICAL(&s_state_lock);
    *state = s_state;
    portEXIT_CRITICAL(&s_state_lock);
}

void air_ride_update_state(const air_ride_state_t *state)
{
    if (!state) return;
    portENTER_CRITICAL(&s_state_lock);
    s_state = *state;
    portEXIT_CRITICAL(&s_state_lock);
}

esp_err_t air_ride_request(air_ride_command_t command)
{
    if (command < AIR_RIDE_FRONT_LEFT_UP || command > AIR_RIDE_AIR_OUT_10S) {
        return ESP_ERR_INVALID_ARG;
    }
    air_ride_state_t state;
    air_ride_get_state(&state);
    if (!state.connected) {
        ESP_LOGW(TAG, "Command %d rejected: Ruin controller is offline", command);
        return ESP_ERR_INVALID_STATE;
    }
    if (command == AIR_RIDE_PRESET_5 || command == AIR_RIDE_PRESET_6) {
        ESP_LOGW(TAG, "Command %d rejected: preset has not been captured", command);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return rotary_controller_air_ride_request((uint8_t)command);
}
