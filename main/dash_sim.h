#pragma once

/* Built-in dashboard data simulator -- when enabled, produces smoothly
   varying, randomized values across realistic ranges for every gauge
   channel, standing in for real CAN data. Useful for eyeballing
   FPS/CPU under continuously-changing values without needing the car
   running. Toggled from the settings bar's simulation button
   (honda_dash_ui.c); consumed from main.c's gauge timer. */

#include <stdbool.h>
#include "honda_dash_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
   DASH_SIM_MODE_IDLE = 0,
   DASH_SIM_MODE_CRUISE,
   DASH_SIM_MODE_FULL_THROTTLE,
   DASH_SIM_MODE_REDLINE,
   DASH_SIM_MODE_COUNT,
} dash_sim_mode_t;

void dash_sim_set_enabled(bool enabled);
bool dash_sim_is_enabled(void);
void dash_sim_set_mode(dash_sim_mode_t mode);
dash_sim_mode_t dash_sim_get_mode(void);

/* Advances the simulation by one tick and fills `out` with the new
   values. Only meaningful while dash_sim_is_enabled() is true. */
void dash_sim_step(honda_dash_data_t *out);

#ifdef __cplusplus
}
#endif
