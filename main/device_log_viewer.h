#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_LOG_MAX_FILES 100
#define DEVICE_LOG_MAX_POINTS 240
#define DEVICE_LOG_MAX_SERIES 4

typedef enum {
    DEVICE_LOG_PRESET_AFR_TIMING_BOOST = 0,
    DEVICE_LOG_PRESET_BOOST_TIMING,
    DEVICE_LOG_PRESET_TEMPS_RPM_BOOST,
    DEVICE_LOG_PRESET_ENGINE_HEALTH,
    DEVICE_LOG_PRESET_COUNT,
} device_log_preset_t;

typedef struct {
    char filename[20];
    uint32_t size_bytes;
} device_log_file_t;

typedef struct {
    char filename[20];
    uint32_t duration_ms;
    size_t source_rows;
    size_t point_count;
    size_t series_count;
    const char *series_names[DEVICE_LOG_MAX_SERIES];
    const char *series_units[DEVICE_LOG_MAX_SERIES];
    int16_t values[DEVICE_LOG_MAX_SERIES][DEVICE_LOG_MAX_POINTS];
    int32_t minimums[DEVICE_LOG_MAX_SERIES];
    int32_t maximums[DEVICE_LOG_MAX_SERIES];
} device_log_chart_t;

size_t device_log_list(device_log_file_t *files, size_t capacity);
esp_err_t device_log_load_chart(const char *filename, device_log_preset_t preset,
                                device_log_chart_t *chart);

#ifdef __cplusplus
}
#endif