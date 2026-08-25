#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "honda_dash_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void data_logger_init(void);
esp_err_t data_logger_start(char *filename, size_t filename_size);
esp_err_t data_logger_stop(char *filename, size_t filename_size);
void data_logger_submit(const honda_dash_data_t *data);
bool data_logger_is_recording(void);
bool data_logger_auto_update(const honda_dash_data_t *data, bool can_live);
void data_logger_note_manual_control(void);

#ifdef __cplusplus
}
#endif