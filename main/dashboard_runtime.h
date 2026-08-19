#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void dashboard_runtime_set_ota_mode(bool enabled);
void dashboard_runtime_set_render_paused(bool paused);

#ifdef __cplusplus
}
#endif