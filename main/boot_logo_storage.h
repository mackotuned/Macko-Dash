#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_LOGO_STORAGE_MAX_LOGOS 12
#define BOOT_LOGO_STORAGE_NAME_MAX  60

typedef struct {
    char filename[96];
    char display_name[BOOT_LOGO_STORAGE_NAME_MAX];
} boot_logo_storage_entry_t;

void boot_logo_storage_init(void);
size_t boot_logo_storage_get_count(void);
const boot_logo_storage_entry_t *boot_logo_storage_get_entry(size_t index);
int boot_logo_storage_get_selected_index(void);
esp_err_t boot_logo_storage_select(int index);
esp_err_t boot_logo_storage_delete(size_t index);
esp_err_t boot_logo_storage_load_selected(lv_img_dsc_t *image, uint8_t **pixels);

#ifdef __cplusplus
}
#endif