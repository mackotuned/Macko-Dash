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

#ifdef __cplusplus
}
#endif

#endif
