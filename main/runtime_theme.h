#pragma once

#include "esp_err.h"
#include "honda_dash_ui.h"
#include "theme_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t runtime_theme_load(lv_obj_t *parent, const theme_storage_package_t *package,
                             lv_event_cb_t settings_cb, lv_event_cb_t record_cb,
                             lv_event_cb_t sim_cb, lv_event_cb_t sim_long_press_cb);
void runtime_theme_unload(void);
void runtime_theme_update(const honda_dash_data_t *data);
lv_obj_t *runtime_theme_get_root(void);

#ifdef __cplusplus
}
#endif
