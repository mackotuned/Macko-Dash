#include "dash_config.h"
#include "nvs_flash.h"
#include "odometer/odometer.h"
#include "esp_err.h"
#include <string.h>

#define DASH_CONFIG_NVS_NAMESPACE "honda_dash"
#define KEY_METRIC       "cfg_metric"
#define KEY_SPEED_KPH    "cfg_spd_kph"
#define KEY_TEMP_C       "cfg_temp_c"
#define KEY_PRESSURE_KPA "cfg_press_kpa"
#define KEY_DISTANCE_KM  "cfg_dist_km"
#define KEY_CAN_PROTOCOL "cfg_can_proto"
#define KEY_VTEC_RPM     "cfg_vtec_rpm"
#define KEY_REDLINE_RPM  "cfg_redline"
#define KEY_CHIME_MASK   "cfg_chime_m"
#define KEY_CHIME_VOLUME "cfg_chime_v"
#define KEY_BRIGHTNESS   "cfg_bright"
#define KEY_SHOW_SIM     "cfg_show_sim"
#define KEY_VALUE_SMOOTH "cfg_smooth"
#define KEY_REDLINE_FLASH "cfg_rl_flash"
#define KEY_REDLINE_FLASH_COLOR "cfg_rl_color"

#define DEFAULT_VTEC_RPM    5600
#define DEFAULT_REDLINE_RPM 8400
#define VTEC_RPM_MIN        3000
#define VTEC_RPM_MAX        7500
#define REDLINE_RPM_MIN     6000
#define REDLINE_RPM_MAX     11000
#define CHIME_VOLUME_MIN    0
#define CHIME_VOLUME_MAX    100
#define DEFAULT_CHIME_VOLUME 60
#define DEFAULT_BRIGHTNESS 95
#define DEFAULT_REDLINE_FLASH_COLOR 0xe4002b

static const char *const THRESHOLD_KEYS[DASH_CONFIG_THRESHOLD_COUNT] = {
    "thr_ect_y", "thr_ect", "thr_iat_y", "thr_iat", "thr_afrr_y", "thr_afrr",
    "thr_afrl_y", "thr_afrl", "thr_map_y", "thr_map", "thr_batt_y", "thr_batt",
    "thr_tps_y", "thr_tps", "thr_oil_y", "thr_oil", "thr_duty_y", "thr_duty",
    "thr_knka", "thr_knkr",
};
static const int THRESHOLD_DEFAULTS[DASH_CONFIG_THRESHOLD_COUNT] = {
    1950, 2050, 1000, 1100, 108, 105, 112, 115, 120, 160,
    130, 120, 700, 800, 200, 100, 650, 750, 20, 40,
};
static const int THRESHOLD_MINS[DASH_CONFIG_THRESHOLD_COUNT] = {
    1600, 1600, 600, 600, 80, 80, 100, 100, 0, 0,
    80, 80, 500, 500, 0, 0, 400, 400, 0, 10,
};
static const int THRESHOLD_MAXS[DASH_CONFIG_THRESHOLD_COUNT] = {
    2600, 2600, 2000, 2000, 140, 140, 200, 200, 400, 400,
    150, 150, 1000, 1000, 500, 500, 1000, 1000, 80, 100,
};
static const int THRESHOLD_STEPS[DASH_CONFIG_THRESHOLD_COUNT] = {
    10, 10, 10, 10, 1, 1, 1, 1, 5, 5,
    1, 1, 10, 10, 10, 10, 10, 10, 1, 1,
};
static const char *const HAL_FIELD_KEYS[DASH_CONFIG_HAL_FIELD_COUNT] = {
    "cfg_hal_0", "cfg_hal_1", "cfg_hal_2", "cfg_hal_3",
    "cfg_hal_4", "cfg_hal_5", "cfg_hal_6", "cfg_hal_7",
};
static const int HAL_FIELD_DEFAULTS[DASH_CONFIG_HAL_FIELD_COUNT] = {4, 2, 0, 1, 7, 10, 5, 6};
static const char *const MODERN_FIELD_KEYS[DASH_CONFIG_MODERN_FIELD_COUNT] = {
    "cfg_mod_0", "cfg_mod_1", "cfg_mod_2", "cfg_mod_3", "cfg_mod_4",
    "cfg_mod_5", "cfg_mod_6", "cfg_mod_7", "cfg_mod_8", "cfg_mod_9",
};
static const char *const RACE_FIELD_KEYS[DASH_CONFIG_RACE_FIELD_COUNT] = {
    "cfg_race_0", "cfg_race_1", "cfg_race_2", "cfg_race_3",
    "cfg_race_4", "cfg_race_5", "cfg_race_6",
};
static const char *const ENDURANCE_FIELD_KEYS[DASH_CONFIG_ENDURANCE_FIELD_COUNT] = {
    "cfg_end_0", "cfg_end_1", "cfg_end_2", "cfg_end_3", "cfg_end_4", "cfg_end_5",
};
static const char *const TOURING_FIELD_KEYS[DASH_CONFIG_TOURING_FIELD_COUNT] = {
    "cfg_tour_0", "cfg_tour_1", "cfg_tour_2", "cfg_tour_3",
};
static const int MODERN_FIELD_DEFAULTS[DASH_CONFIG_MODERN_FIELD_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
static const int RACE_FIELD_DEFAULTS[DASH_CONFIG_RACE_FIELD_COUNT] = {0, 1, 2, 4, 10, 5, 6};
static const int ENDURANCE_FIELD_DEFAULTS[DASH_CONFIG_ENDURANCE_FIELD_COUNT] = {0, 7, 2, 4, 10, 5};
static const int TOURING_FIELD_DEFAULTS[DASH_CONFIG_TOURING_FIELD_COUNT] = {0, 10, 5, 7};

static bool s_metric = false;
static bool s_speed_kph = false;
static bool s_temperature_celsius = false;
static bool s_pressure_kpa = false;
static bool s_distance_km = false;
static char s_can_protocol[32] = "hondata";
static int  s_vtec_rpm = DEFAULT_VTEC_RPM;
static int  s_redline_rpm = DEFAULT_REDLINE_RPM;
static uint32_t s_chime_warning_mask = DASH_CONFIG_CHIME_WARNING_ALL;
static int s_chime_volume = DEFAULT_CHIME_VOLUME;
static int s_brightness = DEFAULT_BRIGHTNESS;
static bool s_show_sim_button = true;
static bool s_value_smoothing = false;
static bool s_redline_screen_flash = true;
static uint32_t s_redline_flash_color = DEFAULT_REDLINE_FLASH_COLOR;
static int s_hal_field_channels[DASH_CONFIG_HAL_FIELD_COUNT] = {4, 2, 0, 1, 7, 10, 5, 6};
static int s_modern_field_channels[DASH_CONFIG_MODERN_FIELD_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
static int s_race_field_channels[DASH_CONFIG_RACE_FIELD_COUNT] = {0, 1, 2, 4, 10, 5, 6};
static int s_endurance_field_channels[DASH_CONFIG_ENDURANCE_FIELD_COUNT] = {0, 7, 2, 4, 10, 5};
static int s_touring_field_channels[DASH_CONFIG_TOURING_FIELD_COUNT] = {0, 10, 5, 7};
static int s_thresholds[DASH_CONFIG_THRESHOLD_COUNT] = {
    1950, 2050, 1000, 1100, 108, 105, 112, 115, 120, 160,
    130, 120, 700, 800, 200, 100, 650, 750, 20, 40,
};

void dash_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) {
            return;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return;
    }

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;
    }

    int32_t val = 0;
    if (nvs_get_i32(h, KEY_METRIC, &val) == ESP_OK) {
        s_metric = (val != 0);
        s_speed_kph = s_metric;
        s_temperature_celsius = s_metric;
        s_distance_km = s_metric;
    }
    if (nvs_get_i32(h, KEY_SPEED_KPH, &val) == ESP_OK) s_speed_kph = (val != 0);
    if (nvs_get_i32(h, KEY_TEMP_C, &val) == ESP_OK) s_temperature_celsius = (val != 0);
    if (nvs_get_i32(h, KEY_PRESSURE_KPA, &val) == ESP_OK) s_pressure_kpa = (val != 0);
    if (nvs_get_i32(h, KEY_DISTANCE_KM, &val) == ESP_OK) s_distance_km = (val != 0);
    if (nvs_get_i32(h, KEY_VTEC_RPM, &val) == ESP_OK && val >= VTEC_RPM_MIN && val <= VTEC_RPM_MAX) {
        s_vtec_rpm = (int)val;
    }
    if (nvs_get_i32(h, KEY_REDLINE_RPM, &val) == ESP_OK && val >= REDLINE_RPM_MIN && val <= REDLINE_RPM_MAX) {
        s_redline_rpm = (int)val;
    }
    if (nvs_get_i32(h, KEY_CHIME_MASK, &val) == ESP_OK) {
        s_chime_warning_mask = ((uint32_t)val) & DASH_CONFIG_CHIME_WARNING_ALL;
    }
    if (nvs_get_i32(h, KEY_CHIME_VOLUME, &val) == ESP_OK && val >= CHIME_VOLUME_MIN && val <= CHIME_VOLUME_MAX) {
        s_chime_volume = (int)val;
    }
    if (nvs_get_i32(h, KEY_BRIGHTNESS, &val) == ESP_OK && val >= 20 && val <= 100) {
        s_brightness = (int)val;
    }
    if (nvs_get_i32(h, KEY_SHOW_SIM, &val) == ESP_OK) {
        s_show_sim_button = (val != 0);
    }
    if (nvs_get_i32(h, KEY_VALUE_SMOOTH, &val) == ESP_OK) {
        s_value_smoothing = (val != 0);
    }
    if (nvs_get_i32(h, KEY_REDLINE_FLASH, &val) == ESP_OK) {
        s_redline_screen_flash = (val != 0);
    }
    if (nvs_get_i32(h, KEY_REDLINE_FLASH_COLOR, &val) == ESP_OK && val >= 0 && val <= 0xffffff) {
        s_redline_flash_color = (uint32_t)val;
    }
    for (int i = 0; i < DASH_CONFIG_THRESHOLD_COUNT; ++i) {
        if (nvs_get_i32(h, THRESHOLD_KEYS[i], &val) == ESP_OK &&
                val >= THRESHOLD_MINS[i] && val <= THRESHOLD_MAXS[i]) {
            s_thresholds[i] = (int)val;
        }
    }
    for (int i = 0; i < DASH_CONFIG_HAL_FIELD_COUNT; ++i) {
        if (nvs_get_i32(h, HAL_FIELD_KEYS[i], &val) == ESP_OK &&
                val >= 0 && val < DASH_CONFIG_HAL_CHANNEL_COUNT) {
            s_hal_field_channels[i] = (int)val;
        }
    }
    for (int i = 0; i < DASH_CONFIG_MODERN_FIELD_COUNT; ++i) {
        if (nvs_get_i32(h, MODERN_FIELD_KEYS[i], &val) == ESP_OK &&
                val >= 0 && val < DASH_CONFIG_HAL_CHANNEL_COUNT) {
            s_modern_field_channels[i] = (int)val;
        }
    }
    for (int i = 0; i < DASH_CONFIG_RACE_FIELD_COUNT; ++i) {
        if (nvs_get_i32(h, RACE_FIELD_KEYS[i], &val) == ESP_OK &&
                val >= 0 && val < DASH_CONFIG_HAL_CHANNEL_COUNT) {
            s_race_field_channels[i] = (int)val;
        }
    }
    for (int i = 0; i < DASH_CONFIG_ENDURANCE_FIELD_COUNT; ++i) {
        if (nvs_get_i32(h, ENDURANCE_FIELD_KEYS[i], &val) == ESP_OK &&
                val >= 0 && val < DASH_CONFIG_HAL_CHANNEL_COUNT) {
            s_endurance_field_channels[i] = (int)val;
        }
    }
    for (int i = 0; i < DASH_CONFIG_TOURING_FIELD_COUNT; ++i) {
        if (nvs_get_i32(h, TOURING_FIELD_KEYS[i], &val) == ESP_OK &&
                val >= 0 && val < DASH_CONFIG_HAL_CHANNEL_COUNT) {
            s_touring_field_channels[i] = (int)val;
        }
    }

    char buf[sizeof(s_can_protocol)];
    size_t len = sizeof(buf);
    if (nvs_get_str(h, KEY_CAN_PROTOCOL, buf, &len) == ESP_OK) {
        strncpy(s_can_protocol, buf, sizeof(s_can_protocol) - 1);
        s_can_protocol[sizeof(s_can_protocol) - 1] = '\0';
    }

    nvs_close(h);
}

bool dash_config_get_metric(void)
{
    return s_metric;
}

void dash_config_set_metric(bool metric)
{
    s_metric = metric;
    s_speed_kph = metric;
    s_temperature_celsius = metric;
    s_pressure_kpa = metric;
    s_distance_km = metric;
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, KEY_METRIC, metric ? 1 : 0);
    nvs_set_i32(h, KEY_SPEED_KPH, metric ? 1 : 0);
    nvs_set_i32(h, KEY_TEMP_C, metric ? 1 : 0);
    nvs_set_i32(h, KEY_PRESSURE_KPA, metric ? 1 : 0);
    nvs_set_i32(h, KEY_DISTANCE_KM, metric ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

#define DEFINE_UNIT_SETTING(getter, setter, state, key) \
    bool getter(void) { return state; } \
    void setter(bool enabled) { \
        state = enabled; \
        nvs_handle_t h; \
        if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return; \
        nvs_set_i32(h, key, enabled ? 1 : 0); \
        nvs_commit(h); \
        nvs_close(h); \
    }

DEFINE_UNIT_SETTING(dash_config_get_speed_kph, dash_config_set_speed_kph, s_speed_kph, KEY_SPEED_KPH)
DEFINE_UNIT_SETTING(dash_config_get_temperature_celsius, dash_config_set_temperature_celsius,
                    s_temperature_celsius, KEY_TEMP_C)
DEFINE_UNIT_SETTING(dash_config_get_pressure_kpa, dash_config_set_pressure_kpa, s_pressure_kpa,
                    KEY_PRESSURE_KPA)
DEFINE_UNIT_SETTING(dash_config_get_distance_km, dash_config_set_distance_km, s_distance_km,
                    KEY_DISTANCE_KM)

void dash_config_calibrate_odometer_miles(double miles)
{
    odometer_set_miles(miles);
}

const char *dash_config_get_can_protocol(void)
{
    return s_can_protocol;
}

void dash_config_set_can_protocol(const char *name)
{
    if (!name) {
        name = "";
    }
    strncpy(s_can_protocol, name, sizeof(s_can_protocol) - 1);
    s_can_protocol[sizeof(s_can_protocol) - 1] = '\0';

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, KEY_CAN_PROTOCOL, s_can_protocol);
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_vtec_rpm(void)
{
    return s_vtec_rpm;
}

int dash_config_get_redline_rpm(void)
{
    return s_redline_rpm;
}

void dash_config_set_vtec_rpm(int rpm)
{
    if (rpm < VTEC_RPM_MIN) rpm = VTEC_RPM_MIN;
    if (rpm > VTEC_RPM_MAX) rpm = VTEC_RPM_MAX;
    if (rpm >= s_redline_rpm) rpm = s_redline_rpm - 600;
    s_vtec_rpm = rpm;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, KEY_VTEC_RPM, (int32_t)s_vtec_rpm);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_set_redline_rpm(int rpm)
{
    if (rpm < REDLINE_RPM_MIN) rpm = REDLINE_RPM_MIN;
    if (rpm > REDLINE_RPM_MAX) rpm = REDLINE_RPM_MAX;
    if (rpm <= s_vtec_rpm) rpm = s_vtec_rpm + 600;
    s_redline_rpm = rpm;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, KEY_REDLINE_RPM, (int32_t)s_redline_rpm);
    nvs_commit(h);
    nvs_close(h);
}

uint32_t dash_config_get_chime_warning_mask(void)
{
    return s_chime_warning_mask;
}

bool dash_config_get_chime_warning_enabled(uint32_t mask_bit)
{
    return (s_chime_warning_mask & mask_bit) != 0;
}

void dash_config_set_chime_warning_mask(uint32_t mask)
{
    s_chime_warning_mask = mask & DASH_CONFIG_CHIME_WARNING_ALL;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, KEY_CHIME_MASK, (int32_t)s_chime_warning_mask);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_set_chime_warning_enabled(uint32_t mask_bit, bool enabled)
{
    uint32_t next_mask = s_chime_warning_mask;
    if (enabled) {
        next_mask |= mask_bit;
    } else {
        next_mask &= ~mask_bit;
    }
    dash_config_set_chime_warning_mask(next_mask);
}

int dash_config_get_chime_volume(void)
{
    return s_chime_volume;
}

void dash_config_set_chime_volume(int volume)
{
    if (volume < CHIME_VOLUME_MIN) volume = CHIME_VOLUME_MIN;
    if (volume > CHIME_VOLUME_MAX) volume = CHIME_VOLUME_MAX;
    s_chime_volume = volume;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, KEY_CHIME_VOLUME, (int32_t)s_chime_volume);
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_brightness(void)
{
    return s_brightness;
}

void dash_config_set_brightness(int brightness)
{
    if (brightness < 20) brightness = 20;
    if (brightness > 100) brightness = 100;
    s_brightness = brightness;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, KEY_BRIGHTNESS, (int32_t)s_brightness);
    nvs_commit(h);
    nvs_close(h);
}

bool dash_config_get_show_sim_button(void)
{
    return s_show_sim_button;
}

void dash_config_set_show_sim_button(bool show)
{
    s_show_sim_button = show;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, KEY_SHOW_SIM, show ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool dash_config_get_value_smoothing(void)
{
    return s_value_smoothing;
}

void dash_config_set_value_smoothing(bool enabled)
{
    s_value_smoothing = enabled;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, KEY_VALUE_SMOOTH, enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

bool dash_config_get_redline_screen_flash(void)
{
    return s_redline_screen_flash;
}

void dash_config_set_redline_screen_flash(bool enabled)
{
    s_redline_screen_flash = enabled;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, KEY_REDLINE_FLASH, enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

uint32_t dash_config_get_redline_flash_color(void)
{
    return s_redline_flash_color;
}

void dash_config_set_redline_flash_color(uint32_t rgb)
{
    if (rgb > 0xffffff) rgb = DEFAULT_REDLINE_FLASH_COLOR;
    s_redline_flash_color = rgb;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, KEY_REDLINE_FLASH_COLOR, (int32_t)rgb);
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_hal_field_channel(int slot)
{
    if (slot < 0 || slot >= DASH_CONFIG_HAL_FIELD_COUNT) return 0;
    return s_hal_field_channels[slot];
}

void dash_config_set_hal_field_channel(int slot, int channel)
{
    if (slot < 0 || slot >= DASH_CONFIG_HAL_FIELD_COUNT ||
            channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) return;
    s_hal_field_channels[slot] = channel;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, HAL_FIELD_KEYS[slot], (int32_t)channel);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_reset_hal_field_channels(void)
{
    memcpy(s_hal_field_channels, HAL_FIELD_DEFAULTS, sizeof(s_hal_field_channels));
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    for (int slot = 0; slot < DASH_CONFIG_HAL_FIELD_COUNT; ++slot) {
        nvs_set_i32(h, HAL_FIELD_KEYS[slot], (int32_t)s_hal_field_channels[slot]);
    }
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_modern_field_channel(int slot)
{
    if (slot < 0 || slot >= DASH_CONFIG_MODERN_FIELD_COUNT) return 0;
    return s_modern_field_channels[slot];
}

void dash_config_set_modern_field_channel(int slot, int channel)
{
    if (slot < 0 || slot >= DASH_CONFIG_MODERN_FIELD_COUNT ||
            channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) return;
    s_modern_field_channels[slot] = channel;
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, MODERN_FIELD_KEYS[slot], (int32_t)channel);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_reset_modern_field_channels(void)
{
    memcpy(s_modern_field_channels, MODERN_FIELD_DEFAULTS, sizeof(s_modern_field_channels));
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    for (int slot = 0; slot < DASH_CONFIG_MODERN_FIELD_COUNT; ++slot) {
        nvs_set_i32(h, MODERN_FIELD_KEYS[slot], (int32_t)s_modern_field_channels[slot]);
    }
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_race_field_channel(int slot)
{
    if (slot < 0 || slot >= DASH_CONFIG_RACE_FIELD_COUNT) return 0;
    return s_race_field_channels[slot];
}

void dash_config_set_race_field_channel(int slot, int channel)
{
    if (slot < 0 || slot >= DASH_CONFIG_RACE_FIELD_COUNT ||
            channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) return;
    s_race_field_channels[slot] = channel;
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, RACE_FIELD_KEYS[slot], (int32_t)channel);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_reset_race_field_channels(void)
{
    memcpy(s_race_field_channels, RACE_FIELD_DEFAULTS, sizeof(s_race_field_channels));
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    for (int slot = 0; slot < DASH_CONFIG_RACE_FIELD_COUNT; ++slot) {
        nvs_set_i32(h, RACE_FIELD_KEYS[slot], (int32_t)s_race_field_channels[slot]);
    }
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_endurance_field_channel(int slot)
{
    if (slot < 0 || slot >= DASH_CONFIG_ENDURANCE_FIELD_COUNT) return 0;
    return s_endurance_field_channels[slot];
}

void dash_config_set_endurance_field_channel(int slot, int channel)
{
    if (slot < 0 || slot >= DASH_CONFIG_ENDURANCE_FIELD_COUNT ||
            channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) return;
    s_endurance_field_channels[slot] = channel;
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, ENDURANCE_FIELD_KEYS[slot], (int32_t)channel);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_reset_endurance_field_channels(void)
{
    memcpy(s_endurance_field_channels, ENDURANCE_FIELD_DEFAULTS, sizeof(s_endurance_field_channels));
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    for (int slot = 0; slot < DASH_CONFIG_ENDURANCE_FIELD_COUNT; ++slot) {
        nvs_set_i32(h, ENDURANCE_FIELD_KEYS[slot], (int32_t)s_endurance_field_channels[slot]);
    }
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_touring_field_channel(int slot)
{
    if (slot < 0 || slot >= DASH_CONFIG_TOURING_FIELD_COUNT) return 0;
    return s_touring_field_channels[slot];
}

void dash_config_set_touring_field_channel(int slot, int channel)
{
    if (slot < 0 || slot >= DASH_CONFIG_TOURING_FIELD_COUNT ||
            channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) return;
    s_touring_field_channels[slot] = channel;
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, TOURING_FIELD_KEYS[slot], (int32_t)channel);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_reset_touring_field_channels(void)
{
    memcpy(s_touring_field_channels, TOURING_FIELD_DEFAULTS, sizeof(s_touring_field_channels));
    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    for (int slot = 0; slot < DASH_CONFIG_TOURING_FIELD_COUNT; ++slot) {
        nvs_set_i32(h, TOURING_FIELD_KEYS[slot], (int32_t)s_touring_field_channels[slot]);
    }
    nvs_commit(h);
    nvs_close(h);
}

int dash_config_get_threshold_tenths(dash_config_threshold_t threshold)
{
    if (threshold < 0 || threshold >= DASH_CONFIG_THRESHOLD_COUNT) return 0;
    return s_thresholds[threshold];
}

void dash_config_set_threshold_tenths(dash_config_threshold_t threshold, int value_tenths)
{
    if (threshold < 0 || threshold >= DASH_CONFIG_THRESHOLD_COUNT) return;
    if (value_tenths < THRESHOLD_MINS[threshold]) value_tenths = THRESHOLD_MINS[threshold];
    if (value_tenths > THRESHOLD_MAXS[threshold]) value_tenths = THRESHOLD_MAXS[threshold];
    switch (threshold) {
        case DASH_CONFIG_THRESHOLD_ECT_YELLOW:
        case DASH_CONFIG_THRESHOLD_IAT_YELLOW:
        case DASH_CONFIG_THRESHOLD_AFR_LEAN_YELLOW:
        case DASH_CONFIG_THRESHOLD_MAP_YELLOW:
        case DASH_CONFIG_THRESHOLD_TPS_YELLOW:
        case DASH_CONFIG_THRESHOLD_DUTY_YELLOW:
        case DASH_CONFIG_THRESHOLD_KNOCK_AMBER:
            if (value_tenths >= s_thresholds[threshold + 1]) {
                value_tenths = s_thresholds[threshold + 1] - THRESHOLD_STEPS[threshold];
            }
            break;
        case DASH_CONFIG_THRESHOLD_ECT_HIGH:
        case DASH_CONFIG_THRESHOLD_IAT_HIGH:
        case DASH_CONFIG_THRESHOLD_AFR_LEAN:
        case DASH_CONFIG_THRESHOLD_MAP_HIGH:
        case DASH_CONFIG_THRESHOLD_TPS_HIGH:
        case DASH_CONFIG_THRESHOLD_DUTY_HIGH:
        case DASH_CONFIG_THRESHOLD_KNOCK_RED:
            if (value_tenths <= s_thresholds[threshold - 1]) {
                value_tenths = s_thresholds[threshold - 1] + THRESHOLD_STEPS[threshold];
            }
            break;
        case DASH_CONFIG_THRESHOLD_AFR_RICH_YELLOW:
        case DASH_CONFIG_THRESHOLD_BATT_YELLOW:
        case DASH_CONFIG_THRESHOLD_OIL_YELLOW:
            if (value_tenths <= s_thresholds[threshold + 1]) {
                value_tenths = s_thresholds[threshold + 1] + THRESHOLD_STEPS[threshold];
            }
            break;
        case DASH_CONFIG_THRESHOLD_AFR_RICH:
        case DASH_CONFIG_THRESHOLD_BATT_LOW:
        case DASH_CONFIG_THRESHOLD_OIL_LOW:
            if (value_tenths >= s_thresholds[threshold - 1]) {
                value_tenths = s_thresholds[threshold - 1] - THRESHOLD_STEPS[threshold];
            }
            break;
        default:
            break;
    }
    s_thresholds[threshold] = value_tenths;

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, THRESHOLD_KEYS[threshold], (int32_t)value_tenths);
    nvs_commit(h);
    nvs_close(h);
}

void dash_config_factory_reset(void)
{
    s_metric = false;
    s_speed_kph = false;
    s_temperature_celsius = false;
    s_pressure_kpa = false;
    s_distance_km = false;
    s_vtec_rpm = DEFAULT_VTEC_RPM;
    s_redline_rpm = DEFAULT_REDLINE_RPM;
    s_chime_warning_mask = DASH_CONFIG_CHIME_WARNING_ALL;
    s_chime_volume = DEFAULT_CHIME_VOLUME;
    s_brightness = DEFAULT_BRIGHTNESS;
    s_show_sim_button = true;
    s_value_smoothing = false;
    s_redline_screen_flash = true;
    s_redline_flash_color = DEFAULT_REDLINE_FLASH_COLOR;
    memcpy(s_hal_field_channels, HAL_FIELD_DEFAULTS, sizeof(s_hal_field_channels));
    memcpy(s_modern_field_channels, MODERN_FIELD_DEFAULTS, sizeof(s_modern_field_channels));
    memcpy(s_race_field_channels, RACE_FIELD_DEFAULTS, sizeof(s_race_field_channels));
    memcpy(s_endurance_field_channels, ENDURANCE_FIELD_DEFAULTS, sizeof(s_endurance_field_channels));
    memcpy(s_touring_field_channels, TOURING_FIELD_DEFAULTS, sizeof(s_touring_field_channels));
    memcpy(s_thresholds, THRESHOLD_DEFAULTS, sizeof(s_thresholds));
    strncpy(s_can_protocol, "hondata", sizeof(s_can_protocol) - 1);
    s_can_protocol[sizeof(s_can_protocol) - 1] = '\0';

    nvs_handle_t h;
    if (nvs_open(DASH_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_i32(h, KEY_METRIC, 0);
    nvs_set_i32(h, KEY_SPEED_KPH, 0);
    nvs_set_i32(h, KEY_TEMP_C, 0);
    nvs_set_i32(h, KEY_PRESSURE_KPA, 0);
    nvs_set_i32(h, KEY_DISTANCE_KM, 0);
    nvs_set_i32(h, KEY_VTEC_RPM, (int32_t)s_vtec_rpm);
    nvs_set_i32(h, KEY_REDLINE_RPM, (int32_t)s_redline_rpm);
    nvs_set_i32(h, KEY_CHIME_MASK, (int32_t)s_chime_warning_mask);
    nvs_set_i32(h, KEY_CHIME_VOLUME, (int32_t)s_chime_volume);
    nvs_set_i32(h, KEY_BRIGHTNESS, (int32_t)s_brightness);
    nvs_set_i32(h, KEY_SHOW_SIM, 1);
    nvs_set_i32(h, KEY_VALUE_SMOOTH, 0);
    nvs_set_i32(h, KEY_REDLINE_FLASH, 1);
    nvs_set_i32(h, KEY_REDLINE_FLASH_COLOR, (int32_t)s_redline_flash_color);
    for (int i = 0; i < DASH_CONFIG_HAL_FIELD_COUNT; ++i) {
        nvs_set_i32(h, HAL_FIELD_KEYS[i], (int32_t)s_hal_field_channels[i]);
    }
    for (int i = 0; i < DASH_CONFIG_MODERN_FIELD_COUNT; ++i) {
        nvs_set_i32(h, MODERN_FIELD_KEYS[i], (int32_t)s_modern_field_channels[i]);
    }
    for (int i = 0; i < DASH_CONFIG_RACE_FIELD_COUNT; ++i) {
        nvs_set_i32(h, RACE_FIELD_KEYS[i], (int32_t)s_race_field_channels[i]);
    }
    for (int i = 0; i < DASH_CONFIG_ENDURANCE_FIELD_COUNT; ++i) {
        nvs_set_i32(h, ENDURANCE_FIELD_KEYS[i], (int32_t)s_endurance_field_channels[i]);
    }
    for (int i = 0; i < DASH_CONFIG_TOURING_FIELD_COUNT; ++i) {
        nvs_set_i32(h, TOURING_FIELD_KEYS[i], (int32_t)s_touring_field_channels[i]);
    }
    for (int i = 0; i < DASH_CONFIG_THRESHOLD_COUNT; ++i) {
        nvs_set_i32(h, THRESHOLD_KEYS[i], (int32_t)s_thresholds[i]);
    }
    nvs_set_str(h, KEY_CAN_PROTOCOL, s_can_protocol);
    nvs_commit(h);
    nvs_close(h);
}
