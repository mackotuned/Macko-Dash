#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t fps;
	uint32_t lvgl_stack_margin;
	uint32_t can_stack_margin;
	uint32_t odometer_stack_margin;
} dashboard_runtime_stats_t;

void dashboard_runtime_set_ota_mode(bool enabled);
void dashboard_runtime_set_render_paused(bool paused);
void dashboard_runtime_get_stats(dashboard_runtime_stats_t *stats);

#ifdef __cplusplus
}
#endif