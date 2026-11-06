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

/* Wireless knob: listen for knob frames on UDP (call once the knob Wi-Fi
   network is up). */
esp_err_t rotary_controller_net_start(void);

/* True while a wireless knob is talking to the dash; copies the version the
   knob reports (empty for knobs older than 1.2.0). */
bool rotary_controller_wireless_status(char *version, size_t version_len);

/* Tells the wireless knob to fetch new firmware from the dash on this port. */
bool rotary_controller_send_knob_update(uint16_t port, uint32_t size, const char *version);

#ifdef __cplusplus
}
#endif

#endif
