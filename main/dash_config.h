#pragma once

/* Centralized, NVS-persisted dashboard configuration -- units, odometer
   calibration, CAN protocol selection, and redline/VTEC RPM. Backs the
   settings menu's Config page (honda_dash_ui.c). All setters persist
   immediately; call dash_config_init() once at boot before anything
   else reads from this module. */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dash_config_init(void);

/* Legacy aggregate preference retained for compatibility. */
bool dash_config_get_metric(void);
void dash_config_set_metric(bool metric);
bool dash_config_get_speed_kph(void);
void dash_config_set_speed_kph(bool enabled);
bool dash_config_get_temperature_celsius(void);
void dash_config_set_temperature_celsius(bool enabled);
bool dash_config_get_pressure_kpa(void);
void dash_config_set_pressure_kpa(bool enabled);
bool dash_config_get_distance_km(void);
void dash_config_set_distance_km(bool enabled);

/* --- odometer calibration -- sets the odometer to an absolute value,
   e.g. to match a car's factory dash when this unit is first installed --- */
void dash_config_calibrate_odometer_miles(double miles);

/* --- CAN protocol selection --------------------------------------------
   Returns "" to mean "auto-detect" (the dash's existing, already-working
   frame-ID based detection). Returns a specific protocol name (e.g.
   "hondata") to force that protocol at boot, skipping auto-detect.
   Takes effect on next boot -- protocol activation only happens once,
   early in startup, well before the display or CAN tasks are running. */
const char *dash_config_get_can_protocol(void);
void dash_config_set_can_protocol(const char *name);

/* --- redline / VTEC RPM -------------------------------------------------
   Drives the tach's color gradient, shift-light trigger, solid-red
   threshold, and gauge scale max across every theme. */
int  dash_config_get_vtec_rpm(void);
int  dash_config_get_redline_rpm(void);
void dash_config_set_vtec_rpm(int rpm);
void dash_config_set_redline_rpm(int rpm);

int dash_config_get_brightness(void);
void dash_config_set_brightness(int brightness);
bool dash_config_get_show_sim_button(void);
void dash_config_set_show_sim_button(bool show);
bool dash_config_get_value_smoothing(void);
void dash_config_set_value_smoothing(bool enabled);
bool dash_config_get_redline_screen_flash(void);
void dash_config_set_redline_screen_flash(bool enabled);
uint32_t dash_config_get_redline_flash_color(void);
void dash_config_set_redline_flash_color(uint32_t rgb);

#define DASH_CONFIG_HAL_FIELD_COUNT 8
#define DASH_CONFIG_HAL_CHANNEL_COUNT 11
int dash_config_get_hal_field_channel(int slot);
void dash_config_set_hal_field_channel(int slot, int channel);
void dash_config_reset_hal_field_channels(void);
#define DASH_CONFIG_MODERN_FIELD_COUNT 10
#define DASH_CONFIG_RACE_FIELD_COUNT 7
#define DASH_CONFIG_ENDURANCE_FIELD_COUNT 6
#define DASH_CONFIG_TOURING_FIELD_COUNT 4
int dash_config_get_modern_field_channel(int slot);
void dash_config_set_modern_field_channel(int slot, int channel);
void dash_config_reset_modern_field_channels(void);
int dash_config_get_race_field_channel(int slot);
void dash_config_set_race_field_channel(int slot, int channel);
void dash_config_reset_race_field_channels(void);
int dash_config_get_endurance_field_channel(int slot);
void dash_config_set_endurance_field_channel(int slot, int channel);
void dash_config_reset_endurance_field_channels(void);
int dash_config_get_touring_field_channel(int slot);
void dash_config_set_touring_field_channel(int slot, int channel);
void dash_config_reset_touring_field_channels(void);

typedef enum {
   DASH_CONFIG_THRESHOLD_ECT_YELLOW = 0,
   DASH_CONFIG_THRESHOLD_ECT_HIGH,
   DASH_CONFIG_THRESHOLD_IAT_YELLOW,
   DASH_CONFIG_THRESHOLD_IAT_HIGH,
   DASH_CONFIG_THRESHOLD_AFR_RICH_YELLOW,
   DASH_CONFIG_THRESHOLD_AFR_RICH,
   DASH_CONFIG_THRESHOLD_AFR_LEAN_YELLOW,
   DASH_CONFIG_THRESHOLD_AFR_LEAN,
   DASH_CONFIG_THRESHOLD_MAP_YELLOW,
   DASH_CONFIG_THRESHOLD_MAP_HIGH,
   DASH_CONFIG_THRESHOLD_BATT_YELLOW,
   DASH_CONFIG_THRESHOLD_BATT_LOW,
   DASH_CONFIG_THRESHOLD_TPS_YELLOW,
   DASH_CONFIG_THRESHOLD_TPS_HIGH,
   DASH_CONFIG_THRESHOLD_OIL_YELLOW,
   DASH_CONFIG_THRESHOLD_OIL_LOW,
   DASH_CONFIG_THRESHOLD_DUTY_YELLOW,
   DASH_CONFIG_THRESHOLD_DUTY_HIGH,
   DASH_CONFIG_THRESHOLD_KNOCK_AMBER,
   DASH_CONFIG_THRESHOLD_KNOCK_RED,
   DASH_CONFIG_THRESHOLD_COUNT,
} dash_config_threshold_t;

/* Threshold values are stored in tenths of their displayed unit. */
int dash_config_get_threshold_tenths(dash_config_threshold_t threshold);
void dash_config_set_threshold_tenths(dash_config_threshold_t threshold, int value_tenths);

/* --- factory reset: clears every value above back to its default --- */
void dash_config_factory_reset(void);

#ifdef __cplusplus
}
#endif
