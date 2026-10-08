#ifndef ROTARY_CONTROLLER_UART_H
#define ROTARY_CONTROLLER_UART_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t rotary_controller_uart_start(void);
esp_err_t rotary_controller_air_ride_request(uint8_t command);
esp_err_t rotary_controller_capture_start(char *filename, size_t filename_size);
esp_err_t rotary_controller_capture_stop(void);

#ifdef __cplusplus
}
#endif

#endif
