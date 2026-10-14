#ifndef ROTARY_CONTROLLER_UART_H
#define ROTARY_CONTROLLER_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rotary_controller_uart_start(void);
esp_err_t rotary_controller_air_ride_request(uint8_t command);
typedef struct {
	bool connected;
	uint8_t fan;
	uint8_t mode;
	uint8_t temp;
	uint8_t target_fan;
	uint8_t target_mode;
	uint8_t target_temp;
	uint8_t moving_mask;
	uint8_t fault_flags;
} rotary_controller_hvac_state_t;

void rotary_controller_hvac_get_state(rotary_controller_hvac_state_t *state);
esp_err_t rotary_controller_hvac_request(uint8_t fan_level, uint8_t mode_level,
										 uint8_t temp_level);
esp_err_t rotary_controller_capture_start(char *filename, size_t filename_size);
esp_err_t rotary_controller_capture_stop(void);

#ifdef __cplusplus
}
#endif

#endif
