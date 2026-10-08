#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void ruin_capture_recorder_init(void);
esp_err_t ruin_capture_recorder_start(char *filename, size_t filename_size);
esp_err_t ruin_capture_recorder_stop(void);
void ruin_capture_recorder_ingest(const uint8_t *payload, uint16_t payload_length);
void ruin_capture_recorder_handle_status(const uint8_t *payload, uint16_t payload_length);
bool ruin_capture_recorder_is_recording(void);

#ifdef __cplusplus
}
#endif
