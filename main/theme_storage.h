#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THEME_STORAGE_MAX_PACKAGES 30
#define THEME_STORAGE_NAME_MAX     96
#define THEME_STORAGE_PATH_MAX     192
#define THEME_STORAGE_ID_MAX       64
#define THEME_STORAGE_STATUS_MAX   64

typedef struct {
    char filename[THEME_STORAGE_NAME_MAX];
    char path[THEME_STORAGE_PATH_MAX];
    char id[THEME_STORAGE_ID_MAX];
    char display_name[THEME_STORAGE_NAME_MAX];
    char status[THEME_STORAGE_STATUS_MAX];
    char layout_path[THEME_STORAGE_PATH_MAX];
    uint16_t design_width;
    uint16_t design_height;
    uint16_t schema;
    bool manifest_valid;
} theme_storage_package_t;

esp_err_t theme_storage_init(void);
bool theme_storage_is_available(void);
size_t theme_storage_get_count(void);
const theme_storage_package_t *theme_storage_get_package(size_t index);
esp_err_t theme_storage_delete(size_t index);
esp_err_t theme_storage_read_file(const theme_storage_package_t *package, const char *entry_path,
                                  size_t max_size, uint8_t **data, size_t *data_size);

#ifdef __cplusplus
}
#endif