/*
 * honda_dash_ui.c
 * ------------------------------------------------------------------
 * See honda_dash_ui.h for usage, required lv_conf.h flags, and font notes.
 */

#include "esp_heap_caps.h"
#include "honda_dash_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/display.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "extra/libs/qrcode/lv_qrcode.h"
#include "ota_update.h"
#include "dash_sim.h"
#include "dash_config.h"
#include "dashboard_i18n.h"
#include "data_logger.h"
#include "device_log_viewer.h"
#include "achievement_system.h"
#include "session_peaks.h"
#include "theme_storage.h"
#include "boot_logo_storage.h"
#include "runtime_theme.h"
#include "odometer/odometer.h"
#include "dashboard_runtime.h"
#include "canbus.h"

#define lv_label_set_text(object, text) \
    lv_label_set_text((object), dashboard_i18n_translate(text))
#define lv_dropdown_set_options(object, options) \
    lv_dropdown_set_options((object), dashboard_i18n_translate(options))

LV_IMG_DECLARE(huntertuned_logo);

#if LV_USE_QRCODE
extern lv_obj_t * lv_qrcode_create(lv_obj_t * parent, lv_coord_t size, lv_color_t dark_color, lv_color_t light_color);
extern lv_res_t lv_qrcode_update(lv_obj_t * qrcode, const void * data, uint32_t data_len);
#endif

/* ================= FONTS (swap these for custom Barlow Condensed fonts) === */
static lv_font_t *s_i18n_font_14;
static lv_font_t s_font_12;
static lv_font_t s_font_14;
static lv_font_t s_font_28;
static lv_font_t s_font_40;
static lv_font_t s_font_44;

#define DASH_FONT_SPEED    (s_i18n_font_14 ? &s_font_44 : &lv_font_montserrat_44)
#define DASH_FONT_RPM      (s_i18n_font_14 ? &s_font_44 : &lv_font_montserrat_44)
#define DASH_FONT_GEAR     (s_i18n_font_14 ? &s_font_44 : &lv_font_montserrat_44)
#define DASH_FONT_TILEVAL  (s_i18n_font_14 ? &s_font_28 : &lv_font_montserrat_28)
#define DASH_FONT_LABEL    (s_i18n_font_14 ? &s_font_12 : &lv_font_montserrat_12)
#define DASH_FONT_LABEL14  (s_i18n_font_14 ? &s_font_14 : &lv_font_montserrat_14)
#define DASH_FONT_HAL_VALUE (s_i18n_font_14 ? &s_font_40 : &lv_font_montserrat_40)

extern const uint8_t dashboard_i18n_ttf_start[] asm("_binary_Dashboard_I18n_ttf_start");
extern const uint8_t dashboard_i18n_ttf_end[] asm("_binary_Dashboard_I18n_ttf_end");

static lv_font_t *create_i18n_font(int size, const lv_font_t *fallback)
{
    size_t data_size = (size_t)(dashboard_i18n_ttf_end - dashboard_i18n_ttf_start);
    size_t cache_size = (size_t)size * (size_t)size;
    if (cache_size < 4096) cache_size = 4096;
    lv_font_t *font = lv_tiny_ttf_create_data_ex(dashboard_i18n_ttf_start, data_size,
                                                  size, cache_size);
    if (font) font->fallback = fallback;
    return font;
}

static void initialize_i18n_fonts(void)
{
    if (dash_config_get_language() == DASH_CONFIG_LANGUAGE_ENGLISH || s_i18n_font_14) return;
    s_i18n_font_14 = create_i18n_font(14, NULL);
    if (!s_i18n_font_14) return;

    s_font_12 = lv_font_montserrat_12;
    s_font_14 = lv_font_montserrat_14;
    s_font_28 = lv_font_montserrat_28;
    s_font_40 = lv_font_montserrat_40;
    s_font_44 = lv_font_montserrat_44;
    s_font_12.fallback = s_i18n_font_14;
    s_font_14.fallback = s_i18n_font_14;
    s_font_28.fallback = s_i18n_font_14;
    s_font_40.fallback = s_i18n_font_14;
    s_font_44.fallback = s_i18n_font_14;
}

/* ================= PALETTE (matches the web mockup 1:1) =================== */
#define C_VOID       lv_color_hex(0x08090a)
#define C_PANEL      lv_color_hex(0x151619)
#define C_LINE       lv_color_hex(0x2a2c31)
#define C_RED        lv_color_hex(0xe4002b)
#define C_RED_DEEP   lv_color_hex(0x4a0413)
#define C_WHITE      lv_color_hex(0xf4f3ef)
#define C_AMBER      lv_color_hex(0xffb020)
#define C_GREEN      lv_color_hex(0x39ff8c)
#define C_GREEN_DEEP lv_color_hex(0x0e3e2a)
#define C_LABEL      lv_color_hex(0x84868d)
#define C_LABEL_DIM  lv_color_hex(0x4c4e54)
#define C_SEG_OFF    lv_color_hex(0x232429)

/* per-tile accent colors now live directly in TILE_DEFS[].accent_hex */

/* gradient stops used on the RPM bar after VTEC engages */
#define C_GRAD_WHITE  lv_color_hex(0xffffff)
#define C_GRAD_YELLOW lv_color_hex(0xffe066)
#define C_GRAD_ORANGE lv_color_hex(0xff8c1a)
#define C_GRAD_RED    lv_color_hex(0xe4002b)

/* TEMPORARY diagnostic instrumentation for the Track/Retro lag investigation --
   logs each theme's update_theme_X() duration to the console periodically.
   Safe to leave on for now (throttled to ~1 log line per 2s per theme, plus
   an immediate line on anything unusually slow); flip to 0 once the lag
   question is settled and this isn't needed anymore. */
/* ================= TUNING CONSTANTS ======================================= */
/* These four are runtime-adjustable via the Config page (dash_config.c
   persists them to NVS) -- kept as plain variables instead of #define so
   every existing use site below picks up a changed value automatically
   without needing to be touched. Confirmed none of them are ever used as
   an array size, which is the one thing that would have required them
   to stay compile-time constants. */
static int REDLINE      = 8400;
static int FULL_RED_RPM = 7800;   /* bar goes solid red + screen flashes past this */
static int MAXRPM       = 9000;
static int VTEC_RPM     = 5600;
#define SEG_COUNT   64

/* screen is a fixed 1024x600 panel */
#define SCR_W 1024
#define SCR_H 600
#define DASH_CONTROL_ROW_WIDTH 210

/* ================= TILE DESCRIPTOR (mirrors the web version's `fields`) === */
typedef enum {
    TILE_ECT = 0,
    TILE_IAT,
    TILE_AFR,
    TILE_TIMING,
    TILE_MAP,
    TILE_BATT,
    TILE_TPS,
    TILE_OIL,
    TILE_DUTY,
    TILE_KNOCK,
    TILE_COUNT
} tile_id_t;

typedef struct {
    float    lo, hi;
    uint32_t color_hex;
} zone_t;

typedef struct {
    const char   *name;
    const char   *unit;
    uint32_t      accent_hex;   /* raw hex; converted with lv_color_hex() at point of use */
    float         min, max;     /* bar width scale */
    const zone_t *zones;        /* bar FILL color by value range (blue=cold, green=good, red=bad) */
    uint8_t       zone_count;
    uint8_t       dp;           /* decimal places */
} tile_def_t;

/* --- zone tables, from the car's actual value/color spec --- */
static const zone_t ZONES_ECT[]    = { {0,150,0x4d8fff}, {150,205,0x39ff8c}, {205,300,0xe4002b} };
static const zone_t ZONES_IAT[]    = { {0,60, 0x4d8fff}, {60,110, 0x39ff8c}, {110,200,0xe4002b} };
static const zone_t ZONES_AFR[]    = { {9,10.5f,0xe4002b}, {10.5f,11.5f,0x39ff8c}, {11.5f,20,0xe4002b} };
static const zone_t ZONES_TIMING[] = { {0,36,0x39ff8c} }; /* always green */
static const zone_t ZONES_MAP[]    = { {-20,16,0x39ff8c}, {16,20,0xe4002b} };
static const zone_t ZONES_BATT[]   = { {0,12,0xe4002b}, {12,20,0x39ff8c} };
static const zone_t ZONES_TPS[]    = { {0,80,0x39ff8c}, {80,100,0xe4002b} };
static const zone_t ZONES_OIL[]    = { {0,10,0xe4002b}, {10,120,0x39ff8c} };
static const zone_t ZONES_DUTY[]   = { {0,75,0x39ff8c}, {75,100,0xe4002b} };
static const zone_t ZONES_KNOCK[]  = { {0,2,0x4c4e54}, {2,4,0xffb020}, {4,6,0xe4002b} };

static const tile_def_t TILE_DEFS[TILE_COUNT] = {
    /* name              unit           accent    min    max    zones          zone_count  dp */
    { "COOLANT TEMP",    "\xC2\xB0" "F", 0x4d8fff, 0,    300,   ZONES_ECT,    3, 0 },
    { "INTAKE AIR TEMP", "\xC2\xB0" "F", 0xfb923c, 0,    200,   ZONES_IAT,    3, 0 },
    { "AFR / O2",        ":1",           0xc084fc, 9,    20,    ZONES_AFR,    3, 1 },
    { "IGNITION TIMING", "\xC2\xB0" " BTDC", 0xf472b6, 0, 36,   ZONES_TIMING, 1, 0 },
    { "MAP",             "PSI",          0x38bdf8, -20,  20,    ZONES_MAP,    2, 1 },
    { "BATTERY",         "V",            0xfbbf24, 0,    20,    ZONES_BATT,   2, 1 },
    { "THROTTLE POS",    "%",            0x2dd4bf, 0,    100,   ZONES_TPS,    2, 0 },
    { "OIL PRESSURE",    "PSI",          0x818cf8, 0,    120,   ZONES_OIL,    2, 0 },
    { "INJECTOR DUTY",   "%",            0xa3e635, 0,    100,   ZONES_DUTY,   2, 0 },
    { "KNOCK RETARD",    "\xC2\xB0",     0x4c4e54, 0,    6,     ZONES_KNOCK,  3, 1 },
};

/* live widget handles for each tile */
typedef struct {
    lv_obj_t *tile;    /* the tile container (border-left color changes)   */
    lv_obj_t *label;   /* small caps title                                 */
    lv_obj_t *unit;
    lv_obj_t *value;   /* big number                                       */
    lv_obj_t *value_outline[4]; /* black copies behind the value, offset 1px N/S/E/W -- fakes a text outline */
    lv_obj_t *bar;     /* thick progress bar along the bottom              */
    char last_text[24];   /* last formatted value string -- skip redraw if unchanged */
    uint32_t last_zone_hex; /* last zone color -- skip redraw if unchanged too */
} tile_widgets_t;

static tile_widgets_t s_tiles[TILE_COUNT];

/* RPM bar segments and top-readout indicator lights */
static lv_obj_t *s_segs[SEG_COUNT];
static lv_obj_t *s_rpm_val_label;
static lv_obj_t *s_tag_vtec;
static lv_obj_t *s_tag_shift;
static lv_obj_t *s_tag_vtec_label;
static lv_obj_t *s_tag_shift_label;

/* speed + gear */
static lv_obj_t *s_speed_val_label;
static lv_obj_t *s_speed_unit_label;
static lv_obj_t *s_gear_num_label;
static lv_obj_t *s_rpm_mini_fill;

/* telltales */
typedef struct {
    lv_obj_t *dot;
    lv_color_t on_color;
} telltale_t;

static telltale_t s_tell_cel;

/* odometer + fuel level (bottom corners of the telltale strip) */
static lv_obj_t *s_odo_val_label;
static lv_obj_t *s_odo_caption_label;
static lv_obj_t *s_achievement_count_label;
static lv_obj_t *s_trip_reset_modal;
static int s_trip_reset_pending;

/* which value the Modern ODO tile currently shows: 0=main odometer,
    1=trip A, 2=trip B. */
static int s_odo_display_mode = 0;

static void odo_tiles_refresh_display(void);
static void trip_reset_request(int trip);

static void odo_reset_long_press_cb(lv_event_t *event)
{
    (void)event;
    trip_reset_request(s_odo_display_mode);
}

static void odo_cycle_cb(lv_event_t *e)
{
    (void)e;
    s_odo_display_mode = (s_odo_display_mode + 1) % 3;
    odo_tiles_refresh_display();
}
static lv_obj_t *s_fuel_val_label;
static lv_obj_t *s_fuel_bar;

/* theme containers -- exactly one is visible at a time */
static lv_obj_t *s_theme_modern;
static lv_obj_t *s_theme_race_lcd;
static lv_obj_t *s_theme_haldash;
static lv_obj_t *s_theme_endurance;
static lv_obj_t *s_theme_touring;
static lv_obj_t *s_cluster;

#define THEME_ID_MODERN   0
#define THEME_ID_RACE_LCD 6
#define THEME_ID_HALDASH  7
#define THEME_ID_ENDURANCE 8
#define THEME_ID_TOURING  9

/* Keep Race LCD at index 6 so existing NVS selections remain compatible. */
static int s_active_theme = 0;

/* cached snapshot of the last data pushed in, so switching themes can
   immediately paint fresh values into the newly-shown theme instead of
   waiting for the next update() call */
static honda_dash_data_t s_last_data;
static bool s_have_last_data = false;
static honda_dash_data_t s_warning_data;
static bool s_warning_have_data;
static lv_obj_t *s_warning_banner;
static lv_obj_t *s_warning_title;
static lv_obj_t *s_warning_detail;
static lv_obj_t *s_can_status_badge;
static lv_obj_t *s_can_status_label;
static lv_obj_t *s_record_status_badge;
static lv_obj_t *s_record_status_label;
static int s_shift_light_last_theme = -1;
static int s_shift_light_last_bucket = -1;
static int s_shift_light_last_enabled = -1;
static int s_shift_light_last_flash_on = -1;
static int s_shift_light_last_brightness = -1;
static int s_shift_light_last_rpms[DASH_CONFIG_SHIFT_STAGE_COUNT] = {-1, -1, -1};
static int s_shift_light_last_colors[DASH_CONFIG_SHIFT_STAGE_COUNT] = {-1, -1, -1};

static void add_press_feedback(lv_obj_t *obj);
static void record_btn_cb(lv_event_t *event);
static void sim_btn_cb(lv_event_t *event);
static void sim_btn_long_press_cb(lv_event_t *event);
static void update_theme_modern(const honda_dash_data_t *data, int rpm, bool limiter_hit, bool vtec_on, float fuel);

static void odo_tiles_refresh_display(void)
{
    const char *caption = (s_odo_display_mode == 0) ? "ODO"
                        : (s_odo_display_mode == 1) ? "TRIP A" : "TRIP B";
    double miles = (s_odo_display_mode == 1) ? odometer_get_trip_a_miles()
                 : (s_odo_display_mode == 2) ? odometer_get_trip_b_miles()
                 : odometer_get_miles();
    if (dash_config_get_distance_km()) {
        miles *= 1.60934;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%.0f %s", miles, dash_config_get_distance_km() ? "KM" : "MI");

    if (s_odo_caption_label) lv_label_set_text(s_odo_caption_label, caption);
    if (s_odo_val_label) lv_label_set_text(s_odo_val_label, buf);
    if (s_achievement_count_label) {
        snprintf(buf, sizeof(buf), "%u/%u", achievement_system_get_count(),
                 ACHIEVEMENT_SYSTEM_TOTAL);
        lv_label_set_text(s_achievement_count_label, buf);
    }
}

/* ---- Race LCD theme widget handles ---- */
#define RPK_LAMP_COUNT 10
#define RPK_TACH_COUNT 36
#define RPK_LCD_BLUE lv_color_hex(0x0077ff)
static lv_obj_t *s_rpk_lamps[RPK_LAMP_COUNT];
static lv_obj_t *s_rpk_tach[RPK_TACH_COUNT];
static lv_obj_t *s_rpk_rpm_val;
static lv_obj_t *s_rpk_gear_val;
static lv_obj_t *s_rpk_speed_val;
static lv_obj_t *s_rpk_speed_label;
static lv_obj_t *s_rpk_field_label[DASH_CONFIG_RACE_FIELD_COUNT];
static lv_obj_t *s_rpk_field_val[DASH_CONFIG_RACE_FIELD_COUNT];
static char s_rpk_last_field_text[DASH_CONFIG_RACE_FIELD_COUNT][16];
static int s_rpk_last_rpm = -1;
static int s_rpk_last_gear = -2;
static int s_rpk_last_speed = -1;
static int s_rpk_last_speed_kph = -1;
static void update_theme_race_lcd(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel);

/* ---- HalDash theme widget handles ---- */
#define HAL_RED lv_color_hex(0xe21b2d)
#define HAL_GAUGE_OUTER_SIZE 430
#define HAL_GAUGE_INNER_SIZE 316
#define HAL_GAUGE_CENTER     158
static lv_obj_t *s_hal_rpm_arc;
static lv_obj_t *s_hal_speed_arc;
static lv_obj_t *s_hal_rpm_needle_base;
static lv_obj_t *s_hal_speed_needle_base;
static lv_obj_t *s_hal_rpm_needle;
static lv_obj_t *s_hal_speed_needle;
static lv_point_t s_hal_rpm_needle_base_points[2];
static lv_point_t s_hal_speed_needle_base_points[2];
static lv_point_t s_hal_rpm_needle_points[2];
static lv_point_t s_hal_speed_needle_points[2];
static lv_obj_t *s_hal_rpm_val;
static lv_obj_t *s_hal_speed_val;
static lv_obj_t *s_hal_gear_val;
static lv_obj_t *s_hal_field_val[8];
static lv_obj_t *s_hal_field_label[8];
static lv_obj_t *s_hal_field_unit[8];
static char s_hal_last_field_text[8][16];
static lv_obj_t *s_hal_channel_modal;
static lv_obj_t *s_hal_channel_dropdown;
static lv_obj_t *s_hal_threshold_panel;
static int s_hal_edit_slot = -1;
typedef enum {
    SYSTEM_FIELD_HAL = 0,
    SYSTEM_FIELD_MODERN,
    SYSTEM_FIELD_RACE,
    SYSTEM_FIELD_ENDURANCE,
    SYSTEM_FIELD_TOURING,
} system_field_theme_t;
static system_field_theme_t s_system_edit_theme = SYSTEM_FIELD_HAL;
static void hal_refresh_field_identity(int slot);
static void system_field_refresh_identity(system_field_theme_t theme, int slot);
static void system_field_long_press_cb(lv_event_t *e);
static lv_color_t system_field_value_color(int channel, float value, bool available);
static float system_field_display_value(int channel, float value);
static void system_field_refresh_live_unit(system_field_theme_t theme, int slot,
                                           int channel, float value);
static void update_theme_haldash(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel);

/* ---- HunterDash theme widget handles (retains Endurance's persisted slot) ---- */
#define END_SEG_COUNT 32
#define HUNTER_RED lv_color_hex(0xe31b23)
#define HUNTER_SEG_OFF lv_color_hex(0x282b2f)
static lv_obj_t *s_end_segments[END_SEG_COUNT];
static lv_obj_t *s_end_rpm_val;
static lv_obj_t *s_end_speed_val;
static lv_obj_t *s_end_speed_unit;
static lv_obj_t *s_end_gear_val;
static lv_obj_t *s_end_shift_light;
static lv_obj_t *s_end_shift_label;
static lv_obj_t *s_end_vtec_light;
static lv_obj_t *s_end_vtec_label;
static lv_obj_t *s_end_cel_light;
static lv_obj_t *s_end_cel_label;
static lv_obj_t *s_end_field_label[DASH_CONFIG_ENDURANCE_FIELD_COUNT];
static lv_obj_t *s_end_field_unit[DASH_CONFIG_ENDURANCE_FIELD_COUNT];
static lv_obj_t *s_end_field_val[DASH_CONFIG_ENDURANCE_FIELD_COUNT];
static char s_end_last_field_text[DASH_CONFIG_ENDURANCE_FIELD_COUNT][16];
static void update_theme_endurance(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel);

/* ---- Rally Stage theme widget handles (retains Touring's persisted slot) ---- */
#define TOUR_SEG_COUNT 20
#define RALLY_LIGHT_OFF lv_color_hex(0x25282b)
#define RALLY_TEXT_DIM lv_color_hex(0x969b9f)
static lv_obj_t *s_tour_segments[TOUR_SEG_COUNT];
static lv_obj_t *s_tour_speed_val;
static lv_obj_t *s_tour_speed_unit;
static lv_obj_t *s_tour_rpm_val;
static lv_obj_t *s_tour_odo_val;
static lv_obj_t *s_tour_field_label[DASH_CONFIG_TOURING_FIELD_COUNT];
static lv_obj_t *s_tour_field_unit[DASH_CONFIG_TOURING_FIELD_COUNT];
static lv_obj_t *s_tour_field_val[DASH_CONFIG_TOURING_FIELD_COUNT];
static char s_tour_last_field_text[DASH_CONFIG_TOURING_FIELD_COUNT][16];
static void update_theme_touring(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel);

/* ---- settings menu ---- */
static lv_obj_t *s_settings_overlay;
static lv_obj_t *s_quick_brightness_overlay;
static lv_obj_t *s_settings_nav_button;
static lv_obj_t *s_settings_nav_label;
static lv_obj_t *s_settings_current_page;
static lv_obj_t *s_page_main;
static lv_obj_t *s_page_theme;
static lv_obj_t *s_page_logs_menu;
static lv_obj_t *s_page_info;
static lv_obj_t *s_page_peaks;
static lv_obj_t *s_page_logs;
static lv_obj_t *s_page_update;
static lv_obj_t *s_page_config;
static lv_obj_t *s_page_units;
static lv_obj_t *s_page_display;
static lv_obj_t *s_page_shortcuts;
static lv_obj_t *s_page_readme;
static lv_obj_t *s_page_odometer;
static lv_obj_t *s_page_fuel;
static lv_obj_t *s_page_engine_limits;
static lv_obj_t *s_page_achievements;
static lv_obj_t *s_page_ecu;
static lv_obj_t *s_page_theme_resets;
static lv_obj_t *s_page_contact;
enum {
    DIAG_CAN_BUS = 0,
    DIAG_PROTOCOL,
    DIAG_LAST_FRAME,
    DIAG_SIGNALS,
    DIAG_ERRORS,
    DIAG_SYSTEM,
    DIAG_LVGL_HEAP,
    DIAG_MEMORY,
    DIAG_TASK_STACKS,
    DIAG_DISPLAY,
    DIAG_COUNT,
};
static lv_obj_t *s_diagnostics_values[DIAG_COUNT];
static lv_timer_t *s_diagnostics_timer;
static lv_obj_t *s_peaks_scroll;
enum {
    PEAK_RPM = 0,
    PEAK_SPEED,
    PEAK_BOOST,
    PEAK_COOLANT,
    PEAK_INTAKE,
    PEAK_DUTY,
    PEAK_KNOCK,
    PEAK_AFR_MIN,
    PEAK_OIL_MIN,
    PEAK_BATTERY_MIN,
    PEAK_VALUE_COUNT,
};
static lv_obj_t *s_peak_values[PEAK_VALUE_COUNT];
static lv_obj_t *s_achievement_summary_label;
static lv_obj_t *s_achievement_hit_labels[ACHIEVEMENT_SYSTEM_TOTAL];
static lv_obj_t *s_achievement_record_labels[ACHIEVEMENT_SYSTEM_TOTAL];
static device_log_file_t s_log_files[DEVICE_LOG_MAX_FILES];
static size_t s_log_file_count;
static device_log_chart_t s_log_chart_data;
static lv_obj_t *s_log_file_dropdown;
static lv_obj_t *s_log_preset_dropdown;
static lv_obj_t *s_log_status_label;
static lv_obj_t *s_log_readout_label;
static lv_obj_t *s_log_chart_view;
static lv_obj_t *s_log_chart_content;
static lv_obj_t *s_log_charts[DEVICE_LOG_MAX_SERIES];
static lv_chart_series_t *s_log_chart_series[DEVICE_LOG_MAX_SERIES];
static lv_chart_cursor_t *s_log_chart_cursors[DEVICE_LOG_MAX_SERIES];

/* ---- config page widget handles + pending (not-yet-saved) state ---- */
typedef enum {
    UNIT_SETTING_SPEED = 0,
    UNIT_SETTING_TEMPERATURE,
    UNIT_SETTING_PRESSURE,
    UNIT_SETTING_DISTANCE,
    UNIT_SETTING_COUNT,
} unit_setting_t;
static lv_obj_t *s_cfg_unit_switches[UNIT_SETTING_COUNT];
static lv_obj_t *s_cfg_unit_values[UNIT_SETTING_COUNT];
static bool cfg_unit_setting_get(unit_setting_t setting);
static const char *cfg_unit_setting_value(unit_setting_t setting, bool enabled);
static lv_obj_t *s_cfg_odo_label;
static lv_obj_t *s_cfg_trip_a_label;
static lv_obj_t *s_cfg_trip_b_label;
static lv_obj_t *s_cfg_vtec_label;
static lv_obj_t *s_cfg_vtec_slider;
static lv_obj_t *s_cfg_redline_label;
static lv_obj_t *s_cfg_redline_slider;
static lv_obj_t *s_cfg_restart_note;
static lv_obj_t *s_cfg_sim_button_switch;
static lv_obj_t *s_cfg_smoothing_dropdown;
static lv_obj_t *s_cfg_fuel_input_dropdown;
static lv_obj_t *s_cfg_fuel_voltage_sliders[2];
static lv_obj_t *s_cfg_fuel_voltage_labels[2];
static lv_obj_t *s_cfg_fuel_live_label;
static lv_obj_t *s_cfg_boot_logo_dropdown;
static lv_obj_t *s_cfg_boot_logo_delete_button;
static lv_obj_t *s_cfg_boot_logo_status;
static lv_obj_t *s_cfg_auto_record_switch;
static lv_obj_t *s_cfg_log_name_dropdown;
static lv_obj_t *s_cfg_shift_enable_switch;
static lv_obj_t *s_cfg_shift_stage_sliders[DASH_CONFIG_SHIFT_STAGE_COUNT];
static lv_obj_t *s_cfg_shift_stage_labels[DASH_CONFIG_SHIFT_STAGE_COUNT];
static lv_obj_t *s_cfg_shift_color_dropdowns[DASH_CONFIG_SHIFT_STAGE_COUNT];
static lv_obj_t *s_cfg_shift_brightness_slider;
static lv_obj_t *s_cfg_shift_brightness_label;
static lv_obj_t *s_cfg_shift_flash_switch;
static lv_obj_t *s_factory_reset_modal;
static lv_obj_t *s_cfg_threshold_sliders[DASH_CONFIG_THRESHOLD_COUNT];
static lv_obj_t *s_cfg_threshold_labels[DASH_CONFIG_THRESHOLD_COUNT];
static double s_cfg_odo_pending_miles = 0.0;
static int s_cfg_pending_vtec_rpm = 0;
static int s_cfg_pending_redline_rpm = 0;
static void shift_settings_refresh(void);

static const char *const SHIFT_COLOR_OPTIONS =
    "Green\nYellow\nAmber\nRed\nBlue\nCyan\nWhite\nMagenta";

typedef struct {
    const char *label;
    const char *unit;
    int min_tenths;
    int max_tenths;
    int step_tenths;
} threshold_ui_def_t;

static const threshold_ui_def_t THRESHOLD_UI[DASH_CONFIG_THRESHOLD_COUNT] = {
    [DASH_CONFIG_THRESHOLD_ECT_YELLOW]  = { "Coolant Yellow Above", "\xC2\xB0" "F", 1600, 2600, 10 },
    [DASH_CONFIG_THRESHOLD_ECT_HIGH]    = { "Coolant Red Above", "\xC2\xB0" "F", 1600, 2600, 10 },
    [DASH_CONFIG_THRESHOLD_IAT_YELLOW]  = { "Intake Air Yellow Above", "\xC2\xB0" "F", 600, 2000, 10 },
    [DASH_CONFIG_THRESHOLD_IAT_HIGH]    = { "Intake Air Red Above", "\xC2\xB0" "F", 600, 2000, 10 },
    [DASH_CONFIG_THRESHOLD_AFR_RICH_YELLOW] = { "AFR Rich Yellow Below", ":1", 80, 140, 1 },
    [DASH_CONFIG_THRESHOLD_AFR_RICH]    = { "AFR Rich Red Below", ":1", 80, 140, 1 },
    [DASH_CONFIG_THRESHOLD_AFR_LEAN_YELLOW] = { "AFR Lean Yellow Above", ":1", 100, 200, 1 },
    [DASH_CONFIG_THRESHOLD_AFR_LEAN]    = { "AFR Lean Red Above", ":1", 100, 200, 1 },
    [DASH_CONFIG_THRESHOLD_MAP_YELLOW]  = { "Boost Yellow Above", " PSI", 0, 400, 5 },
    [DASH_CONFIG_THRESHOLD_MAP_HIGH]    = { "Boost Red Above", " PSI", 0, 400, 5 },
    [DASH_CONFIG_THRESHOLD_BATT_YELLOW] = { "Battery Yellow Below", " V", 80, 150, 1 },
    [DASH_CONFIG_THRESHOLD_BATT_LOW]    = { "Battery Red Below", " V", 80, 150, 1 },
    [DASH_CONFIG_THRESHOLD_TPS_YELLOW]  = { "Throttle Yellow Above", "%", 500, 1000, 10 },
    [DASH_CONFIG_THRESHOLD_TPS_HIGH]    = { "Throttle Red Above", "%", 500, 1000, 10 },
    [DASH_CONFIG_THRESHOLD_OIL_YELLOW]  = { "Oil Pressure Yellow Below", " PSI", 0, 500, 10 },
    [DASH_CONFIG_THRESHOLD_OIL_LOW]     = { "Oil Pressure Red Below", " PSI", 0, 500, 10 },
    [DASH_CONFIG_THRESHOLD_DUTY_YELLOW] = { "Injector Duty Yellow Above", "%", 400, 1000, 10 },
    [DASH_CONFIG_THRESHOLD_DUTY_HIGH]   = { "Injector Duty Red Above", "%", 400, 1000, 10 },
    [DASH_CONFIG_THRESHOLD_KNOCK_AMBER] = { "Knock Amber Above", "\xC2\xB0", 0, 80, 1 },
    [DASH_CONFIG_THRESHOLD_KNOCK_RED]   = { "Knock Red Above", "\xC2\xB0", 10, 100, 1 },
};

static const char *const CFG_PROTOCOL_NAMES[] = {
    "", /* auto-detect */
    "hondata",
    "haltech",
    "link_g4x",
    "megasquirt",
    "emtron",
    "maxxecu_default",
    "emu_black",
};
#define CFG_PROTOCOL_COUNT (sizeof(CFG_PROTOCOL_NAMES) / sizeof(CFG_PROTOCOL_NAMES[0]))
static const char *const CFG_PROTOCOL_LABELS[] = {
    "Auto-detect",
    "Hondata",
    "Haltech",
    "Link G4X",
    "MegaSquirt",
    "Emtron",
    "MaxxECU",
    "ECUMaster Black",
};
static lv_obj_t *s_cfg_protocol_tiles[CFG_PROTOCOL_COUNT];
static lv_obj_t *s_cfg_protocol_status_label;
static lv_obj_t *s_ecu_restart_note;
static void cfg_protocol_tiles_refresh(void);
static lv_obj_t *s_brightness_slider;
static lv_obj_t *s_brightness_value_label;
static lv_obj_t *s_update_status_label;
static lv_obj_t *s_update_details_label;
static lv_obj_t *s_update_qr;
static lv_obj_t *s_contact_qr;
static lv_obj_t *s_contact_destination_label;
static lv_timer_t *s_update_poll_timer;
static bool s_update_background_paused = false;
static bool s_render_paused = false;
static void sim_buttons_apply_visibility(void);

/* keep grid/flex descriptor arrays alive for LVGL (must not be stack-local) */
static lv_coord_t s_grid_col_dsc[TILE_COUNT / 2 + 1];
static lv_coord_t s_grid_row_dsc[3];

/* avoid re-coloring all 64 segments every single update when RPM barely moved */
static int s_last_seg_bucket = -1;
static bool s_last_seg_limiter = false;

/* ================= SMALL HELPERS =========================================== */

static lv_color_t lerp_color(lv_color_t a, lv_color_t b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    /* lv_color_mix: mix=255 -> pure c1(a), mix=0 -> pure c2(b) */
    uint8_t mix = (uint8_t)((1.0f - t) * 255.0f);
    return lv_color_mix(a, b, mix);
}

/* white -> yellow -> orange -> red across t in [0,1] (t=0 at VTEC, t=1 at redline) */
static lv_color_t rpm_grad_color(float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (t <= 0.35f) {
        return lerp_color(C_GRAD_WHITE, C_GRAD_YELLOW, t / 0.35f);
    } else if (t <= 0.68f) {
        return lerp_color(C_GRAD_YELLOW, C_GRAD_ORANGE, (t - 0.35f) / (0.68f - 0.35f));
    } else {
        return lerp_color(C_GRAD_ORANGE, C_GRAD_RED, (t - 0.68f) / (1.0f - 0.68f));
    }
}

static lv_color_t seg_color_for_rpm(int rpm)
{
    if (rpm <= 0) return C_SEG_OFF;
    if (rpm < VTEC_RPM) return C_WHITE;
    float t = (float)(rpm - VTEC_RPM) / (float)(REDLINE - VTEC_RPM);
    return rpm_grad_color(t);
}

/* ================= WIDGET BUILDERS ========================================= */

static lv_obj_t *make_plain_container(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    return l;
}

/* LVGL v8 has no native text-stroke, so this fakes an outline by stacking
   4 black copies of the label 1px N/S/E/W behind the real (white) one.
   `fg_label` must already exist with its final text/font/position set.
   If `out_shadows` is non-NULL, the 4 new label handles are written there
   so callers can keep their text in sync when the value changes. */
static void add_text_outline(lv_obj_t *fg_label, const lv_font_t *font, lv_obj_t *out_shadows[4])
{
    /* fg_label lives inside a flex/grid container; its real on-screen
       position isn't resolved until LVGL runs a layout pass, which doesn't
       happen synchronously on creation. Force it now so align_to below
       uses the label's actual final position, not a stale default. This
       matters most for labels (like the tile name) that are never
       re-aligned after creation -- the value label happens to "self heal"
       every update() call, which is why only the labels looked wrong. */
    lv_obj_update_layout(fg_label);

    lv_obj_t *parent = lv_obj_get_parent(fg_label);
    const int off = 1;
    const int dx[4] = { -off, off, 0, 0 };
    const int dy[4] = { 0, 0, -off, off };
    const char *text = lv_label_get_text(fg_label);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *s = lv_label_create(parent);
        lv_label_set_text(s, text);
        lv_obj_set_style_text_font(s, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(s, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align_to(s, fg_label, LV_ALIGN_CENTER, dx[i], dy[i]);
        lv_obj_move_background(s);
        if (out_shadows) out_shadows[i] = s;
    }
}

/* ---- RPM bar -------------------------------------------------------------- */

/* dome taper: bars are tall/flat through the middle, shorter toward both
   ends -- traces the bezel's domed cutout instead of a flat rectangle */
#define DOME_MAX_SEG_H  104.0f
#define DOME_SHRINK     0.55f
static float dome_height_frac(float t)
{
    return 1.0f - DOME_SHRINK * powf(fabsf(t), 1.8f);
}

static lv_obj_t *build_rpm_wrap(lv_obj_t *parent)
{
    const lv_coord_t wrap_w = SCR_W;

    lv_obj_t *wrap = make_plain_container(parent);
    lv_obj_set_size(wrap, wrap_w, (lv_coord_t)(SCR_H * 0.23f));
    lv_obj_set_style_bg_color(wrap, lv_color_hex(0x0c0d0f), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(wrap, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(wrap, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(wrap, C_LINE, LV_PART_MAIN);
    /* rounded + clipped top corners so the bar's outer segments don't poke
       out past the physical bezel's rounded cutout */
    lv_obj_set_style_radius(wrap, 28, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(wrap, true, LV_PART_MAIN);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_left(wrap, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(wrap, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(wrap, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(wrap, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(wrap, 6, LV_PART_MAIN);

    /* -- segment bar: bottom-aligned, each segment's own height follows the dome taper -- */
    lv_obj_t *segbar = make_plain_container(wrap);
    lv_obj_set_size(segbar, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(segbar, 1);
    lv_obj_set_flex_flow(segbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(segbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(segbar, 4, LV_PART_MAIN);

    for (int i = 0; i < SEG_COUNT; i++) {
        lv_obj_t *seg = lv_obj_create(segbar);
        lv_obj_set_style_bg_color(seg, C_SEG_OFF, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(seg, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(seg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_grow(seg, 1);
        float t = ((float)i / (float)(SEG_COUNT - 1)) * 2.0f - 1.0f;
        lv_obj_set_height(seg, (lv_coord_t)(DOME_MAX_SEG_H * dome_height_frac(t)));
        s_segs[i] = seg;
    }

    /* VTEC crossover tick mark, drawn on top of the bar at its fixed rpm position */
    lv_coord_t segbar_w = wrap_w - 28; /* screen width minus 14px left/right padding */
    lv_coord_t vtec_x = (lv_coord_t)((float)segbar_w * VTEC_RPM / MAXRPM);

    lv_obj_t *marker = lv_obj_create(segbar);
    lv_obj_add_flag(marker, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(marker, 2, LV_PCT(100));
    lv_obj_set_pos(marker, vtec_x, 0);
    lv_obj_set_style_bg_color(marker, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(marker, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(marker, 0, LV_PART_MAIN);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *marker_label = make_label(segbar, "VTEC", DASH_FONT_LABEL, C_LABEL);
    lv_obj_add_flag(marker_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(marker_label, vtec_x - 14, -16);

    /* scale numbers 0-9 under the bar, evenly spaced across the full width */
    lv_obj_t *scale_row = make_plain_container(wrap);
    lv_obj_set_size(scale_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scale_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scale_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(scale_row, 5, LV_PART_MAIN);
    for (int i = 0; i <= 9; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i);
        make_label(scale_row, buf, DASH_FONT_LABEL, C_LABEL_DIM);
    }

    return wrap;
}

/* ---- speed + gear ---------------------------------------------------------- */

static lv_obj_t *build_top_readout(lv_obj_t *parent)
{
    lv_obj_t *row = make_plain_container(parent);
    lv_obj_set_size(row, LV_PCT(100), 182);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 28, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* Large status lights are positioned independently so the center
       readouts remain perfectly centered regardless of label width. */
    s_tag_vtec = lv_obj_create(row);
    lv_obj_add_flag(s_tag_vtec, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_tag_vtec, 150, 86);
    lv_obj_align(s_tag_vtec, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_tag_vtec, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tag_vtec, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tag_vtec, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_tag_vtec, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tag_vtec, 8, LV_PART_MAIN);
    lv_obj_clear_flag(s_tag_vtec, LV_OBJ_FLAG_SCROLLABLE);
    s_tag_vtec_label = make_label(s_tag_vtec, "VTEC", DASH_FONT_TILEVAL, C_LABEL_DIM);
    lv_obj_center(s_tag_vtec_label);

    s_tag_shift = lv_obj_create(row);
    lv_obj_add_flag(s_tag_shift, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_tag_shift, 150, 86);
    lv_obj_align(s_tag_shift, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(s_tag_shift, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tag_shift, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tag_shift, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_tag_shift, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tag_shift, 8, LV_PART_MAIN);
    lv_obj_clear_flag(s_tag_shift, LV_OBJ_FLAG_SCROLLABLE);
    s_tag_shift_label = make_label(s_tag_shift, "SHIFT", DASH_FONT_TILEVAL, C_LABEL_DIM);
    lv_obj_center(s_tag_shift_label);

    /* ---- RPM: same value/unit rhythm as speed, plus the mini bar ---- */
    lv_obj_t *rpm_block = make_plain_container(row);
    lv_obj_set_size(rpm_block, 150, 118);
    lv_obj_set_flex_flow(rpm_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rpm_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(rpm_block, 5, LV_PART_MAIN);

    s_rpm_val_label = make_label(rpm_block, "850", DASH_FONT_RPM, C_WHITE);
    lv_obj_t *rpm_unit = make_label(rpm_block, "RPM", DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_style_text_letter_space(rpm_unit, 3, LV_PART_MAIN);

    lv_obj_t *rpm_mini_track = lv_obj_create(rpm_block);
    lv_obj_set_size(rpm_mini_track, 96, 4);
    lv_obj_set_style_radius(rpm_mini_track, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rpm_mini_track, C_SEG_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rpm_mini_track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rpm_mini_track, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rpm_mini_track, LV_OBJ_FLAG_SCROLLABLE);

    s_rpm_mini_fill = lv_bar_create(rpm_mini_track);
    lv_obj_set_size(s_rpm_mini_fill, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_rpm_mini_fill);
    lv_obj_set_style_bg_opa(s_rpm_mini_fill, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_rpm_mini_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_rpm_mini_fill, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rpm_mini_fill, C_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_rpm_mini_fill, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_rpm_mini_fill, 2, LV_PART_INDICATOR);
    lv_bar_set_range(s_rpm_mini_fill, 0, 1000);
    lv_bar_set_value(s_rpm_mini_fill, 0, LV_ANIM_OFF);

    /* divider */
    lv_obj_t *div1 = lv_obj_create(row);
    lv_obj_set_size(div1, 1, 106);
    lv_obj_set_style_bg_color(div1, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div1, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(div1, 0, LV_PART_MAIN);
    lv_obj_clear_flag(div1, LV_OBJ_FLAG_SCROLLABLE);

    /* speed */
    lv_obj_t *speed_block = make_plain_container(row);
    lv_obj_set_size(speed_block, 150, 118);
    lv_obj_set_flex_flow(speed_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(speed_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_speed_val_label = make_label(speed_block, "0", DASH_FONT_SPEED, C_WHITE);
    s_speed_unit_label = make_label(speed_block, dash_config_get_speed_kph() ? "KPH" : "MPH", DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_style_text_letter_space(s_speed_unit_label, 3, LV_PART_MAIN);

    /* divider */
    lv_obj_t *div2 = lv_obj_create(row);
    lv_obj_set_size(div2, 1, 106);
    lv_obj_set_style_bg_color(div2, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div2, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(div2, 0, LV_PART_MAIN);
    lv_obj_clear_flag(div2, LV_OBJ_FLAG_SCROLLABLE);

    /* gear */
    lv_obj_t *gear_block = make_plain_container(row);
    lv_obj_set_size(gear_block, 150, 118);
    lv_obj_set_flex_flow(gear_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gear_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(gear_block, 6, LV_PART_MAIN);

    make_label(gear_block, "GEAR", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *gear_box = lv_obj_create(gear_block);
    lv_obj_set_size(gear_box, 88, 88);
    lv_obj_set_style_bg_color(gear_box, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gear_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(gear_box, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(gear_box, C_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(gear_box, 6, LV_PART_MAIN);
    lv_obj_clear_flag(gear_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(gear_box);

    s_gear_num_label = lv_label_create(gear_box);
    lv_label_set_text(s_gear_num_label, "N");
    lv_obj_set_style_text_font(s_gear_num_label, DASH_FONT_GEAR, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_gear_num_label, C_WHITE, LV_PART_MAIN);
    lv_obj_center(s_gear_num_label);

    return row;
}

/* ---- data tile -------------------------------------------------------------- */

static void build_tile(lv_obj_t *parent, tile_id_t id, int col, int row)
{
    const tile_def_t *def = &TILE_DEFS[id];
    lv_color_t accent = lv_color_hex(def->accent_hex);

    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_style_bg_color(tile, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 5, LV_PART_MAIN);
    /* deliberately NO padding on the tile itself -- LV_PCT(100) children
       (the accent stripe, the background bar) resolve against the parent's
       *padded* content area even with LV_OBJ_FLAG_IGNORE_LAYOUT, so any
       padding here would inset them from the tile's true edges. Padding
       for the readable content lives on content_wrap below instead. */
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);

    /* full-tile background fill, created before the text so it paints
       behind it. Fill WIDTH tracks the live value (proportional to
       min/max) -- NOT a constant full fill; color/opacity signal the
       zone, width signals how far into that range the value actually is. */
    lv_obj_t *bar = lv_bar_create(tile);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(bar, LV_PCT(100), LV_PCT(100));
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, accent, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_20, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

     /* content wrapper -- this is where the tile's old padding moved to.
         Sits above the bar in z-order (created after it), fills
       the whole tile, and lays out label/value normally via flex. */
    lv_obj_t *content_wrap = make_plain_container(tile);
    lv_obj_set_size(content_wrap, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_left(content_wrap, 13, LV_PART_MAIN);
    lv_obj_set_style_pad_right(content_wrap, 11, LV_PART_MAIN);
    lv_obj_set_style_pad_top(content_wrap, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(content_wrap, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(content_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(content_wrap, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = make_label(content_wrap, def->name, DASH_FONT_LABEL, C_WHITE);
    lv_obj_set_style_opa(label, LV_OPA_90, LV_PART_MAIN);

    lv_obj_t *value_row = make_plain_container(content_wrap);
    lv_obj_set_size(value_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(value_row, 5, LV_PART_MAIN);
    lv_obj_clear_flag(value_row, LV_OBJ_FLAG_CLICKABLE);

    /* value text is always plain white now, so it stays readable regardless
       of the accent color washing the tile behind it (warn/crit still
       override this to amber/red in set_tile_value()) */
    lv_obj_t *value = make_label(value_row, "--", DASH_FONT_TILEVAL, C_WHITE);
    lv_obj_t *unit = make_label(value_row, def->unit, DASH_FONT_LABEL, C_LABEL_DIM);
    add_text_outline(value, DASH_FONT_TILEVAL, s_tiles[id].value_outline);

    s_tiles[id].tile  = tile;
    s_tiles[id].label = label;
    s_tiles[id].unit  = unit;
    s_tiles[id].value = value;
    s_tiles[id].bar   = bar;
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tile, system_field_long_press_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)(SYSTEM_FIELD_MODERN * 16 + id));
    add_press_feedback(tile);

}

static lv_obj_t *build_tiles_grid(lv_obj_t *parent)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    s_grid_col_dsc[0] = LV_GRID_FR(1);
    s_grid_col_dsc[1] = LV_GRID_FR(1);
    s_grid_col_dsc[2] = LV_GRID_FR(1);
    s_grid_col_dsc[3] = LV_GRID_FR(1);
    s_grid_col_dsc[4] = LV_GRID_FR(1);
    s_grid_col_dsc[5] = LV_GRID_TEMPLATE_LAST;

    s_grid_row_dsc[0] = LV_GRID_FR(1);
    s_grid_row_dsc[1] = LV_GRID_FR(1);
    s_grid_row_dsc[2] = LV_GRID_TEMPLATE_LAST;

    lv_obj_set_grid_dsc_array(grid, s_grid_col_dsc, s_grid_row_dsc);
    lv_obj_set_style_pad_column(grid, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 9, LV_PART_MAIN);

    /* row 0: ECT, IAT, AFR, TIMING, MAP     row 1: BATT, TPS, OIL, DUTY, KNOCK */
    build_tile(grid, TILE_ECT,    0, 0);
    build_tile(grid, TILE_IAT,    1, 0);
    build_tile(grid, TILE_AFR,    2, 0);
    build_tile(grid, TILE_TIMING, 3, 0);
    build_tile(grid, TILE_MAP,    4, 0);
    build_tile(grid, TILE_BATT,   0, 1);
    build_tile(grid, TILE_TPS,    1, 1);
    build_tile(grid, TILE_OIL,    2, 1);
    build_tile(grid, TILE_DUTY,   3, 1);
    build_tile(grid, TILE_KNOCK,  4, 1);

    return grid;
}

/* ---- telltale strip --------------------------------------------------------- */

static telltale_t build_check_engine_icon(lv_obj_t *parent)
{
    static lv_point_t engine_points[] = {
        {1, 6}, {6, 6}, {9, 2}, {21, 2}, {24, 6}, {30, 6},
        {30, 17}, {25, 17}, {22, 21}, {9, 21}, {6, 17}, {1, 17}, {1, 6},
    };
    lv_obj_t *icon = lv_line_create(parent);
    lv_line_set_points(icon, engine_points,
                       sizeof(engine_points) / sizeof(engine_points[0]));
    lv_obj_set_size(icon, 32, 23);
    lv_obj_set_style_line_color(icon, C_LABEL_DIM, LV_PART_MAIN);
    lv_obj_set_style_line_width(icon, 3, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(icon, true, LV_PART_MAIN);

    telltale_t t = { .dot = icon, .on_color = C_RED };
    return t;
}

static lv_coord_t s_strip_col_dsc[7];
static lv_coord_t s_strip_row_dsc[2];

static lv_obj_t *build_telltales(lv_obj_t *parent)
{
    lv_obj_t *strip = make_plain_container(parent);
    lv_obj_set_size(strip, LV_PCT(100), (lv_coord_t)(SCR_H * 0.085f));
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x0a0b0d), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(strip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(strip, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(strip, 32, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(strip, 0, LV_PART_MAIN);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    s_strip_col_dsc[0] = LV_GRID_CONTENT;
    s_strip_col_dsc[1] = LV_GRID_CONTENT;
    s_strip_col_dsc[2] = LV_GRID_FR(1);
    s_strip_col_dsc[3] = LV_GRID_CONTENT;
    s_strip_col_dsc[4] = LV_GRID_CONTENT;
    s_strip_col_dsc[5] = LV_GRID_CONTENT;
    s_strip_col_dsc[6] = LV_GRID_TEMPLATE_LAST;
    s_strip_row_dsc[0] = LV_GRID_FR(1);
    s_strip_row_dsc[1] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(strip, s_strip_col_dsc, s_strip_row_dsc);
    lv_obj_set_style_pad_column(strip, 20, LV_PART_MAIN);

    /* ---- odometer, bottom-left ---- */
    lv_obj_t *odo_block = make_plain_container(strip);
    lv_obj_set_size(odo_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(odo_block, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(odo_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(odo_block, 1, LV_PART_MAIN);
    lv_obj_add_flag(odo_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(odo_block, odo_cycle_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(odo_block, odo_reset_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    add_press_feedback(odo_block);

    s_odo_caption_label = make_label(odo_block, "ODO", DASH_FONT_LABEL, C_LABEL_DIM);
    s_odo_val_label = make_label(odo_block, "0 MI", DASH_FONT_LABEL14, C_WHITE);

    lv_obj_t *achievement_block = make_plain_container(strip);
    lv_obj_set_size(achievement_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(achievement_block, LV_GRID_ALIGN_CENTER, 1, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(achievement_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(achievement_block, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(achievement_block, 5, LV_PART_MAIN);
    static lv_point_t achievement_star_points[] = {
        {8, 0}, {10, 5}, {16, 6}, {12, 10}, {13, 16},
        {8, 13}, {3, 16}, {4, 10}, {0, 6}, {6, 5}, {8, 0},
    };
    lv_obj_t *achievement_star = lv_line_create(achievement_block);
    lv_line_set_points(achievement_star, achievement_star_points,
                       sizeof(achievement_star_points) / sizeof(achievement_star_points[0]));
    lv_obj_set_size(achievement_star, 17, 17);
    lv_obj_set_style_line_color(achievement_star, C_AMBER, LV_PART_MAIN);
    lv_obj_set_style_line_width(achievement_star, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(achievement_star, true, LV_PART_MAIN);
    s_achievement_count_label = make_label(achievement_block, "0/10", DASH_FONT_LABEL14, C_AMBER);
    odo_tiles_refresh_display();

    s_tell_cel = build_check_engine_icon(strip);
    lv_obj_set_grid_cell(s_tell_cel.dot, LV_GRID_ALIGN_CENTER, 3, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);

    /* ---- fuel level bar, bottom-right ---- */
    lv_obj_t *fuel_block = make_plain_container(strip);
    lv_obj_set_size(fuel_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(fuel_block, LV_GRID_ALIGN_END, 5, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(fuel_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fuel_block, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fuel_block, 8, LV_PART_MAIN);

    s_fuel_bar = lv_bar_create(fuel_block);
    lv_obj_set_size(s_fuel_bar, 70, 9);
    lv_obj_set_style_radius(s_fuel_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fuel_bar, C_SEG_OFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_fuel_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_fuel_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_fuel_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_fuel_bar, lv_color_hex(0x2dd4bf), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_fuel_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(s_fuel_bar, 0, 100);
    lv_bar_set_value(s_fuel_bar, 0, LV_ANIM_OFF);

    lv_obj_t *fuel_text = make_plain_container(fuel_block);
    lv_obj_set_size(fuel_text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(fuel_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(fuel_text, 1, LV_PART_MAIN);
    make_label(fuel_text, "FUEL", DASH_FONT_LABEL, C_LABEL_DIM);
    s_fuel_val_label = make_label(fuel_text, "0", DASH_FONT_LABEL14, C_WHITE);

    return strip;
}

/* ================= PUBLIC API =============================================== */

/* ================= SETTINGS MENU ============================================ */

static void ui_opacity_anim_cb(void *object, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, LV_PART_MAIN);
}

static void ui_hide_after_anim_cb(lv_anim_t *animation)
{
    lv_obj_t *object = (lv_obj_t *)animation->var;
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(object, LV_OPA_COVER, LV_PART_MAIN);
}

static void ui_delete_after_anim_cb(lv_anim_t *animation)
{
    lv_obj_del_async((lv_obj_t *)animation->var);
}

static void ui_fade(lv_obj_t *object, lv_opa_t from, lv_opa_t to, uint32_t duration,
                    lv_anim_ready_cb_t ready_cb)
{
    lv_anim_del(object, ui_opacity_anim_cb);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, ui_opacity_anim_cb);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_time(&animation, duration);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    if (ready_cb) lv_anim_set_ready_cb(&animation, ready_cb);
    lv_anim_start(&animation);
}

static void settings_show_page(lv_obj_t *page)
{
    lv_obj_add_flag(s_page_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_theme, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_logs_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_peaks, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_logs, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_update, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_config, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_units, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_ecu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_theme_resets, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_display, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_shortcuts, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_readme, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_odometer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_fuel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_engine_limits, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_achievements, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_page_contact, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    s_settings_current_page = page;
    if (s_settings_nav_label) {
        lv_label_set_text(s_settings_nav_label,
                          page == s_page_main ? LV_SYMBOL_CLOSE : LV_SYMBOL_LEFT);
    }
    ui_fade(page, LV_OPA_TRANSP, LV_OPA_COVER, 140, NULL);
}

static void settings_set_update_background_paused(bool paused);
static void settings_set_render_paused(bool paused);

static void quick_brightness_close(void)
{
    lv_obj_t *overlay = s_quick_brightness_overlay;
    s_quick_brightness_overlay = NULL;
    if (overlay) {
        ui_fade(overlay, lv_obj_get_style_opa(overlay, LV_PART_MAIN), LV_OPA_TRANSP,
                100, ui_delete_after_anim_cb);
    }
}

static void quick_brightness_close_cb(lv_event_t *event)
{
    (void)event;
    quick_brightness_close();
}

static void quick_brightness_select_cb(lv_event_t *event)
{
    int brightness = (int)(intptr_t)lv_event_get_user_data(event);
    bsp_display_brightness_set(brightness);
    dash_config_set_brightness(brightness);
    if (s_brightness_slider) lv_slider_set_value(s_brightness_slider, brightness, LV_ANIM_OFF);
    if (s_brightness_value_label) {
        char value[8];
        snprintf(value, sizeof(value), "%d%%", brightness);
        lv_label_set_text(s_brightness_value_label, value);
    }
    quick_brightness_close();
}

static void quick_brightness_open(void)
{
    if (s_quick_brightness_overlay) return;
    static const char *const names[] = {"Day", "Dim", "Night"};
    static const int values[] = {100, 55, 25};

    s_quick_brightness_overlay = lv_obj_create(s_cluster);
    lv_obj_add_flag(s_quick_brightness_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_quick_brightness_overlay, SCR_W, SCR_H);
    lv_obj_set_pos(s_quick_brightness_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_quick_brightness_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_quick_brightness_overlay, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_quick_brightness_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_quick_brightness_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_quick_brightness_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_quick_brightness_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_quick_brightness_overlay, quick_brightness_close_cb,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_quick_brightness_overlay);
    lv_obj_set_size(panel, 620, 210);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 18, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 14, LV_PART_MAIN);
    make_label(panel, "QUICK BRIGHTNESS", DASH_FONT_LABEL14, C_WHITE);

    lv_obj_t *choices = make_plain_container(panel);
    lv_obj_set_size(choices, LV_PCT(100), 124);
    lv_obj_set_flex_flow(choices, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(choices, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    int current = dash_config_get_brightness();
    for (size_t index = 0; index < 3; ++index) {
        bool selected = current == values[index];
        lv_obj_t *button = lv_obj_create(choices);
        lv_obj_set_size(button, 180, 112);
        lv_obj_set_style_bg_color(button, selected ? C_RED_DEEP : C_VOID, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, selected ? 3 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, selected ? C_RED : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, quick_brightness_select_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)values[index]);
        add_press_feedback(button);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(button, 8, LV_PART_MAIN);
        make_label(button, names[index], DASH_FONT_LABEL14, C_WHITE);
        char value[8];
        snprintf(value, sizeof(value), "%d%%", values[index]);
        make_label(button, value, DASH_FONT_LABEL14, selected ? C_RED : C_LABEL);
    }
    lv_obj_move_foreground(s_quick_brightness_overlay);
    ui_fade(s_quick_brightness_overlay, LV_OPA_TRANSP, LV_OPA_COVER, 120, NULL);
}

static void settings_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        quick_brightness_open();
        return;
    }
    settings_set_render_paused(true);
    settings_show_page(s_page_main);
    lv_obj_clear_flag(s_settings_overlay, LV_OBJ_FLAG_HIDDEN);
    ui_fade(s_settings_overlay, LV_OPA_TRANSP, LV_OPA_COVER, 180, NULL);
}

static void settings_set_update_background_paused(bool paused)
{
    if (s_update_background_paused == paused) {
        return;
    }

    s_update_background_paused = paused;
    dashboard_runtime_set_ota_mode(paused);
}

static void settings_set_render_paused(bool paused)
{
    if (s_render_paused == paused) {
        return;
    }

    s_render_paused = paused;
    dashboard_runtime_set_render_paused(paused);
}

static void settings_close_cb(lv_event_t *e)
{
    (void)e;
    if (!ota_update_is_running() && !ota_update_is_starting()) {
        settings_set_update_background_paused(false);
    }
    settings_set_render_paused(false);
    ui_fade(s_settings_overlay, lv_obj_get_style_opa(s_settings_overlay, LV_PART_MAIN),
            LV_OPA_TRANSP, 140, ui_hide_after_anim_cb);
}

static void theme_stage_selection(int idx);
static void settings_open_theme_cb(lv_event_t *e)
{
    (void)e;
    theme_stage_selection(s_active_theme);
    settings_show_page(s_page_theme);
}
static void settings_open_logs_menu_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_logs_menu);
}
static void settings_diagnostics_refresh(void);
static void settings_peaks_refresh(void);
static void settings_logs_refresh(void);
static void settings_open_info_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_info);
    settings_diagnostics_refresh();
}
static void settings_open_peaks_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_peaks);
    lv_obj_scroll_to_y(s_peaks_scroll, 0, LV_ANIM_OFF);
    settings_peaks_refresh();
}
static void settings_open_logs_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_logs);
    settings_logs_refresh();
}
static void settings_open_ecu_cb(lv_event_t *e)
{
    (void)e;
    cfg_protocol_tiles_refresh();
    settings_show_page(s_page_ecu);
}
static void settings_open_theme_resets_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_theme_resets);
}
static void settings_open_units_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_units); }
static void settings_open_display_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_display); }
static void settings_open_shortcuts_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_shortcuts); }
static void settings_open_readme_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_readme); }
static void settings_open_odometer_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_odometer); }
static void settings_open_fuel_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_fuel); }
static void settings_open_contact_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_contact); }
static void settings_achievements_refresh(void)
{
    char text[48];
    snprintf(text, sizeof(text), "%u OF %u UNLOCKED", achievement_system_get_count(),
             ACHIEVEMENT_SYSTEM_TOTAL);
    lv_label_set_text(s_achievement_summary_label, text);

    for (uint8_t index = 0; index < ACHIEVEMENT_SYSTEM_TOTAL; ++index) {
        achievement_system_entry_t entry;
        if (!achievement_system_get_entry(index, &entry)) continue;
        snprintf(text, sizeof(text), "Hits  %lu", (unsigned long)entry.hit_count);
        lv_label_set_text(s_achievement_hit_labels[index], text);
        if (!entry.record_valid) {
            lv_label_set_text(s_achievement_record_labels[index], "Record  --");
        } else if (!strcmp(entry.unit, "RPM") || !strcmp(entry.unit, "F")) {
            snprintf(text, sizeof(text), "%s  %.0f %s",
                     entry.lower_is_better ? "Low" : "Peak",
                     (double)entry.record_value, entry.unit);
            lv_label_set_text(s_achievement_record_labels[index], text);
        } else {
            snprintf(text, sizeof(text), "%s  %.1f %s",
                     entry.lower_is_better ? "Low" : "Peak",
                     (double)entry.record_value, entry.unit);
            lv_label_set_text(s_achievement_record_labels[index], text);
        }
    }
}

static void settings_open_achievements_cb(lv_event_t *e)
{
    (void)e;
    settings_achievements_refresh();
    settings_show_page(s_page_achievements);
}

static void settings_open_engine_limits_cb(lv_event_t *e)
{
    (void)e;
    shift_settings_refresh();
    settings_show_page(s_page_engine_limits);
}

static void settings_open_config_cb(lv_event_t *e)
{
    (void)e;
    /* reset the pending (not-yet-saved) fields to whatever's currently
       active every time the page is opened */
    s_cfg_odo_pending_miles = odometer_get_miles();
    s_cfg_pending_vtec_rpm = dash_config_get_vtec_rpm();
    s_cfg_pending_redline_rpm = dash_config_get_redline_rpm();

    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f mi", s_cfg_odo_pending_miles);
    lv_label_set_text(s_cfg_odo_label, buf);

    snprintf(buf, sizeof(buf), "%.0f mi", odometer_get_trip_a_miles());
    lv_label_set_text(s_cfg_trip_a_label, buf);
    snprintf(buf, sizeof(buf), "%.0f mi", odometer_get_trip_b_miles());
    lv_label_set_text(s_cfg_trip_b_label, buf);

    snprintf(buf, sizeof(buf), "%d RPM", s_cfg_pending_vtec_rpm);
    lv_label_set_text(s_cfg_vtec_label, buf);
    if (s_cfg_vtec_slider) lv_slider_set_value(s_cfg_vtec_slider, s_cfg_pending_vtec_rpm, LV_ANIM_OFF);

    snprintf(buf, sizeof(buf), "%d RPM", s_cfg_pending_redline_rpm);
    lv_label_set_text(s_cfg_redline_label, buf);
    if (s_cfg_redline_slider) lv_slider_set_value(s_cfg_redline_slider, s_cfg_pending_redline_rpm, LV_ANIM_OFF);

    for (int index = 0; index < UNIT_SETTING_COUNT; ++index) {
        bool enabled = cfg_unit_setting_get((unit_setting_t)index);
        if (s_cfg_unit_switches[index]) {
            if (enabled) lv_obj_add_state(s_cfg_unit_switches[index], LV_STATE_CHECKED);
            else lv_obj_clear_state(s_cfg_unit_switches[index], LV_STATE_CHECKED);
        }
        if (s_cfg_unit_values[index]) {
            lv_label_set_text(s_cfg_unit_values[index],
                              cfg_unit_setting_value((unit_setting_t)index, enabled));
        }
    }

    if (s_brightness_slider) {
        lv_slider_set_value(s_brightness_slider, dash_config_get_brightness(), LV_ANIM_OFF);
    }
    if (s_brightness_value_label) {
        char brightness_buf[24];
        snprintf(brightness_buf, sizeof(brightness_buf), "%d%%", dash_config_get_brightness());
        lv_label_set_text(s_brightness_value_label, brightness_buf);
    }
    if (s_cfg_sim_button_switch) {
        if (dash_config_get_show_sim_button()) lv_obj_add_state(s_cfg_sim_button_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cfg_sim_button_switch, LV_STATE_CHECKED);
    }
    if (s_cfg_smoothing_dropdown) {
        lv_dropdown_set_selected(s_cfg_smoothing_dropdown, dash_config_get_smoothing_strength());
    }
    if (s_cfg_auto_record_switch) {
        if (dash_config_get_auto_record()) lv_obj_add_state(s_cfg_auto_record_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cfg_auto_record_switch, LV_STATE_CHECKED);
    }
    for (int i = 0; i < DASH_CONFIG_THRESHOLD_COUNT; ++i) {
        int value = dash_config_get_threshold_tenths((dash_config_threshold_t)i);
        if (s_cfg_threshold_sliders[i]) lv_slider_set_value(s_cfg_threshold_sliders[i], value, LV_ANIM_OFF);
        if (s_cfg_threshold_labels[i]) {
            char threshold_buf[24];
            if (THRESHOLD_UI[i].step_tenths == 1 || THRESHOLD_UI[i].step_tenths == 5) {
                snprintf(threshold_buf, sizeof(threshold_buf), "%.1f%s", value / 10.0f, THRESHOLD_UI[i].unit);
            } else {
                snprintf(threshold_buf, sizeof(threshold_buf), "%d%s", value / 10, THRESHOLD_UI[i].unit);
            }
            lv_label_set_text(s_cfg_threshold_labels[i], threshold_buf);
        }
    }

    settings_show_page(s_page_config);
}

static bool cfg_unit_setting_get(unit_setting_t setting)
{
    if (setting == UNIT_SETTING_SPEED) return dash_config_get_speed_kph();
    if (setting == UNIT_SETTING_TEMPERATURE) return dash_config_get_temperature_celsius();
    if (setting == UNIT_SETTING_PRESSURE) return dash_config_get_pressure_kpa();
    return dash_config_get_distance_km();
}

static const char *cfg_unit_setting_value(unit_setting_t setting, bool enabled)
{
    if (setting == UNIT_SETTING_SPEED) return enabled ? "KPH" : "MPH";
    if (setting == UNIT_SETTING_TEMPERATURE) return enabled ? "Celsius" : "Fahrenheit";
    if (setting == UNIT_SETTING_PRESSURE) return enabled ? "kPa" : "PSI";
    return enabled ? "Kilometres" : "Miles";
}

static void cfg_unit_switch_cb(lv_event_t *e)
{
    unit_setting_t setting = (unit_setting_t)(intptr_t)lv_event_get_user_data(e);
    bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (setting == UNIT_SETTING_SPEED) dash_config_set_speed_kph(enabled);
    else if (setting == UNIT_SETTING_TEMPERATURE) dash_config_set_temperature_celsius(enabled);
    else if (setting == UNIT_SETTING_PRESSURE) dash_config_set_pressure_kpa(enabled);
    else dash_config_set_distance_km(enabled);
    lv_label_set_text(s_cfg_unit_values[setting], cfg_unit_setting_value(setting, enabled));
    lv_obj_clear_flag(s_cfg_restart_note, LV_OBJ_FLAG_HIDDEN);
}

static void cfg_protocol_tiles_refresh(void)
{

    const char *current = dash_config_get_can_protocol();
    size_t selected = 0;
    for (size_t index = 0; index < CFG_PROTOCOL_COUNT; ++index) {
        bool active = !strcmp(CFG_PROTOCOL_NAMES[index], current);
        if (active) selected = index;
        if (!s_cfg_protocol_tiles[index]) continue;
        lv_obj_set_style_border_width(s_cfg_protocol_tiles[index], active ? 3 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_cfg_protocol_tiles[index], active ? C_RED : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_cfg_protocol_tiles[index], active ? LV_OPA_30 : LV_OPA_COVER, LV_PART_MAIN);
    }
    if (s_cfg_protocol_status_label) {
        char status[48];
        snprintf(status, sizeof(status), "Selected: %s", CFG_PROTOCOL_LABELS[selected]);
        lv_label_set_text(s_cfg_protocol_status_label, status);
    }
}

static void cfg_protocol_tile_cb(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= CFG_PROTOCOL_COUNT) return;
    dash_config_set_can_protocol(CFG_PROTOCOL_NAMES[index]);
    cfg_protocol_tiles_refresh();
    lv_obj_clear_flag(s_ecu_restart_note, LV_OBJ_FLAG_HIDDEN);
}

static void cfg_sim_button_switch_cb(lv_event_t *e)
{
    bool show = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    dash_config_set_show_sim_button(show);
    sim_buttons_apply_visibility();
}

static void cfg_smoothing_dropdown_cb(lv_event_t *e)
{
    dash_config_set_smoothing_strength((dash_config_smoothing_strength_t)
                                       lv_dropdown_get_selected(lv_event_get_target(e)));
}

static void cfg_fuel_live_refresh(void)
{
    if (!s_cfg_fuel_live_label) return;
    int input = dash_config_get_fuel_analog_input();
    float voltage = can_data.analog_inputs[input];
    char value[32];
    snprintf(value, sizeof(value), "Analog %d  %.2f V  %.0f%%", input, voltage,
             dash_config_fuel_percent(voltage));
    lv_label_set_text(s_cfg_fuel_live_label, value);
}

static void cfg_fuel_input_cb(lv_event_t *e)
{
    dash_config_set_fuel_analog_input(lv_dropdown_get_selected(lv_event_get_target(e)));
    cfg_fuel_live_refresh();
}

static void cfg_fuel_voltage_changed_cb(lv_event_t *e)
{
    int endpoint = (int)(intptr_t)lv_event_get_user_data(e);
    int millivolts = lv_slider_get_value(lv_event_get_target(e));
    char value[12];
    snprintf(value, sizeof(value), "%.2f V", (double)millivolts / 1000.0);
    lv_label_set_text(s_cfg_fuel_voltage_labels[endpoint], value);
}

static void cfg_fuel_voltage_released_cb(lv_event_t *e)
{
    int endpoint = (int)(intptr_t)lv_event_get_user_data(e);
    int millivolts = lv_slider_get_value(lv_event_get_target(e));
    if (endpoint == 0) dash_config_set_fuel_empty_mv(millivolts);
    else dash_config_set_fuel_full_mv(millivolts);
    cfg_fuel_live_refresh();
}

static void cfg_fuel_live_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    cfg_fuel_live_refresh();
}

static void cfg_boot_logo_dropdown_cb(lv_event_t *e)
{
    bool custom = lv_dropdown_get_selected(lv_event_get_target(e)) > 0;
    if (custom) lv_obj_clear_state(s_cfg_boot_logo_delete_button, LV_STATE_DISABLED);
    else lv_obj_add_state(s_cfg_boot_logo_delete_button, LV_STATE_DISABLED);
}

static void cfg_boot_logo_set_cb(lv_event_t *e)
{
    (void)e;
    int selected = (int)lv_dropdown_get_selected(s_cfg_boot_logo_dropdown) - 1;
    esp_err_t result = boot_logo_storage_select(selected);
    lv_label_set_text(s_cfg_boot_logo_status,
                      result == ESP_OK ? "Selected. Shown on next startup." : "Could not save selection.");
}

static void cfg_boot_logo_delete_cb(lv_event_t *e)
{
    (void)e;
    int selected = (int)lv_dropdown_get_selected(s_cfg_boot_logo_dropdown) - 1;
    if (selected < 0) return;
    esp_err_t result = boot_logo_storage_delete((size_t)selected);
    lv_label_set_text(s_cfg_boot_logo_status,
                      result == ESP_OK ? "Boot logo deleted." : "Could not delete boot logo.");
    if (result != ESP_OK) return;

    char options[BOOT_LOGO_STORAGE_MAX_LOGOS * BOOT_LOGO_STORAGE_NAME_MAX + 32] = "Built-in MackoDash";
    for (size_t index = 0; index < boot_logo_storage_get_count(); ++index) {
        const boot_logo_storage_entry_t *logo = boot_logo_storage_get_entry(index);
        strlcat(options, "\n", sizeof(options));
        strlcat(options, logo->display_name, sizeof(options));
    }
    lv_dropdown_set_options(s_cfg_boot_logo_dropdown, options);
    lv_dropdown_set_selected(s_cfg_boot_logo_dropdown, 0);
    lv_obj_add_state(s_cfg_boot_logo_delete_button, LV_STATE_DISABLED);
}

static void cfg_language_restart_cb(lv_timer_t *timer)
{
    (void)timer;
    esp_restart();
}

static void cfg_language_dropdown_cb(lv_event_t *e)
{
    dash_config_language_t language = (dash_config_language_t)
                                      lv_dropdown_get_selected(lv_event_get_target(e));
    if (language == dash_config_get_language()) return;
    dash_config_set_language(language);
    lv_timer_t *restart_timer = lv_timer_create(cfg_language_restart_cb, 350, NULL);
    lv_timer_set_repeat_count(restart_timer, 1);
}

static void cfg_auto_record_switch_cb(lv_event_t *e)
{
    bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    dash_config_set_auto_record(enabled);
}

static void cfg_log_name_dropdown_cb(lv_event_t *e)
{
    dash_config_set_log_name((dash_config_log_name_t)
                             lv_dropdown_get_selected(lv_event_get_target(e)));
}

static void cfg_shift_enable_cb(lv_event_t *e)
{
    dash_config_set_shift_light_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    s_shift_light_last_theme = -1;
}

static void cfg_shift_stage_slider_cb(lv_event_t *e)
{
    int stage = (int)(intptr_t)lv_event_get_user_data(e);
    int rpm = lv_slider_get_value(lv_event_get_target(e));
    rpm = ((rpm + 50) / 100) * 100;
    lv_slider_set_value(lv_event_get_target(e), rpm, LV_ANIM_OFF);
    char text[20];
    snprintf(text, sizeof(text), "%d RPM", rpm);
    lv_label_set_text(s_cfg_shift_stage_labels[stage], text);
}

static void cfg_shift_stage_released_cb(lv_event_t *e)
{
    int stage = (int)(intptr_t)lv_event_get_user_data(e);
    dash_config_set_shift_stage_rpm(stage, lv_slider_get_value(lv_event_get_target(e)));
    shift_settings_refresh();
    s_shift_light_last_theme = -1;
}

static void cfg_shift_color_cb(lv_event_t *e)
{
    int stage = (int)(intptr_t)lv_event_get_user_data(e);
    dash_config_set_shift_stage_color(stage,
        (dash_config_shift_color_t)lv_dropdown_get_selected(lv_event_get_target(e)));
    s_shift_light_last_colors[stage] = -1;
}

static void cfg_shift_brightness_cb(lv_event_t *e)
{
    int brightness = lv_slider_get_value(lv_event_get_target(e));
    brightness = ((brightness + 2) / 5) * 5;
    lv_slider_set_value(lv_event_get_target(e), brightness, LV_ANIM_OFF);
    char text[12];
    snprintf(text, sizeof(text), "%d%%", brightness);
    lv_label_set_text(s_cfg_shift_brightness_label, text);
}

static void cfg_shift_brightness_released_cb(lv_event_t *e)
{
    dash_config_set_shift_light_brightness(lv_slider_get_value(lv_event_get_target(e)));
    shift_settings_refresh();
    s_shift_light_last_brightness = -1;
}

static void cfg_shift_flash_cb(lv_event_t *e)
{
    dash_config_set_shift_redline_flash(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    s_shift_light_last_theme = -1;
}

static void shift_settings_refresh(void)
{
    if (s_cfg_shift_enable_switch) {
        if (dash_config_get_shift_light_enabled()) lv_obj_add_state(s_cfg_shift_enable_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cfg_shift_enable_switch, LV_STATE_CHECKED);
    }
    for (int stage = 0; stage < DASH_CONFIG_SHIFT_STAGE_COUNT; ++stage) {
        int rpm = dash_config_get_shift_stage_rpm(stage);
        if (s_cfg_shift_stage_sliders[stage]) lv_slider_set_value(s_cfg_shift_stage_sliders[stage], rpm, LV_ANIM_OFF);
        if (s_cfg_shift_stage_labels[stage]) {
            char text[20];
            snprintf(text, sizeof(text), "%d RPM", rpm);
            lv_label_set_text(s_cfg_shift_stage_labels[stage], text);
        }
        if (s_cfg_shift_color_dropdowns[stage]) {
            lv_dropdown_set_selected(s_cfg_shift_color_dropdowns[stage],
                                     dash_config_get_shift_stage_color(stage));
        }
    }
    int brightness = dash_config_get_shift_light_brightness();
    if (s_cfg_shift_brightness_slider) lv_slider_set_value(s_cfg_shift_brightness_slider, brightness, LV_ANIM_OFF);
    if (s_cfg_shift_brightness_label) {
        char text[12];
        snprintf(text, sizeof(text), "%d%%", brightness);
        lv_label_set_text(s_cfg_shift_brightness_label, text);
    }
    if (s_cfg_shift_flash_switch) {
        if (dash_config_get_shift_redline_flash()) lv_obj_add_state(s_cfg_shift_flash_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cfg_shift_flash_switch, LV_STATE_CHECKED);
    }
}

static void cfg_reboot_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGW("honda_dash_ui", "Config reboot requested");
    bsp_display_backlight_off();
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_restart();
}

static void cfg_odo_step_cb(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    s_cfg_odo_pending_miles += step;
    if (s_cfg_odo_pending_miles < 0.0) {
        s_cfg_odo_pending_miles = 0.0;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f mi", s_cfg_odo_pending_miles);
    lv_label_set_text(s_cfg_odo_label, buf);
}

static void cfg_odo_save_cb(lv_event_t *e)
{
    (void)e;
    dash_config_calibrate_odometer_miles(s_cfg_odo_pending_miles);
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f mi (saved)", s_cfg_odo_pending_miles);
    lv_label_set_text(s_cfg_odo_label, buf);
}

static void trip_reset_modal_close(void)
{
    if (s_trip_reset_modal) lv_obj_del_async(s_trip_reset_modal);
    s_trip_reset_modal = NULL;
    s_trip_reset_pending = 0;
}

static void trip_reset_cancel_cb(lv_event_t *event)
{
    (void)event;
    trip_reset_modal_close();
}

static void trip_reset_confirm_cb(lv_event_t *event)
{
    (void)event;
    int trip = s_trip_reset_pending;
    if (trip == 1) odometer_reset_trip_a();
    else if (trip == 2) odometer_reset_trip_b();
    odo_tiles_refresh_display();
    trip_reset_modal_close();
}

static lv_obj_t *trip_reset_modal_button(lv_obj_t *parent, const char *text,
                                         lv_color_t background, lv_color_t foreground,
                                         lv_event_cb_t callback)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_flex_grow(button, 1);
    lv_obj_set_height(button, 48);
    lv_obj_set_style_bg_color(button, background, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    add_press_feedback(button);
    lv_obj_t *label = make_label(button, text, DASH_FONT_LABEL14, foreground);
    lv_obj_center(label);
    return button;
}

static void trip_reset_request(int trip)
{
    if (trip < 1 || trip > 2 || s_trip_reset_modal) return;
    s_trip_reset_pending = trip;
    s_trip_reset_modal = lv_obj_create(s_cluster);
    lv_obj_add_flag(s_trip_reset_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_trip_reset_modal, SCR_W, SCR_H);
    lv_obj_set_pos(s_trip_reset_modal, 0, 0);
    lv_obj_set_style_bg_color(s_trip_reset_modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_trip_reset_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_trip_reset_modal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_trip_reset_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_trip_reset_modal);

    lv_obj_t *panel = lv_obj_create(s_trip_reset_modal);
    lv_obj_set_size(panel, 430, 210);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 20, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    char title[24];
    snprintf(title, sizeof(title), "RESET TRIP %c?", trip == 1 ? 'A' : 'B');
    make_label(panel, title, DASH_FONT_LABEL14, C_WHITE);
    make_label(panel, "This cannot be undone.", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *buttons = make_plain_container(panel);
    lv_obj_set_size(buttons, LV_PCT(100), 48);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 12, LV_PART_MAIN);
    trip_reset_modal_button(buttons, "Cancel", C_VOID, C_LABEL, trip_reset_cancel_cb);
    trip_reset_modal_button(buttons, "Reset", C_RED, C_WHITE, trip_reset_confirm_cb);
}

static void cfg_trip_a_reset_cb(lv_event_t *e)
{
    (void)e;
    trip_reset_request(1);
}

static void cfg_trip_b_reset_cb(lv_event_t *e)
{
    (void)e;
    trip_reset_request(2);
}

static void cfg_vtec_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int rpm = lv_slider_get_value(slider);
    dash_config_set_vtec_rpm(rpm);
    s_cfg_pending_vtec_rpm = dash_config_get_vtec_rpm(); /* pick up any clamping */
    char buf[48];
    snprintf(buf, sizeof(buf), "%d RPM", s_cfg_pending_vtec_rpm);
    lv_label_set_text(s_cfg_vtec_label, buf);
    lv_obj_clear_flag(s_cfg_restart_note, LV_OBJ_FLAG_HIDDEN);
}

static void cfg_redline_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int rpm = lv_slider_get_value(slider);
    dash_config_set_redline_rpm(rpm);
    s_cfg_pending_redline_rpm = dash_config_get_redline_rpm(); /* pick up any clamping */
    char buf[48];
    snprintf(buf, sizeof(buf), "%d RPM", s_cfg_pending_redline_rpm);
    lv_label_set_text(s_cfg_redline_label, buf);
    lv_obj_clear_flag(s_cfg_restart_note, LV_OBJ_FLAG_HIDDEN);
}

static void cfg_threshold_slider_cb(lv_event_t *e)
{
    int threshold = (int)(intptr_t)lv_event_get_user_data(e);
    int value = lv_slider_get_value(lv_event_get_target(e));
    int step = THRESHOLD_UI[threshold].step_tenths;
    value = ((value + step / 2) / step) * step;
    lv_slider_set_value(lv_event_get_target(e), value, LV_ANIM_OFF);

    char buf[24];
    if (step == 1 || step == 5) {
        snprintf(buf, sizeof(buf), "%.1f%s", value / 10.0f, THRESHOLD_UI[threshold].unit);
    } else {
        snprintf(buf, sizeof(buf), "%d%s", value / 10, THRESHOLD_UI[threshold].unit);
    }
    lv_label_set_text(s_cfg_threshold_labels[threshold], buf);
}

static void cfg_threshold_slider_released_cb(lv_event_t *e)
{
    dash_config_threshold_t threshold = (dash_config_threshold_t)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    dash_config_set_threshold_tenths(threshold, lv_slider_get_value(slider));

    int value = dash_config_get_threshold_tenths(threshold);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    char buf[24];
    if (THRESHOLD_UI[threshold].step_tenths == 1 || THRESHOLD_UI[threshold].step_tenths == 5) {
        snprintf(buf, sizeof(buf), "%.1f%s", value / 10.0f, THRESHOLD_UI[threshold].unit);
    } else {
        snprintf(buf, sizeof(buf), "%d%s", value / 10, THRESHOLD_UI[threshold].unit);
    }
    lv_label_set_text(s_cfg_threshold_labels[threshold], buf);
}

static void cfg_factory_reset_cb(lv_event_t *e)
{
    (void)e;
    dash_config_factory_reset();
    for (int index = 0; index < UNIT_SETTING_COUNT; ++index) {
        if (s_cfg_unit_switches[index]) lv_obj_clear_state(s_cfg_unit_switches[index], LV_STATE_CHECKED);
        if (s_cfg_unit_values[index]) {
            lv_label_set_text(s_cfg_unit_values[index],
                              cfg_unit_setting_value((unit_setting_t)index, false));
        }
    }
    cfg_protocol_tiles_refresh();
    s_cfg_pending_vtec_rpm = dash_config_get_vtec_rpm();
    s_cfg_pending_redline_rpm = dash_config_get_redline_rpm();
    if (s_cfg_vtec_slider) lv_slider_set_value(s_cfg_vtec_slider, s_cfg_pending_vtec_rpm, LV_ANIM_OFF);
    if (s_cfg_redline_slider) lv_slider_set_value(s_cfg_redline_slider, s_cfg_pending_redline_rpm, LV_ANIM_OFF);
    if (s_brightness_slider) lv_slider_set_value(s_brightness_slider, dash_config_get_brightness(), LV_ANIM_OFF);
    if (s_brightness_value_label) lv_label_set_text(s_brightness_value_label, "95%");
    if (s_cfg_sim_button_switch) lv_obj_clear_state(s_cfg_sim_button_switch, LV_STATE_CHECKED);
    if (s_cfg_smoothing_dropdown) {
        lv_dropdown_set_selected(s_cfg_smoothing_dropdown, DASH_CONFIG_SMOOTHING_OFF);
    }
    if (s_cfg_auto_record_switch) lv_obj_clear_state(s_cfg_auto_record_switch, LV_STATE_CHECKED);
    if (s_cfg_log_name_dropdown) {
        lv_dropdown_set_selected(s_cfg_log_name_dropdown, DASH_CONFIG_LOG_NAME_GENERAL);
    }
    shift_settings_refresh();
    for (int slot = 0; slot < DASH_CONFIG_HAL_FIELD_COUNT; ++slot) {
        hal_refresh_field_identity(slot);
        s_hal_last_field_text[slot][0] = '\0';
    }
    for (int slot = 0; slot < DASH_CONFIG_MODERN_FIELD_COUNT; ++slot) {
        system_field_refresh_identity(SYSTEM_FIELD_MODERN, slot);
    }
    for (int slot = 0; slot < DASH_CONFIG_RACE_FIELD_COUNT; ++slot) {
        system_field_refresh_identity(SYSTEM_FIELD_RACE, slot);
    }
    for (int slot = 0; slot < DASH_CONFIG_ENDURANCE_FIELD_COUNT; ++slot) {
        system_field_refresh_identity(SYSTEM_FIELD_ENDURANCE, slot);
    }
    for (int slot = 0; slot < DASH_CONFIG_TOURING_FIELD_COUNT; ++slot) {
        system_field_refresh_identity(SYSTEM_FIELD_TOURING, slot);
    }
    for (int i = 0; i < DASH_CONFIG_THRESHOLD_COUNT; ++i) {
        int value = dash_config_get_threshold_tenths((dash_config_threshold_t)i);
        if (s_cfg_threshold_sliders[i]) lv_slider_set_value(s_cfg_threshold_sliders[i], value, LV_ANIM_OFF);
        if (s_cfg_threshold_labels[i]) {
            char threshold_buf[24];
            if (THRESHOLD_UI[i].step_tenths == 1 || THRESHOLD_UI[i].step_tenths == 5) {
                snprintf(threshold_buf, sizeof(threshold_buf), "%.1f%s", value / 10.0f, THRESHOLD_UI[i].unit);
            } else {
                snprintf(threshold_buf, sizeof(threshold_buf), "%d%s", value / 10, THRESHOLD_UI[i].unit);
            }
            lv_label_set_text(s_cfg_threshold_labels[i], threshold_buf);
        }
    }
    bsp_display_brightness_set(dash_config_get_brightness());
    sim_buttons_apply_visibility();
    char buf[48];
    snprintf(buf, sizeof(buf), "%d RPM", s_cfg_pending_vtec_rpm);
    lv_label_set_text(s_cfg_vtec_label, buf);
    snprintf(buf, sizeof(buf), "%d RPM", s_cfg_pending_redline_rpm);
    lv_label_set_text(s_cfg_redline_label, buf);
    lv_obj_clear_flag(s_cfg_restart_note, LV_OBJ_FLAG_HIDDEN);
}

static void factory_reset_modal_close(void)
{
    if (s_factory_reset_modal) lv_obj_del_async(s_factory_reset_modal);
    s_factory_reset_modal = NULL;
}

static void cfg_factory_reset_cancel_cb(lv_event_t *e)
{
    (void)e;
    factory_reset_modal_close();
}

static void cfg_factory_reset_confirm_cb(lv_event_t *e)
{
    factory_reset_modal_close();
    cfg_factory_reset_cb(e);
}

static void cfg_factory_reset_request_cb(lv_event_t *e)
{
    (void)e;
    if (s_factory_reset_modal) return;

    s_factory_reset_modal = lv_obj_create(s_settings_overlay);
    lv_obj_add_flag(s_factory_reset_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_factory_reset_modal, SCR_W, SCR_H);
    lv_obj_set_pos(s_factory_reset_modal, 0, 0);
    lv_obj_set_style_bg_color(s_factory_reset_modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_factory_reset_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_factory_reset_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_factory_reset_modal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_factory_reset_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_factory_reset_modal);
    ui_fade(s_factory_reset_modal, LV_OPA_TRANSP, LV_OPA_COVER, 140, NULL);

    lv_obj_t *panel = lv_obj_create(s_factory_reset_modal);
    lv_obj_set_size(panel, 560, 300);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 24, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 18, LV_PART_MAIN);

    make_label(panel, "FACTORY RESET?", DASH_FONT_LABEL14, C_RED);
    lv_obj_t *message = make_label(panel,
        "Reset all dashboard settings, warning limits, units, ECU selection, and theme layouts? This cannot be undone.",
        DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *buttons = make_plain_container(panel);
    lv_obj_set_size(buttons, LV_PCT(100), 58);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 16, LV_PART_MAIN);

    lv_obj_t *cancel = lv_obj_create(buttons);
    lv_obj_set_size(cancel, 220, 58);
    lv_obj_set_style_bg_color(cancel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel, 10, LV_PART_MAIN);
    lv_obj_clear_flag(cancel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, cfg_factory_reset_cancel_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(cancel);
    lv_obj_center(make_label(cancel, "Cancel", DASH_FONT_LABEL14, C_WHITE));

    lv_obj_t *confirm = lv_obj_create(buttons);
    lv_obj_set_size(confirm, 220, 58);
    lv_obj_set_style_bg_color(confirm, C_RED, LV_PART_MAIN);
    lv_obj_set_style_border_width(confirm, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(confirm, 10, LV_PART_MAIN);
    lv_obj_clear_flag(confirm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(confirm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(confirm, cfg_factory_reset_confirm_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(confirm);
    lv_obj_center(make_label(confirm, "Reset Everything", DASH_FONT_LABEL14, C_WHITE));
}

static void cfg_theme_layout_reset_cb(lv_event_t *e)
{
    system_field_theme_t theme = (system_field_theme_t)(intptr_t)lv_event_get_user_data(e);
    int field_count;

    if (theme == SYSTEM_FIELD_MODERN) {
        dash_config_reset_modern_field_channels();
        field_count = DASH_CONFIG_MODERN_FIELD_COUNT;
    } else if (theme == SYSTEM_FIELD_RACE) {
        dash_config_reset_race_field_channels();
        field_count = DASH_CONFIG_RACE_FIELD_COUNT;
    } else if (theme == SYSTEM_FIELD_ENDURANCE) {
        dash_config_reset_endurance_field_channels();
        field_count = DASH_CONFIG_ENDURANCE_FIELD_COUNT;
    } else if (theme == SYSTEM_FIELD_TOURING) {
        dash_config_reset_touring_field_channels();
        field_count = DASH_CONFIG_TOURING_FIELD_COUNT;
    } else {
        dash_config_reset_hal_field_channels();
        field_count = DASH_CONFIG_HAL_FIELD_COUNT;
    }

    for (int slot = 0; slot < field_count; ++slot) {
        system_field_refresh_identity(theme, slot);
    }
}

static void build_theme_layout_reset_button(lv_obj_t *parent, const char *label,
                                            system_field_theme_t theme)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_size(button, 250, 136);
    lv_obj_set_style_bg_color(button, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(button, 10, LV_PART_MAIN);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, cfg_theme_layout_reset_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)theme);
    add_press_feedback(button);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(button, 8, LV_PART_MAIN);
    make_label(button, LV_SYMBOL_REFRESH, DASH_FONT_TILEVAL, C_WHITE);
    lv_obj_t *button_label = make_label(button, label, DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(button_label, 220);
    lv_obj_set_style_text_align(button_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(button_label, LV_LABEL_LONG_WRAP);
}

static void settings_back_cb(lv_event_t *e)
{
    (void)e;
    if (!ota_update_is_running() && !ota_update_is_starting()) {
        settings_set_update_background_paused(false);
    }
    settings_show_page(s_page_main);
}

static void settings_config_back_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_config);
}

static void settings_display_child_back_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_display);
}

static void settings_logs_child_back_cb(lv_event_t *e)
{
    (void)e;
    settings_show_page(s_page_logs_menu);
}

static void settings_nav_cb(lv_event_t *e)
{
    if (s_settings_current_page == s_page_main) {
        settings_close_cb(e);
    } else if (s_settings_current_page == s_page_units) {
        settings_display_child_back_cb(e);
    } else if (s_settings_current_page == s_page_peaks ||
               s_settings_current_page == s_page_logs ||
               s_settings_current_page == s_page_achievements) {
        settings_logs_child_back_cb(e);
    } else if (s_settings_current_page == s_page_fuel ||
               s_settings_current_page == s_page_info ||
               s_settings_current_page == s_page_shortcuts ||
               s_settings_current_page == s_page_readme ||
             s_settings_current_page == s_page_theme_resets ||
             s_settings_current_page == s_page_contact) {
        settings_config_back_cb(e);
    } else {
        settings_back_cb(e);
    }
}
static void settings_update_refresh_ui(void);

static void settings_update_poll_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    settings_update_refresh_ui();
    if (!ota_update_is_starting()) {
        if (s_update_poll_timer) {
            lv_timer_del(s_update_poll_timer);
            s_update_poll_timer = NULL;
        }
    }
}

static void settings_update_start_poll_timer(void)
{
    if (!s_update_poll_timer) {
        s_update_poll_timer = lv_timer_create(settings_update_poll_timer_cb, 300, NULL);
    } else {
        lv_timer_reset(s_update_poll_timer);
    }
}

static void settings_update_stop_poll_timer(void)
{
    if (s_update_poll_timer) {
        lv_timer_del(s_update_poll_timer);
        s_update_poll_timer = NULL;
    }
}

typedef struct {
    const char *label;
    const char *destination;
} contact_destination_t;

static const contact_destination_t CONTACT_DESTINATIONS[] = {
    {"Report a Problem", "https://github.com/mackotuned/MackoDash-Flash-Tools/issues"},
    {"Email", "mailto:mackotuned@gmail.com?subject=MackoDash%20Support"},
    {"Instagram", "https://instagram.com/cream_civic"},
    {"TikTok", "https://tiktok.com/@mackotuned"},
    {"Leave Feedback / Review", "mailto:mackotuned@gmail.com?subject=MackoDash%20Review"},
};

static void settings_contact_show(uint16_t selected)
{
    if (selected >= sizeof(CONTACT_DESTINATIONS) / sizeof(CONTACT_DESTINATIONS[0])) selected = 0;
    const contact_destination_t *destination = &CONTACT_DESTINATIONS[selected];
    lv_qrcode_update(s_contact_qr, destination->destination,
                     (uint32_t)strlen(destination->destination));
    lv_label_set_text(s_contact_destination_label, destination->destination);
}

static void settings_contact_select_cb(lv_event_t *event)
{
    settings_contact_show(lv_dropdown_get_selected(lv_event_get_target(event)));
}

static void settings_update_refresh_qr(void)
{
    if (!s_update_qr) {
        return;
    }

    char qr_payload[128];
    snprintf(qr_payload, sizeof(qr_payload), "%s", ota_update_get_url());
    lv_qrcode_update(s_update_qr, qr_payload, (uint32_t)strlen(qr_payload));
}

static void settings_update_refresh_ui(void)
{
    if (!s_update_status_label || !s_update_details_label) {
        return;
    }

    if (ota_update_is_starting()) {
        lv_label_set_text_fmt(
            s_update_status_label,
            "C6 OTA: STARTING [%s]",
            ota_update_get_status_detail());
        lv_label_set_text_fmt(
            s_update_details_label,
            "Bringing up Wi-Fi AP and web uploader.\nStage: %s",
            ota_update_get_status_detail());
        lv_obj_add_flag(s_update_qr, LV_OBJ_FLAG_HIDDEN);
    } else if (ota_update_is_running()) {
        lv_label_set_text(s_update_status_label, "C6 OTA: READY TO CONNECT");
        lv_label_set_text_fmt(
            s_update_details_label,
            "1) Join Wi-Fi: %s\n2) Scan QR to open uploader (or browse %s)",
            ota_update_get_ssid(),
            ota_update_get_url());
        lv_obj_clear_flag(s_update_qr, LV_OBJ_FLAG_HIDDEN);
        settings_update_refresh_qr();
    } else {
        esp_err_t last_err = ota_update_get_last_error();
        if (last_err != ESP_OK) {
            lv_label_set_text_fmt(
                s_update_status_label,
                "C6 OTA: FAILED 0x%x [%s]",
                (unsigned int)last_err,
                ota_update_get_status_detail());
            lv_label_set_text_fmt(
                s_update_details_label,
                "Wi-Fi AP did not start. Press Start to retry.\nStage: %s",
                ota_update_get_status_detail());
        } else {
            lv_label_set_text(s_update_status_label, "C6 OTA: IDLE [DBG0721]");
            lv_label_set_text(
                s_update_details_label,
                "Press Start, then join ESP-Hosted-OTA in phone Wi-Fi settings.\nAfter joining, scan QR to open the uploader page.");
        }
        lv_obj_add_flag(s_update_qr, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_start_update_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = ota_update_start_ap_server();
    if (err != ESP_OK) {
        settings_update_stop_poll_timer();
        if (err == ESP_ERR_NOT_SUPPORTED) {
            lv_label_set_text(s_update_status_label, "C6 OTA Start Failed: bridge not registered");
            lv_label_set_text(
                s_update_details_label,
                "No hosted-OTA bridge is registered yet.\nThe C6 bridge module must call ota_update_register_bridge().");
            return;
        }
        lv_label_set_text_fmt(
            s_update_status_label,
            "C6 OTA: FAILED 0x%x [%s]",
            (unsigned int)err,
            ota_update_get_status_detail());
        lv_label_set_text_fmt(
            s_update_details_label,
            "Immediate start failure.\nStage: %s",
            ota_update_get_status_detail());
        settings_update_refresh_ui();
        settings_update_stop_poll_timer();
        return;
    }
    settings_update_start_poll_timer();
    settings_update_refresh_ui();
}

static void settings_stop_update_cb(lv_event_t *e)
{
    (void)e;
    settings_update_stop_poll_timer();
    esp_err_t err = ota_update_stop_ap_server();
    if (err != ESP_OK) {
        lv_label_set_text_fmt(s_update_status_label, "C6 OTA Stop Failed: 0x%x", (unsigned int)err);
        return;
    }
    settings_update_refresh_ui();
}

/* ---- theme picker ---- */
static lv_obj_t *s_theme_card_modern;
static lv_obj_t *s_theme_card_race_lcd;
static lv_obj_t *s_theme_card_haldash;
static lv_obj_t *s_theme_card_endurance;
static lv_obj_t *s_theme_card_touring;
static lv_obj_t *s_theme_card_sd[THEME_STORAGE_MAX_PACKAGES];
static lv_obj_t *s_theme_grid;
static lv_obj_t *s_theme_delete_modal;
static lv_obj_t *s_theme_delete_message;
static size_t s_theme_delete_index = SIZE_MAX;
static int s_theme_pending_idx = THEME_ID_MODERN;

static void theme_card_set_selected(lv_obj_t *card, bool selected)
{
    lv_obj_set_style_border_color(card, selected ? C_RED : C_LINE, LV_PART_MAIN);
}

static void theme_stage_selection(int idx)
{
    s_theme_pending_idx = idx;
    theme_card_set_selected(s_theme_card_modern, idx == THEME_ID_MODERN);
    theme_card_set_selected(s_theme_card_race_lcd, idx == THEME_ID_RACE_LCD);
    theme_card_set_selected(s_theme_card_haldash, idx == THEME_ID_HALDASH);
    theme_card_set_selected(s_theme_card_endurance, idx == THEME_ID_ENDURANCE);
    theme_card_set_selected(s_theme_card_touring, idx == THEME_ID_TOURING);
    for (size_t index = 0; index < theme_storage_get_count(); ++index) {
        if (s_theme_card_sd[index]) {
            theme_card_set_selected(s_theme_card_sd[index], idx == 100 + (int)index);
        }
    }
}

/* ---- theme persistence (NVS) ---- */
#define THEME_NVS_NAMESPACE "honda_dash"
#define THEME_NVS_KEY       "theme_idx"
#define THEME_NVS_ID_KEY    "theme_id"
#define THEME_NVS_TOURING_RECOVERY_KEY "tour_recover"
#define THEME_NVS_PENDING_KEY "theme_pending"

static int64_t s_theme_stability_started_us;

static void theme_recovery_set_pending(bool pending)
{
    nvs_handle_t handle;
    if (nvs_open(THEME_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (pending) nvs_set_u8(handle, THEME_NVS_PENDING_KEY, 1);
    else nvs_erase_key(handle, THEME_NVS_PENDING_KEY);
    nvs_commit(handle);
    nvs_close(handle);
}

static void theme_recovery_schedule_clear(void)
{
    s_theme_stability_started_us = esp_timer_get_time();
}

static int theme_storage_load(void)
{
    /* Safe even if main.c already called this -- nvs_flash_init() no-ops
       if NVS is already initialized. If the saved theme mysteriously never
       sticks, check that main.c's own NVS init is actually succeeding. */
    nvs_flash_init();

    nvs_handle_t h;
    if (nvs_open(THEME_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    uint8_t pending = 0;
    if (nvs_get_u8(h, THEME_NVS_PENDING_KEY, &pending) == ESP_OK && pending) {
        nvs_close(h);
        if (nvs_open(THEME_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_i32(h, THEME_NVS_KEY, THEME_ID_MODERN);
            nvs_erase_key(h, THEME_NVS_ID_KEY);
            nvs_erase_key(h, THEME_NVS_PENDING_KEY);
            nvs_commit(h);
            nvs_close(h);
        }
        ESP_LOGW("THEME", "Previous theme did not stabilize; recovered to MackoDash V1");
        return THEME_ID_MODERN;
    }
    char theme_id[THEME_STORAGE_ID_MAX] = {0};
    size_t theme_id_size = sizeof(theme_id);
    if (nvs_get_str(h, THEME_NVS_ID_KEY, theme_id, &theme_id_size) == ESP_OK) {
        nvs_close(h);
        for (size_t i = 0; i < theme_storage_get_count(); ++i) {
            const theme_storage_package_t *package = theme_storage_get_package(i);
            if (package && package->manifest_valid && strcmp(package->id, theme_id) == 0) {
                theme_recovery_set_pending(true);
                return 100 + (int)i;
            }
        }
        return 0;
    }
    int32_t val = 0;
    esp_err_t err = nvs_get_i32(h, THEME_NVS_KEY, &val);
    nvs_close(h);
    if (err == ESP_OK && val == THEME_ID_TOURING) {
        nvs_handle_t recovery;
        if (nvs_open(THEME_NVS_NAMESPACE, NVS_READWRITE, &recovery) == ESP_OK) {
            uint8_t recovered = 0;
            if (nvs_get_u8(recovery, THEME_NVS_TOURING_RECOVERY_KEY, &recovered) != ESP_OK) {
                nvs_set_u8(recovery, THEME_NVS_TOURING_RECOVERY_KEY, 1);
                nvs_set_i32(recovery, THEME_NVS_KEY, THEME_ID_MODERN);
                nvs_commit(recovery);
                nvs_close(recovery);
                ESP_LOGW("THEME", "Recovered persisted Touring selection after renderer update");
                return THEME_ID_MODERN;
            }
            nvs_close(recovery);
        }
    }
    if (err != ESP_OK || (val != THEME_ID_RACE_LCD && val != THEME_ID_HALDASH &&
            val != THEME_ID_ENDURANCE && val != THEME_ID_TOURING)) return THEME_ID_MODERN;
    theme_recovery_set_pending(true);
    return (int)val;
}

static void theme_storage_save(int idx)
{
    nvs_handle_t h;
    if (nvs_open(THEME_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, THEME_NVS_KEY, (int32_t)idx);
    if (idx >= 100) {
        const theme_storage_package_t *package = theme_storage_get_package((size_t)(idx - 100));
        if (package) nvs_set_str(h, THEME_NVS_ID_KEY, package->id);
    } else {
        nvs_erase_key(h, THEME_NVS_ID_KEY);
    }
    nvs_commit(h);
    nvs_close(h);
    theme_recovery_set_pending(idx != THEME_ID_MODERN);
}

/* pushes the last-known data snapshot straight into whichever theme is now
   active, so switching themes doesn't sit blank until the next tick */
static void refresh_active_theme(void)
{
    if (!s_have_last_data) return;
    const honda_dash_data_t *data = &s_last_data;

    int rpm = data->rpm;
    if (rpm < 0) rpm = 0;
    if (rpm > MAXRPM) rpm = MAXRPM;
    bool vtec_on = rpm >= VTEC_RPM;
    bool limiter_hit = rpm >= FULL_RED_RPM;
    float fuel = data->fuel_pct;
    if (fuel < 0) fuel = 0;
    if (fuel > 100) fuel = 100;

    if (s_active_theme >= 100) runtime_theme_update(data);
    else if (s_active_theme == THEME_ID_RACE_LCD) update_theme_race_lcd(data, rpm, limiter_hit, fuel);
    else if (s_active_theme == THEME_ID_HALDASH) update_theme_haldash(data, rpm, limiter_hit, fuel);
    else if (s_active_theme == THEME_ID_ENDURANCE) update_theme_endurance(data, rpm, limiter_hit, fuel);
    else if (s_active_theme == THEME_ID_TOURING) update_theme_touring(data, rpm, limiter_hit, fuel);
    else update_theme_modern(data, rpm, limiter_hit, vtec_on, fuel);
}

/* single source of truth for "switch to theme N" -- used when the user
    confirms a staged selection and when restoring the saved theme at boot */
static void activate_theme(int idx, bool persist)
{
    if (idx >= 100) {
        const theme_storage_package_t *package = theme_storage_get_package((size_t)(idx - 100));
        esp_err_t err = package && package->manifest_valid ?
                runtime_theme_load(s_cluster, package, settings_btn_cb, record_btn_cb,
                                   sim_btn_cb, sim_btn_long_press_cb) : ESP_ERR_INVALID_ARG;
        if (err != ESP_OK) {
            ESP_LOGW("THEME", "Cannot load SD theme %d: %s; using MackoDash V1", idx, esp_err_to_name(err));
            runtime_theme_unload();
            idx = 0;
        }
    } else {
        runtime_theme_unload();
        if (idx != THEME_ID_RACE_LCD && idx != THEME_ID_HALDASH &&
                idx != THEME_ID_ENDURANCE && idx != THEME_ID_TOURING) idx = THEME_ID_MODERN;
    }
    s_active_theme = idx;
    if (idx == THEME_ID_RACE_LCD) {
        s_rpk_last_rpm = -1;
        s_rpk_last_gear = -2;
        s_rpk_last_speed = -1;
        s_rpk_last_speed_kph = -1;
        memset(s_rpk_last_field_text, 0, sizeof(s_rpk_last_field_text));
    }

    lv_obj_add_flag(s_theme_modern, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_theme_race_lcd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_theme_haldash, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_theme_endurance, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_theme_touring, LV_OBJ_FLAG_HIDDEN);
    theme_card_set_selected(s_theme_card_modern, idx == THEME_ID_MODERN);
    theme_card_set_selected(s_theme_card_race_lcd, idx == THEME_ID_RACE_LCD);
    theme_card_set_selected(s_theme_card_haldash, idx == THEME_ID_HALDASH);
    theme_card_set_selected(s_theme_card_endurance, idx == THEME_ID_ENDURANCE);
    theme_card_set_selected(s_theme_card_touring, idx == THEME_ID_TOURING);
    for (size_t i = 0; i < theme_storage_get_count(); ++i) {
        if (s_theme_card_sd[i]) theme_card_set_selected(s_theme_card_sd[i], idx == 100 + (int)i);
    }

    if (idx >= 100) {
        lv_obj_t *runtime_root = runtime_theme_get_root();
        if (runtime_root) lv_obj_clear_flag(runtime_root, LV_OBJ_FLAG_HIDDEN);
        if (s_warning_banner) lv_obj_move_foreground(s_warning_banner);
        lv_obj_move_foreground(s_settings_overlay);
    } else {
        lv_obj_t *shown = idx == THEME_ID_RACE_LCD ? s_theme_race_lcd :
                          (idx == THEME_ID_HALDASH ? s_theme_haldash :
                          (idx == THEME_ID_ENDURANCE ? s_theme_endurance :
                          (idx == THEME_ID_TOURING ? s_theme_touring : s_theme_modern)));
        lv_obj_clear_flag(shown, LV_OBJ_FLAG_HIDDEN);
    }

    if (persist) theme_storage_save(idx);
    refresh_active_theme();
    if (idx != THEME_ID_MODERN) {
        theme_recovery_schedule_clear();
    } else {
        s_theme_stability_started_us = 0;
    }
}

static void theme_card_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    theme_stage_selection(idx);
}

static int theme_sd_index_for_card(lv_obj_t *card)
{
    for (size_t i = 0; i < theme_storage_get_count(); ++i) {
        if (s_theme_card_sd[i] == card) return (int)i;
    }
    return -1;
}

static void theme_sd_card_cb(lv_event_t *e)
{
    int index = theme_sd_index_for_card(lv_event_get_target(e));
    if (index >= 0) theme_stage_selection(100 + index);
}

static void theme_set_cb(lv_event_t *e)
{
    activate_theme(s_theme_pending_idx, true);
    settings_close_cb(e);
}

static void theme_delete_modal_close(void)
{
    if (s_theme_delete_modal) lv_obj_del_async(s_theme_delete_modal);
    s_theme_delete_modal = NULL;
    s_theme_delete_message = NULL;
    s_theme_delete_index = SIZE_MAX;
}

static void theme_delete_cancel_cb(lv_event_t *e)
{
    (void)e;
    theme_delete_modal_close();
}

static void theme_delete_confirm_cb(lv_event_t *e)
{
    (void)e;
    size_t index = s_theme_delete_index;
    size_t old_count = theme_storage_get_count();
    if (index >= old_count) {
        theme_delete_modal_close();
        return;
    }

    int deleted_theme = 100 + (int)index;
    if (s_active_theme == deleted_theme) {
        activate_theme(0, true);
    }

    esp_err_t err = theme_storage_delete(index);
    if (err != ESP_OK) {
        if (s_theme_delete_message) lv_label_set_text(s_theme_delete_message, "Could not delete the theme from the SD card.");
        return;
    }

    lv_obj_del_async(s_theme_card_sd[index]);
    size_t new_count = old_count - 1;
    for (size_t i = index; i < new_count; ++i) {
        s_theme_card_sd[i] = s_theme_card_sd[i + 1];
        size_t card_index = i + 5;
        lv_obj_set_grid_cell(s_theme_card_sd[i], LV_GRID_ALIGN_STRETCH, (int)(card_index % 2), 1,
                             LV_GRID_ALIGN_STRETCH, (int)(card_index / 2), 1);
    }
    s_theme_card_sd[new_count] = NULL;

    if (s_active_theme > deleted_theme) {
        --s_active_theme;
        theme_storage_save(s_active_theme);
    }
    if (s_theme_pending_idx == deleted_theme) s_theme_pending_idx = s_active_theme;
    else if (s_theme_pending_idx > deleted_theme) --s_theme_pending_idx;
    theme_stage_selection(s_theme_pending_idx);
    theme_delete_modal_close();
}

static void theme_delete_request_cb(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_user_data(e);
    int index = theme_sd_index_for_card(card);
    if (index < 0 || s_theme_delete_modal) return;
    const theme_storage_package_t *package = theme_storage_get_package((size_t)index);
    if (!package) return;

    s_theme_delete_index = (size_t)index;
    s_theme_delete_modal = lv_obj_create(s_settings_overlay);
    lv_obj_add_flag(s_theme_delete_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_theme_delete_modal, SCR_W, SCR_H);
    lv_obj_set_pos(s_theme_delete_modal, 0, 0);
    lv_obj_set_style_bg_color(s_theme_delete_modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_theme_delete_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_theme_delete_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_theme_delete_modal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_theme_delete_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_theme_delete_modal);
    lv_obj_set_size(panel, 520, 280);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 24, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 18, LV_PART_MAIN);

    make_label(panel, "DELETE THEME?", DASH_FONT_LABEL14, C_WHITE);
    char prompt[160];
    snprintf(prompt, sizeof(prompt), "Remove %s from the SD card? This cannot be undone.", package->display_name);
    s_theme_delete_message = make_label(panel, prompt, DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_width(s_theme_delete_message, LV_PCT(100));
    lv_label_set_long_mode(s_theme_delete_message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_theme_delete_message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *buttons = make_plain_container(panel);
    lv_obj_set_size(buttons, LV_PCT(100), 56);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 14, LV_PART_MAIN);

    lv_obj_t *cancel = lv_obj_create(buttons);
    lv_obj_set_size(cancel, 200, 56);
    lv_obj_set_style_bg_color(cancel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel, 10, LV_PART_MAIN);
    lv_obj_clear_flag(cancel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, theme_delete_cancel_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(cancel);
    lv_obj_center(make_label(cancel, "Cancel", DASH_FONT_LABEL14, C_LABEL));

    lv_obj_t *confirm = lv_obj_create(buttons);
    lv_obj_set_size(confirm, 200, 56);
    lv_obj_set_style_bg_color(confirm, C_RED, LV_PART_MAIN);
    lv_obj_set_style_border_width(confirm, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(confirm, 10, LV_PART_MAIN);
    lv_obj_clear_flag(confirm, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(confirm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(confirm, theme_delete_confirm_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(confirm);
    lv_obj_center(make_label(confirm, "Delete", DASH_FONT_LABEL14, C_WHITE));
}

/* small stylized preview drawn from basic shapes (no image assets needed)
   so each theme card gives a rough visual sense of what it looks like */
static void build_theme_preview(lv_obj_t *parent, int theme_idx)
{
    lv_obj_t *pv = lv_obj_create(parent);
    lv_obj_set_size(pv, LV_PCT(100), 100);
    lv_obj_set_style_bg_color(pv, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pv, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(pv, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(pv, 6, LV_PART_MAIN);
    lv_obj_clear_flag(pv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(pv, LV_OBJ_FLAG_CLICKABLE);

    switch (theme_idx) {
    case 0: { /* Modern: thin white bar + a row of colored tile swatches */
        lv_obj_t *bar = lv_obj_create(pv);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(bar, LV_PCT(86), 8);
        lv_obj_set_pos(bar, 10, 10);
        lv_obj_set_style_bg_color(bar, C_WHITE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

        uint32_t colors[4] = { 0x4d8fff, 0x39ff8c, 0xe4002b, 0xffb020 };
        for (int i = 0; i < 4; i++) {
            lv_obj_t *sq = lv_obj_create(pv);
            lv_obj_add_flag(sq, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_size(sq, 18, 18);
            lv_obj_set_pos(sq, 10 + i * 24, 40);
            lv_obj_set_style_bg_color(sq, lv_color_hex(colors[i]), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(sq, LV_OPA_60, LV_PART_MAIN);
            lv_obj_set_style_border_width(sq, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(sq, 3, LV_PART_MAIN);
            lv_obj_clear_flag(sq, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(sq, LV_OBJ_FLAG_CLICKABLE);
        }
        break;
    }
    case 6: {
        lv_obj_set_style_bg_color(pv, lv_color_hex(0x00efff), LV_PART_MAIN);
        for (int i = 0; i < 12; i++) {
            lv_obj_t *seg = lv_obj_create(pv);
            lv_obj_add_flag(seg, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_coord_t y = 10 + (lv_coord_t)(abs(i * 2 - 11) * 2);
            lv_obj_set_size(seg, 7, 24);
            lv_obj_set_pos(seg, 10 + i * 10, y);
            lv_obj_set_style_bg_color(seg, lv_color_black(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(seg, 0, LV_PART_MAIN);
            lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_t *num = make_label(pv, "5", DASH_FONT_TILEVAL, lv_color_black());
        lv_obj_align(num, LV_ALIGN_BOTTOM_MID, 0, -4);
        break;
    }
    case THEME_ID_HALDASH: {
        lv_obj_set_style_bg_color(pv, lv_color_hex(0x070707), LV_PART_MAIN);
        for (int i = 0; i < 2; ++i) {
            lv_obj_t *dial = lv_arc_create(pv);
            lv_obj_add_flag(dial, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_size(dial, 72, 72);
            lv_obj_set_pos(dial, 18 + i * 78, 14);
            lv_arc_set_bg_angles(dial, 135, 45);
            lv_arc_set_range(dial, 0, 100);
            lv_arc_set_value(dial, i == 0 ? 72 : 48);
            lv_obj_remove_style(dial, NULL, LV_PART_KNOB);
            lv_obj_set_style_arc_width(dial, 6, LV_PART_MAIN);
            lv_obj_set_style_arc_color(dial, lv_color_hex(0x343434), LV_PART_MAIN);
            lv_obj_set_style_arc_width(dial, 6, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(dial, i == 0 ? HAL_RED : C_WHITE, LV_PART_INDICATOR);
            lv_obj_clear_flag(dial, LV_OBJ_FLAG_CLICKABLE);
        }
        break;
    }
    case THEME_ID_ENDURANCE: {
        lv_obj_set_style_bg_color(pv, lv_color_hex(0x090a0b), LV_PART_MAIN);
        lv_obj_t *brand = lv_obj_create(pv);
        lv_obj_add_flag(brand, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(brand, 78, 14);
        lv_obj_set_pos(brand, 46, 7);
        lv_obj_set_style_bg_color(brand, C_WHITE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(brand, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(brand, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(brand, 0, LV_PART_MAIN);
        lv_obj_clear_flag(brand, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        for (int i = 0; i < 14; ++i) {
            lv_obj_t *seg = lv_obj_create(pv);
            lv_obj_add_flag(seg, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_size(seg, 8, 7);
            lv_obj_set_pos(seg, 10 + i * 11, 27);
            lv_obj_set_style_bg_color(seg, i < 10 ? HUNTER_RED : HUNTER_SEG_OFF, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(seg, 0, LV_PART_MAIN);
            lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_t *speed = make_label(pv, "128", DASH_FONT_TILEVAL, C_WHITE);
        lv_obj_align(speed, LV_ALIGN_BOTTOM_MID, 0, -10);
        break;
    }
    case THEME_ID_TOURING: {
        lv_obj_set_style_bg_color(pv, lv_color_hex(0x090a0b), LV_PART_MAIN);
        for (int pair = 0; pair < 5; ++pair) {
            for (int side = 0; side < 2; ++side) {
                lv_obj_t *light = lv_obj_create(pv);
                lv_obj_add_flag(light, LV_OBJ_FLAG_IGNORE_LAYOUT);
                lv_obj_set_size(light, 12, 12);
                lv_obj_set_pos(light, side == 0 ? 13 + pair * 15 : 145 - pair * 15, 19 - pair * 3);
                lv_obj_set_style_bg_color(light, pair < 4 ? C_WHITE : RALLY_LIGHT_OFF, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(light, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(light, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(light, lv_color_hex(0x555a5e), LV_PART_MAIN);
                lv_obj_set_style_radius(light, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                lv_obj_clear_flag(light, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            }
        }
        lv_obj_t *rpm = make_label(pv, "8250", DASH_FONT_LABEL14, C_WHITE);
        lv_obj_align(rpm, LV_ALIGN_BOTTOM_LEFT, 20, -10);
        lv_obj_t *speed = make_label(pv, "128", DASH_FONT_LABEL14, C_WHITE);
        lv_obj_align(speed, LV_ALIGN_BOTTOM_RIGHT, -20, -10);
        break;
    }
    default: {
        lv_obj_set_style_bg_color(pv, lv_color_hex(0x101216), LV_PART_MAIN);
        lv_obj_t *sd_label = make_label(pv, "SD", DASH_FONT_TILEVAL, C_WHITE);
        lv_obj_align(sd_label, LV_ALIGN_CENTER, 0, -8);
        lv_obj_t *status = make_label(pv, "IMPORTED", DASH_FONT_LABEL, C_LABEL);
        lv_obj_align(status, LV_ALIGN_CENTER, 0, 20);
        break;
    }
    }
}

/* visible touch feedback: dim + red border flash while actively pressed.
   Works regardless of a button's normal styling (even fully transparent
   ones), since it's LVGL's built-in LV_STATE_PRESSED, not something we
   have to track ourselves. */
static void add_press_feedback(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, C_RED, LV_PART_MAIN | LV_STATE_PRESSED);
}

static lv_obj_t *build_theme_grid_card(lv_obj_t *parent, const char *name, const char *status,
                                       int theme_idx, int col, int row, bool selectable)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (selectable) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, theme_idx >= 100 ? theme_sd_card_cb : theme_card_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)theme_idx);
        add_press_feedback(card);
    } else {
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(card, lv_color_hex(0x3b4654), LV_PART_MAIN);
    }
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);

    build_theme_preview(card, theme_idx);
    lv_obj_t *name_label = make_label(card, name, DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(name_label, LV_PCT(100));
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    if (status) {
        lv_obj_t *status_label = make_label(card, status, DASH_FONT_LABEL, C_LABEL_DIM);
        lv_obj_set_width(status_label, LV_PCT(100));
        lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    return card;
}

static void add_theme_delete_button(lv_obj_t *card)
{
    lv_obj_t *button = lv_obj_create(card);
    lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(button, 42, 42);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_set_style_bg_color(button, C_RED_DEEP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, C_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, theme_delete_request_cb, LV_EVENT_CLICKED, card);
    add_press_feedback(button);
    lv_obj_t *icon = make_label(button, LV_SYMBOL_TRASH, DASH_FONT_LABEL14, C_WHITE);
    lv_obj_center(icon);
}

static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);
    bsp_display_brightness_set((int)v);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)v);
    lv_label_set_text(s_brightness_value_label, buf);
}

static void brightness_slider_released_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    dash_config_set_brightness(lv_slider_get_value(slider));
}

static void configure_menu_slider(lv_obj_t *slider)
{
    lv_obj_add_flag(slider, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
}

/* large icon+label tile used on the main settings page */
static lv_obj_t *build_settings_tile(lv_obj_t *parent, const char *icon_sym, const char *title, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 270, 150);
    lv_obj_set_style_bg_color(btn, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(btn);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 10, LV_PART_MAIN);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, icon_sym);
    lv_obj_set_style_text_font(icon, DASH_FONT_TILEVAL, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, C_WHITE, LV_PART_MAIN);

    make_label(btn, title, DASH_FONT_LABEL14, C_WHITE);
    return btn;
}

static lv_obj_t *build_config_subpage(lv_obj_t *panel, lv_obj_t **page_out, const char *title)
{
    lv_obj_t *page = make_plain_container(panel);
    *page_out = page;
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 14, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    make_label(page, title, DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *content = make_plain_container(page);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 12, LV_PART_MAIN);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    return content;
}

static lv_obj_t *build_shift_rpm_card(lv_obj_t *parent, const char *title, lv_coord_t width,
                                      lv_obj_t **value_label, lv_obj_t **color_dropdown,
                                      lv_event_cb_t changed_cb, lv_event_cb_t released_cb,
                                      intptr_t user_data)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, 96);
    lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);

    lv_obj_t *top = make_plain_container(card);
    lv_obj_set_size(top, LV_PCT(100), 36);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(top, title, DASH_FONT_LABEL14, C_LABEL);
    *value_label = make_label(top, "0 RPM", DASH_FONT_LABEL14, C_WHITE);
    if (color_dropdown) {
        *color_dropdown = lv_dropdown_create(top);
        lv_obj_set_size(*color_dropdown, 160, 34);
        lv_dropdown_set_options(*color_dropdown, SHIFT_COLOR_OPTIONS);
        lv_obj_add_event_cb(*color_dropdown, cfg_shift_color_cb,
                            LV_EVENT_VALUE_CHANGED, (void *)user_data);
    }

    lv_obj_t *slider = lv_slider_create(card);
    configure_menu_slider(slider);
    lv_obj_set_size(slider, LV_PCT(100), 14);
    lv_slider_set_range(slider, 3000, 11000);
    lv_obj_set_style_bg_color(slider, C_RED, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, C_WHITE, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, changed_cb, LV_EVENT_VALUE_CHANGED, (void *)user_data);
    lv_obj_add_event_cb(slider, released_cb, LV_EVENT_RELEASED, (void *)user_data);
    return slider;
}

static lv_obj_t *build_config_menu_row(lv_obj_t *parent, const char *title, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 64);
    lv_obj_set_style_bg_color(row, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 14, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(row, title, DASH_FONT_LABEL14, C_WHITE);
    make_label(row, LV_SYMBOL_RIGHT, DASH_FONT_LABEL14, C_LABEL);
    return row;
}

static void build_shortcut_tip(lv_obj_t *parent, const char *gesture, const char *detail)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), 76);
    lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 18, LV_PART_MAIN);

    lv_obj_t *gesture_label = make_label(card, gesture, DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(gesture_label, 190);
    lv_obj_t *detail_label = make_label(card, detail, DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_width(detail_label, 600);
    lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
}

static lv_obj_t *build_diagnostics_tile(lv_obj_t *parent, const char *title)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 405, 104);
    lv_obj_set_style_bg_color(tile, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(tile, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 13, LV_PART_MAIN);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tile, 7, LV_PART_MAIN);

    make_label(tile, title, DASH_FONT_LABEL, C_LABEL_DIM);
    lv_obj_t *value = make_label(tile, "Waiting", DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_width(value, LV_PCT(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    return value;
}

static const char *diagnostics_controller_name(canbus_controller_state_t state)
{
    switch (state) {
        case CANBUS_CONTROLLER_STOPPED: return "STOPPED";
        case CANBUS_CONTROLLER_RUNNING: return "RUNNING";
        case CANBUS_CONTROLLER_BUS_OFF: return "BUS OFF";
        case CANBUS_CONTROLLER_RECOVERING: return "RECOVERING";
        default: return "OFFLINE";
    }
}

static const char *diagnostics_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON: return "POWER ON/LOSS";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "CRASH";
        case ESP_RST_INT_WDT: return "INT WDT";
        case ESP_RST_TASK_WDT: return "TASK WDT";
        case ESP_RST_WDT: return "OTHER WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_PWR_GLITCH: return "POWER GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU LOCKUP";
        default: return "UNKNOWN";
    }
}

static void settings_diagnostics_refresh(void)
{
    if (!s_page_info || lv_obj_has_flag(s_page_info, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    canbus_diagnostics_t diagnostics;
    canbus_get_diagnostics(&diagnostics);

    char text[96];
    snprintf(text, sizeof(text), "%s / %s",
             diagnostics.live_data ? "LIVE" : "NO DATA",
             diagnostics_controller_name(diagnostics.controller_state));
    lv_label_set_text(s_diagnostics_values[DIAG_CAN_BUS], text);
    lv_obj_set_style_text_color(s_diagnostics_values[DIAG_CAN_BUS],
                                diagnostics.live_data ? C_GREEN : C_AMBER, LV_PART_MAIN);

    if (diagnostics.bitrate > 0) {
        snprintf(text, sizeof(text), "%s / %d kbps%s", diagnostics.protocol,
                 diagnostics.bitrate / 1000, diagnostics.obd2_active ? " / OBD-II" : "");
    } else {
        snprintf(text, sizeof(text), "%s / Detecting rate", diagnostics.protocol);
    }
    lv_label_set_text(s_diagnostics_values[DIAG_PROTOCOL], text);

    if (diagnostics.live_data) {
        snprintf(text, sizeof(text), "%lu ms ago", (unsigned long)diagnostics.last_frame_age_ms);
    } else if (diagnostics.last_frame_age_ms > 0) {
        snprintf(text, sizeof(text), "Stale / %lu ms ago",
                 (unsigned long)diagnostics.last_frame_age_ms);
    } else {
        snprintf(text, sizeof(text), "Waiting for CAN frames");
    }
    lv_label_set_text(s_diagnostics_values[DIAG_LAST_FRAME], text);

    snprintf(text, sizeof(text), "RPM + speed: %s\nGear: %s / Oil: %s",
             diagnostics.drivetrain_live ? "OK" : "WAIT",
             diagnostics.gear_live ? "OK" : "N/A",
             diagnostics.oil_pressure_recent ? "OK" : "N/A");
    lv_label_set_text(s_diagnostics_values[DIAG_SIGNALS], text);

    snprintf(text, sizeof(text), "Bus: %lu / Missed: %lu\nRX counter: %lu / Queued: %lu",
             (unsigned long)diagnostics.bus_error_count,
             (unsigned long)diagnostics.receive_missed_count,
             (unsigned long)diagnostics.rx_error_counter,
             (unsigned long)diagnostics.queued_frames);
    lv_label_set_text(s_diagnostics_values[DIAG_ERRORS], text);
    bool errors = diagnostics.bus_error_count || diagnostics.receive_missed_count ||
                  diagnostics.rx_error_counter;
    lv_obj_set_style_text_color(s_diagnostics_values[DIAG_ERRORS], errors ? C_AMBER : C_GREEN, LV_PART_MAIN);

    const esp_app_desc_t *app = esp_app_get_description();
    if (s_have_last_data && s_last_data.batt_v > 1.0f) {
        snprintf(text, sizeof(text),
                 "FW %s / SD %s\nECU battery: %.1f V / USB V+A: N/A\nReset: %s",
                 app->version, theme_storage_is_available() ? "READY" : "MISSING",
                 (double)s_last_data.batt_v,
                 diagnostics_reset_reason_name(esp_reset_reason()));
    } else {
        snprintf(text, sizeof(text),
                 "FW %s / SD %s\nECU battery: N/A / USB V+A: N/A\nReset: %s",
                 app->version, theme_storage_is_available() ? "READY" : "MISSING",
                 diagnostics_reset_reason_name(esp_reset_reason()));
    }
    lv_label_set_text(s_diagnostics_values[DIAG_SYSTEM], text);

    lv_mem_monitor_t lvgl_memory;
    lv_mem_monitor(&lvgl_memory);
    snprintf(text, sizeof(text), "%lu KB free / %u%% used\nLargest %lu KB / frag %u%%",
             (unsigned long)(lvgl_memory.free_size / 1024), lvgl_memory.used_pct,
             (unsigned long)(lvgl_memory.free_biggest_size / 1024), lvgl_memory.frag_pct);
    lv_label_set_text(s_diagnostics_values[DIAG_LVGL_HEAP], text);

    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    snprintf(text, sizeof(text), "Internal %lu KB / largest %lu KB\nPSRAM %lu KB / largest %lu KB",
             (unsigned long)(internal_free / 1024), (unsigned long)(internal_largest / 1024),
             (unsigned long)(psram_free / 1024), (unsigned long)(psram_largest / 1024));
    lv_label_set_text(s_diagnostics_values[DIAG_MEMORY], text);

    dashboard_runtime_stats_t runtime_stats;
    dashboard_runtime_get_stats(&runtime_stats);
    snprintf(text, sizeof(text), "LVGL %lu B / CAN %lu B\nOdometer %lu B free",
             (unsigned long)runtime_stats.lvgl_stack_margin,
             (unsigned long)runtime_stats.can_stack_margin,
             (unsigned long)runtime_stats.odometer_stack_margin);
    lv_label_set_text(s_diagnostics_values[DIAG_TASK_STACKS], text);

    snprintf(text, sizeof(text), "%lu FPS actual\nRGB565 / PPA preferred / triple partial",
             (unsigned long)runtime_stats.fps);
    lv_label_set_text(s_diagnostics_values[DIAG_DISPLAY], text);
}

static void settings_diagnostics_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    settings_diagnostics_refresh();
}

static void peak_set_text(int index, bool valid, const char *format, double value)
{
    char text[32];
    if (valid) snprintf(text, sizeof(text), format, value);
    else snprintf(text, sizeof(text), "--");
    lv_label_set_text(s_peak_values[index], text);
    lv_obj_set_style_text_color(s_peak_values[index], valid ? C_WHITE : C_LABEL, LV_PART_MAIN);
}

static void settings_peaks_refresh(void)
{
    if (!s_page_peaks) return;
    session_peaks_t peaks;
    session_peaks_get(&peaks);
    bool metric_speed = dash_config_get_speed_kph();
    bool metric_temp = dash_config_get_temperature_celsius();
    bool metric_pressure = dash_config_get_pressure_kpa();
    double speed = metric_speed ? peaks.max_speed_mph * 1.60934 : peaks.max_speed_mph;
    double coolant = metric_temp ? (peaks.max_coolant_f - 32.0) * (5.0 / 9.0) : peaks.max_coolant_f;
    double intake = metric_temp ? (peaks.max_intake_f - 32.0) * (5.0 / 9.0) : peaks.max_intake_f;
    double boost = metric_pressure ? peaks.max_boost_psi * 6.89476 : peaks.max_boost_psi;
    double oil = metric_pressure ? peaks.min_oil_psi * 6.89476 : peaks.min_oil_psi;

    peak_set_text(PEAK_RPM, peaks.has_data, "%.0f RPM", peaks.max_rpm);
    peak_set_text(PEAK_SPEED, peaks.has_data, metric_speed ? "%.1f KPH" : "%.1f MPH", speed);
    peak_set_text(PEAK_BOOST, peaks.has_data, metric_pressure ? "%.1f kPa" : "%.1f PSI", boost);
    peak_set_text(PEAK_COOLANT, peaks.has_data, metric_temp ? "%.1f C" : "%.1f F", coolant);
    peak_set_text(PEAK_INTAKE, peaks.has_data, metric_temp ? "%.1f C" : "%.1f F", intake);
    peak_set_text(PEAK_DUTY, peaks.duty_valid, "%.1f%%", peaks.max_duty_pct);
    peak_set_text(PEAK_KNOCK, peaks.knock_valid, "%.1f deg", peaks.max_knock_deg);
    peak_set_text(PEAK_AFR_MIN, peaks.afr_valid, "%.2f AFR", peaks.min_afr);
    peak_set_text(PEAK_OIL_MIN, peaks.oil_valid, metric_pressure ? "%.1f kPa" : "%.1f PSI", oil);
    peak_set_text(PEAK_BATTERY_MIN, peaks.battery_valid, "%.2f V", peaks.min_battery_v);
}

static void settings_peaks_reset_cb(lv_event_t *event)
{
    (void)event;
    session_peaks_reset();
    settings_peaks_refresh();
}

static void settings_log_format_value(char *buffer, size_t size, size_t series, int32_t value)
{
    const char *unit = s_log_chart_data.series_units[series];
    if (value == INT16_MIN) {
        snprintf(buffer, size, "--");
    } else if (strcmp(unit, "rpm") == 0) {
        snprintf(buffer, size, "%ld rpm", (long)value);
    } else if (strcmp(unit, ":1") == 0 || strcmp(unit, "V") == 0) {
        snprintf(buffer, size, "%.2f %s", (double)value / 100.0, unit);
    } else {
        snprintf(buffer, size, "%.1f %s", (double)value / 10.0, unit);
    }
}

static void settings_log_show_point(size_t point)
{
    if (point >= s_log_chart_data.point_count) return;
    char readout[256];
    unsigned seconds = s_log_chart_data.point_count > 1 ?
        (unsigned)(((uint64_t)s_log_chart_data.duration_ms * point) /
                   (s_log_chart_data.point_count - 1) / 1000) : 0;
    int written = snprintf(readout, sizeof(readout), "%u:%02u", seconds / 60, seconds % 60);
    for (size_t series = 0; series < s_log_chart_data.series_count && written > 0 &&
         (size_t)written < sizeof(readout); ++series) {
        char value[32];
        settings_log_format_value(value, sizeof(value), series, s_log_chart_data.values[series][point]);
        written += snprintf(readout + written, sizeof(readout) - (size_t)written,
                            "   %s %s", s_log_chart_data.series_names[series], value);
        lv_chart_set_cursor_point(s_log_charts[series], s_log_chart_cursors[series],
                                  s_log_chart_series[series], (uint16_t)point);
    }
    lv_label_set_text(s_log_readout_label, readout);
}

static void settings_log_chart_press_cb(lv_event_t *event)
{
    if (s_log_chart_data.point_count == 0) return;
    lv_obj_t *chart = lv_event_get_target(event);
    lv_indev_t *input = lv_indev_get_act();
    if (!input) return;
    lv_point_t touch;
    lv_area_t area;
    lv_indev_get_point(input, &touch);
    lv_obj_get_content_coords(chart, &area);
    int32_t width = lv_area_get_width(&area);
    int32_t relative_x = touch.x - area.x1;
    if (relative_x < 0) relative_x = 0;
    if (relative_x >= width) relative_x = width - 1;
    size_t point = width > 1 ?
        (size_t)(((int64_t)relative_x * (s_log_chart_data.point_count - 1)) / (width - 1)) : 0;
    settings_log_show_point(point);
}

static void settings_log_render_chart(void)
{
    lv_obj_clean(s_log_chart_content);
    memset(s_log_charts, 0, sizeof(s_log_charts));
    int32_t chart_width = (int32_t)s_log_chart_data.point_count * 6;
    if (chart_width < 790) chart_width = 790;
    lv_obj_set_width(s_log_chart_content, chart_width);
    static const uint32_t colors[DEVICE_LOG_MAX_SERIES] = {
        0xe4002b, 0x39ff8c, 0x00c8ff, 0xffb020,
    };
    for (size_t series = 0; series < s_log_chart_data.series_count; ++series) {
        lv_obj_t *chart = lv_chart_create(s_log_chart_content);
        s_log_charts[series] = chart;
        lv_obj_set_size(chart, LV_PCT(100), 64);
        lv_obj_set_style_bg_color(chart, lv_color_hex(0x101216), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(chart, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);
        lv_obj_set_style_line_color(chart, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_line_opa(chart, LV_OPA_50, LV_PART_MAIN);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_div_line_count(chart, 3, 8);
        lv_chart_set_point_count(chart, (uint16_t)s_log_chart_data.point_count);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                           s_log_chart_data.minimums[series], s_log_chart_data.maximums[series]);
        lv_chart_series_t *line = lv_chart_add_series(chart, lv_color_hex(colors[series]),
                                                      LV_CHART_AXIS_PRIMARY_Y);
        s_log_chart_series[series] = line;
        lv_chart_set_ext_y_array(chart, line, s_log_chart_data.values[series]);
        s_log_chart_cursors[series] = lv_chart_add_cursor(chart, C_WHITE, LV_DIR_VER);
        lv_obj_add_event_cb(chart, settings_log_chart_press_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(chart, settings_log_chart_press_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_t *label = make_label(chart, s_log_chart_data.series_names[series], DASH_FONT_LABEL, C_WHITE);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, 4);
    }
    lv_obj_scroll_to_x(s_log_chart_view, 0, LV_ANIM_OFF);
    settings_log_show_point(0);
}

static void settings_log_load_selected(void)
{
    if (s_log_file_count == 0) return;
    uint16_t file_index = lv_dropdown_get_selected(s_log_file_dropdown);
    uint16_t preset_index = lv_dropdown_get_selected(s_log_preset_dropdown);
    if (file_index >= s_log_file_count || preset_index >= DEVICE_LOG_PRESET_COUNT) return;
    lv_label_set_text(s_log_status_label, "Loading log...");
    esp_err_t error = device_log_load_chart(s_log_files[file_index].filename,
                                            (device_log_preset_t)preset_index,
                                            &s_log_chart_data);
    if (error != ESP_OK) {
        char status[64];
        snprintf(status, sizeof(status), "Could not load log: %s", esp_err_to_name(error));
        lv_label_set_text(s_log_status_label, status);
        return;
    }
    char status[96];
    snprintf(status, sizeof(status), "%s   %u samples   %u:%02u",
             s_log_chart_data.filename, (unsigned)s_log_chart_data.source_rows,
             (unsigned)(s_log_chart_data.duration_ms / 60000),
             (unsigned)((s_log_chart_data.duration_ms / 1000) % 60));
    lv_label_set_text(s_log_status_label, status);
    settings_log_render_chart();
}

static void settings_log_selection_cb(lv_event_t *event)
{
    (void)event;
    settings_log_load_selected();
}

static void settings_logs_refresh(void)
{
    s_log_file_count = device_log_list(s_log_files, DEVICE_LOG_MAX_FILES);
    if (s_log_file_count == 0) {
        lv_dropdown_set_options(s_log_file_dropdown, "No logs found");
        lv_label_set_text(s_log_status_label, "No driving logs found on the SD card.");
        lv_obj_clean(s_log_chart_content);
        return;
    }
    static char options[DEVICE_LOG_MAX_FILES * 12];
    size_t used = 0;
    for (size_t index = 0; index < s_log_file_count; ++index) {
        int written = snprintf(options + used, sizeof(options) - used, "%s%s",
                               index ? "\n" : "", s_log_files[index].filename);
        if (written < 0 || (size_t)written >= sizeof(options) - used) break;
        used += (size_t)written;
    }
    lv_dropdown_set_options(s_log_file_dropdown, options);
    lv_dropdown_set_selected(s_log_file_dropdown, 0);
    settings_log_load_selected();
}

static void build_config_section_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label = make_label(parent, title, DASH_FONT_LABEL, C_LABEL_DIM);
    lv_obj_set_style_pad_top(label, 8, LV_PART_MAIN);
}

#define MAX_SIM_BTN_INSTANCES 7
static lv_obj_t *s_sim_btn_instances[MAX_SIM_BTN_INSTANCES];
static int s_sim_btn_count = 0;
static bool s_sim_active = false;
static lv_obj_t *s_sim_mode_modal;
#define MAX_RECORD_BTN_INSTANCES 7
static lv_obj_t *s_record_btn_instances[MAX_RECORD_BTN_INSTANCES];
static int s_record_btn_count;
static int s_record_last_state = -1;
static int64_t s_record_last_toggle_us;
static lv_obj_t *s_record_toast;
static lv_timer_t *s_record_toast_timer;

static void record_toast_close_cb(lv_timer_t *timer)
{
    (void)timer;
    s_record_toast_timer = NULL;
    if (s_record_toast) {
        lv_obj_del(s_record_toast);
        s_record_toast = NULL;
    }
}

static void record_show_toast(const char *title, const char *detail, bool error)
{
    if (s_record_toast_timer) {
        lv_timer_del(s_record_toast_timer);
        s_record_toast_timer = NULL;
    }
    if (s_record_toast) lv_obj_del(s_record_toast);
    s_record_toast = lv_obj_create(s_cluster);
    lv_obj_add_flag(s_record_toast, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_record_toast, 460, 76);
    lv_obj_align(s_record_toast, LV_ALIGN_TOP_MID, 0, 18);
    lv_obj_set_style_bg_color(s_record_toast, error ? C_RED_DEEP : C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_record_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_record_toast, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_record_toast, error ? C_RED : C_GREEN, LV_PART_MAIN);
    lv_obj_set_style_radius(s_record_toast, 10, LV_PART_MAIN);
    lv_obj_clear_flag(s_record_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_record_toast, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_record_toast, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    make_label(s_record_toast, title, DASH_FONT_LABEL14, C_WHITE);
    make_label(s_record_toast, detail, DASH_FONT_LABEL, error ? C_AMBER : C_LABEL);
    lv_obj_move_foreground(s_record_toast);
    s_record_toast_timer = lv_timer_create(record_toast_close_cb, 2200, NULL);
    lv_timer_set_repeat_count(s_record_toast_timer, 1);
}

void honda_dash_ui_show_achievement(const char *name)
{
    if (!name || !s_cluster) return;
    odo_tiles_refresh_display();
    record_show_toast("ACHIEVEMENT UNLOCKED", name, false);
}

static void record_button_apply_visual(lv_obj_t *button, bool recording)
{
    lv_obj_set_style_bg_color(button, recording ? C_RED : C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, recording ? C_RED : C_LINE, LV_PART_MAIN);
}

static void record_buttons_refresh(void)
{
    bool recording = data_logger_is_recording();
    if ((int)recording == s_record_last_state) return;
    s_record_last_state = recording;
    for (int index = 0; index < s_record_btn_count; ++index) {
        record_button_apply_visual(s_record_btn_instances[index], recording);
    }
}

static void record_btn_cb(lv_event_t *event)
{
    (void)event;
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_record_last_toggle_us < 1000000) return;
    s_record_last_toggle_us = now_us;
    char filename[20] = {0};
    esp_err_t err;
    data_logger_note_manual_control();
    if (data_logger_is_recording()) {
        err = data_logger_stop(filename, sizeof(filename));
        if (err == ESP_OK) record_show_toast("LOGGING STOPPED", filename, false);
        else record_show_toast("STOP FAILED", esp_err_to_name(err), true);
    } else {
        err = data_logger_start(filename, sizeof(filename));
        if (err == ESP_OK) record_show_toast("LOGGING STARTED", filename, false);
        else if (err == ESP_ERR_NOT_FOUND) record_show_toast("LOGGING FAILED", "SD card not found", true);
        else record_show_toast("LOGGING FAILED", esp_err_to_name(err), true);
    }
    record_buttons_refresh();
}

static void sim_btn_apply_visual(lv_obj_t *btn)
{
    if (s_sim_active) {
        lv_obj_set_style_border_color(btn, C_RED, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, C_RED, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(btn, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    }
}

static void sim_btn_cb(lv_event_t *e)
{
    s_sim_active = !s_sim_active;
    dash_sim_set_enabled(s_sim_active);
    lv_obj_t *clicked = lv_event_get_target(e);
    if (clicked) sim_btn_apply_visual(clicked);
    for (int i = 0; i < s_sim_btn_count; i++) {
        sim_btn_apply_visual(s_sim_btn_instances[i]);
    }
}

static void sim_mode_modal_close(void)
{
    lv_obj_t *modal = s_sim_mode_modal;
    s_sim_mode_modal = NULL;
    if (modal) {
        ui_fade(modal, lv_obj_get_style_opa(modal, LV_PART_MAIN), LV_OPA_TRANSP,
                120, ui_delete_after_anim_cb);
    }
}

static void sim_mode_cancel_cb(lv_event_t *e)
{
    (void)e;
    sim_mode_modal_close();
}

static void sim_mode_select_cb(lv_event_t *e)
{
    dash_sim_mode_t mode = (dash_sim_mode_t)(intptr_t)lv_event_get_user_data(e);
    dash_sim_set_mode(mode);
    s_sim_active = true;
    for (int i = 0; i < s_sim_btn_count; ++i) {
        sim_btn_apply_visual(s_sim_btn_instances[i]);
    }
    sim_mode_modal_close();
}

static void sim_btn_long_press_cb(lv_event_t *e)
{
    (void)e;
    if (s_sim_mode_modal) return;

    static const char *const mode_names[DASH_SIM_MODE_COUNT] = {
        "Idle", "Cruise", "Full Throttle", "Redline",
    };
    static const char *const mode_details[DASH_SIM_MODE_COUNT] = {
        "850 RPM  /  Neutral",
        "2750 RPM  /  65 MPH",
        "6500 RPM  /  Boost",
        "8550 RPM  /  Limiter",
    };

    s_sim_mode_modal = lv_obj_create(s_cluster);
    lv_obj_add_flag(s_sim_mode_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_sim_mode_modal, SCR_W, SCR_H);
    lv_obj_set_pos(s_sim_mode_modal, 0, 0);
    lv_obj_set_style_bg_color(s_sim_mode_modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sim_mode_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sim_mode_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_sim_mode_modal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_sim_mode_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_sim_mode_modal);
    ui_fade(s_sim_mode_modal, LV_OPA_TRANSP, LV_OPA_COVER, 150, NULL);

    lv_obj_t *panel = lv_obj_create(s_sim_mode_modal);
    lv_obj_set_size(panel, 860, 420);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 24, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 16, LV_PART_MAIN);
    make_label(panel, "SIMULATION MODE", DASH_FONT_LABEL14, C_WHITE);

    lv_obj_t *modes = make_plain_container(panel);
    lv_obj_set_size(modes, LV_PCT(100), 260);
    lv_obj_set_flex_flow(modes, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modes, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modes, 12, LV_PART_MAIN);
    dash_sim_mode_t selected = dash_sim_get_mode();
    for (int index = 0; index < DASH_SIM_MODE_COUNT; ++index) {
        bool active = index == selected;
        lv_obj_t *tile = lv_obj_create(modes);
        lv_obj_set_size(tile, 190, 220);
        lv_obj_set_style_bg_color(tile, active ? C_RED : C_VOID, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, active ? LV_OPA_30 : LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, active ? 3 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, active ? C_RED : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 10, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, sim_mode_select_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)index);
        add_press_feedback(tile);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, 12, LV_PART_MAIN);
        make_label(tile, LV_SYMBOL_CHARGE, DASH_FONT_TILEVAL, active ? C_RED : C_WHITE);
        make_label(tile, mode_names[index], DASH_FONT_LABEL14, C_WHITE);
        lv_obj_t *detail = make_label(tile, mode_details[index], DASH_FONT_LABEL, C_LABEL);
        lv_obj_set_width(detail, 164);
        lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    lv_obj_t *cancel = lv_obj_create(panel);
    lv_obj_set_size(cancel, LV_PCT(100), 52);
    lv_obj_set_style_bg_color(cancel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cancel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cancel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(cancel, 10, LV_PART_MAIN);
    lv_obj_clear_flag(cancel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cancel, sim_mode_cancel_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(cancel);
    lv_obj_center(make_label(cancel, "Cancel", DASH_FONT_LABEL14, C_LABEL));
}

static void sim_buttons_apply_visibility(void)
{
    bool show = dash_config_get_show_sim_button();
    for (int i = 0; i < s_sim_btn_count; i++) {
        if (show) lv_obj_clear_flag(s_sim_btn_instances[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_sim_btn_instances[i], LV_OBJ_FLAG_HIDDEN);
    }
}

typedef enum {
    CRITICAL_WARNING_NONE = 0,
    CRITICAL_WARNING_OIL,
    CRITICAL_WARNING_COOLANT,
    CRITICAL_WARNING_AFR_LEAN,
    CRITICAL_WARNING_KNOCK,
    CRITICAL_WARNING_BATTERY,
    CRITICAL_WARNING_AFR_RICH,
    CRITICAL_WARNING_INTAKE,
    CRITICAL_WARNING_DUTY,
    CRITICAL_WARNING_BOOST,
    CRITICAL_WARNING_CEL,
} critical_warning_t;

static critical_warning_t critical_warning_select(const honda_dash_data_t *data)
{
    if (!data || !canbus_has_live_data() || s_sim_active) return CRITICAL_WARNING_NONE;
    if (data->rpm >= 800 && canbus_has_recent_oil_pressure() &&
        data->oil_psi < dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_OIL_LOW) / 10.0f) {
        return CRITICAL_WARNING_OIL;
    }
    if (data->ect_f >= dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_ECT_HIGH) / 10.0f) {
        return CRITICAL_WARNING_COOLANT;
    }
    if (data->rpm >= 1500 && data->tps_pct >= 20.0f && data->afr > 5.0f &&
        data->afr > dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_AFR_LEAN) / 10.0f) {
        return CRITICAL_WARNING_AFR_LEAN;
    }
    if (data->knock_valid &&
        data->knock_deg >= dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_KNOCK_RED) / 10.0f) {
        return CRITICAL_WARNING_KNOCK;
    }
    if (data->rpm >= 800 && data->batt_v > 1.0f &&
        data->batt_v < dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_BATT_LOW) / 10.0f) {
        return CRITICAL_WARNING_BATTERY;
    }
    if (data->rpm >= 1500 && data->tps_pct >= 20.0f && data->afr > 5.0f &&
        data->afr < dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_AFR_RICH) / 10.0f) {
        return CRITICAL_WARNING_AFR_RICH;
    }
    if (data->iat_f >= dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_IAT_HIGH) / 10.0f) {
        return CRITICAL_WARNING_INTAKE;
    }
    if (data->duty_valid &&
        data->duty_pct >= dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_DUTY_HIGH) / 10.0f) {
        return CRITICAL_WARNING_DUTY;
    }
    if (data->map_psi >= dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_MAP_HIGH) / 10.0f) {
        return CRITICAL_WARNING_BOOST;
    }
    if (data->cel) return CRITICAL_WARNING_CEL;
    return CRITICAL_WARNING_NONE;
}

static void critical_warning_set_text(critical_warning_t warning, const honda_dash_data_t *data)
{
    const char *title = "ENGINE WARNING";
    char detail[48];
    switch (warning) {
        case CRITICAL_WARNING_OIL: {
            float value = dash_config_get_pressure_kpa() ? data->oil_psi * 6.89476f : data->oil_psi;
            title = "LOW OIL PRESSURE";
            snprintf(detail, sizeof(detail), "%.0f %s", (double)value,
                     dash_config_get_pressure_kpa() ? "kPa" : "PSI");
            break;
        }
        case CRITICAL_WARNING_COOLANT: {
            float value = dash_config_get_temperature_celsius() ?
                          (data->ect_f - 32.0f) * (5.0f / 9.0f) : data->ect_f;
            title = "HIGH COOLANT TEMP";
            snprintf(detail, sizeof(detail), "%.0f %s", (double)value,
                     dash_config_get_temperature_celsius() ? "C" : "F");
            break;
        }
        case CRITICAL_WARNING_AFR_LEAN:
            title = "LEAN AFR";
            snprintf(detail, sizeof(detail), "%.1f :1", (double)data->afr);
            break;
        case CRITICAL_WARNING_KNOCK:
            title = "KNOCK DETECTED";
            snprintf(detail, sizeof(detail), "%.1f deg", (double)data->knock_deg);
            break;
        case CRITICAL_WARNING_BATTERY:
            title = "LOW BATTERY VOLTAGE";
            snprintf(detail, sizeof(detail), "%.1f V", (double)data->batt_v);
            break;
        case CRITICAL_WARNING_AFR_RICH:
            title = "RICH AFR";
            snprintf(detail, sizeof(detail), "%.1f :1", (double)data->afr);
            break;
        case CRITICAL_WARNING_INTAKE: {
            float value = dash_config_get_temperature_celsius() ?
                          (data->iat_f - 32.0f) * (5.0f / 9.0f) : data->iat_f;
            title = "HIGH INTAKE TEMP";
            snprintf(detail, sizeof(detail), "%.0f %s", (double)value,
                     dash_config_get_temperature_celsius() ? "C" : "F");
            break;
        }
        case CRITICAL_WARNING_DUTY:
            title = "HIGH INJECTOR DUTY";
            snprintf(detail, sizeof(detail), "%.0f%%", (double)data->duty_pct);
            break;
        case CRITICAL_WARNING_BOOST: {
            float value = dash_config_get_pressure_kpa() ? data->map_psi * 6.89476f : data->map_psi;
            title = "BOOST LIMIT";
            snprintf(detail, sizeof(detail), "%.1f %s", (double)value,
                     dash_config_get_pressure_kpa() ? "kPa" : "PSI");
            break;
        }
        case CRITICAL_WARNING_CEL:
            title = "CHECK ENGINE";
            snprintf(detail, sizeof(detail), "ECU fault active");
            break;
        default:
            snprintf(detail, sizeof(detail), "");
            break;
    }
    lv_label_set_text(s_warning_title, title);
    lv_label_set_text(s_warning_detail, detail);
}

static void critical_warning_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    canbus_diagnostics_t diagnostics;
    canbus_get_diagnostics(&diagnostics);
    if (!s_sim_active && !diagnostics.live_data) {
        const char *status = diagnostics.last_frame_age_ms > 0 ? "ECU DATA STALE" : "ECU DISCONNECTED";
        if (strcmp(lv_label_get_text(s_can_status_label), status) != 0) {
            lv_label_set_text(s_can_status_label, status);
        }
        bool was_hidden = lv_obj_has_flag(s_can_status_badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_can_status_badge, LV_OBJ_FLAG_HIDDEN);
        if (was_hidden && (!s_settings_overlay ||
                lv_obj_has_flag(s_settings_overlay, LV_OBJ_FLAG_HIDDEN))) {
            lv_obj_move_foreground(s_can_status_badge);
        }
    } else {
        lv_obj_add_flag(s_can_status_badge, LV_OBJ_FLAG_HIDDEN);
    }

    if (data_logger_is_recording()) {
        char filename[24] = {0};
        uint32_t elapsed_seconds = 0;
        if (data_logger_get_status(filename, sizeof(filename), &elapsed_seconds)) {
            char recording_text[56];
            snprintf(recording_text, sizeof(recording_text), "REC  %s  %02lu:%02lu",
                     filename, (unsigned long)(elapsed_seconds / 60),
                     (unsigned long)(elapsed_seconds % 60));
            if (strcmp(lv_label_get_text(s_record_status_label), recording_text) != 0) {
                lv_label_set_text(s_record_status_label, recording_text);
            }
        }
        bool was_hidden = lv_obj_has_flag(s_record_status_badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_record_status_badge, LV_OBJ_FLAG_HIDDEN);
        if (was_hidden && (!s_settings_overlay ||
                lv_obj_has_flag(s_settings_overlay, LV_OBJ_FLAG_HIDDEN))) {
            lv_obj_move_foreground(s_record_status_badge);
        }
    } else {
        lv_obj_add_flag(s_record_status_badge, LV_OBJ_FLAG_HIDDEN);
    }

    static critical_warning_t candidate;
    static critical_warning_t active;
    static int64_t candidate_since_us;
    static int64_t clear_since_us;
    int64_t now_us = esp_timer_get_time();
    critical_warning_t selected = s_warning_have_data ?
                                  critical_warning_select(&s_warning_data) : CRITICAL_WARNING_NONE;

    if (selected == CRITICAL_WARNING_NONE) {
        candidate = CRITICAL_WARNING_NONE;
        candidate_since_us = 0;
        if (active != CRITICAL_WARNING_NONE) {
            if (clear_since_us == 0) clear_since_us = now_us;
            if (now_us - clear_since_us >= 1200000) {
                active = CRITICAL_WARNING_NONE;
                lv_obj_add_flag(s_warning_banner, LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    clear_since_us = 0;
    if (selected != candidate) {
        candidate = selected;
        candidate_since_us = now_us;
    }
    if (active != selected && now_us - candidate_since_us >= 800000) {
        active = selected;
        lv_obj_clear_flag(s_warning_banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_warning_banner);
        if (s_trip_reset_modal) {
            lv_obj_move_foreground(s_trip_reset_modal);
        } else if (s_settings_overlay && !lv_obj_has_flag(s_settings_overlay, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_move_foreground(s_settings_overlay);
        }
    }
    if (active == selected) critical_warning_set_text(active, &s_warning_data);
}

static void build_critical_warning_banner(lv_obj_t *cluster)
{
    s_can_status_badge = lv_obj_create(cluster);
    lv_obj_add_flag(s_can_status_badge, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_can_status_badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_can_status_badge, 180, 32);
    lv_obj_align(s_can_status_badge, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_color(s_can_status_badge, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_can_status_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_can_status_badge, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_can_status_badge, C_AMBER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_can_status_badge, 5, LV_PART_MAIN);
    s_can_status_label = make_label(s_can_status_badge, "ECU DISCONNECTED", DASH_FONT_LABEL, C_AMBER);
    lv_obj_center(s_can_status_label);

    s_record_status_badge = lv_obj_create(cluster);
    lv_obj_add_flag(s_record_status_badge, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_record_status_badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_record_status_badge, 250, 32);
    lv_obj_align(s_record_status_badge, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_set_style_bg_color(s_record_status_badge, C_RED_DEEP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_record_status_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_record_status_badge, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_record_status_badge, C_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(s_record_status_badge, 5, LV_PART_MAIN);
    s_record_status_label = make_label(s_record_status_badge, "REC", DASH_FONT_LABEL, C_WHITE);
    lv_obj_center(s_record_status_label);

    s_warning_banner = lv_obj_create(cluster);
    lv_obj_add_flag(s_warning_banner, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_warning_banner, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_warning_banner, 520, 62);
    lv_obj_align(s_warning_banner, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(s_warning_banner, C_RED_DEEP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_warning_banner, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_warning_banner, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_warning_banner, C_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(s_warning_banner, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_warning_banner, 18, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_warning_banner, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_warning_banner, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_warning_title = make_label(s_warning_banner, "ENGINE WARNING", DASH_FONT_LABEL14, C_WHITE);
    s_warning_detail = make_label(s_warning_banner, "", DASH_FONT_LABEL14, C_AMBER);
    lv_timer_create(critical_warning_timer_cb, 100, NULL);
}

static lv_color_t shift_light_color(dash_config_shift_color_t color)
{
    static const uint32_t colors[DASH_CONFIG_SHIFT_COLOR_COUNT] = {
        0x39ff8c, 0xffe066, 0xff8c1a, 0xe4002b,
        0x4d8fff, 0x00efff, 0xffffff, 0xf472b6,
    };
    if (color < 0 || color >= DASH_CONFIG_SHIFT_COLOR_COUNT) color = DASH_CONFIG_SHIFT_COLOR_RED;
    return lv_color_hex(colors[color]);
}

static int shift_light_stage_for_rpm(int rpm, const int thresholds[DASH_CONFIG_SHIFT_STAGE_COUNT])
{
    if (rpm >= thresholds[2]) return 2;
    if (rpm >= thresholds[1]) return 1;
    if (rpm >= thresholds[0]) return 0;
    return -1;
}

void honda_dash_ui_update_shift_lights(uint16_t rpm)
{
    if (s_active_theme >= 100) return;

    int thresholds[DASH_CONFIG_SHIFT_STAGE_COUNT] = {
        dash_config_get_shift_stage_rpm(0),
        dash_config_get_shift_stage_rpm(1),
        dash_config_get_shift_stage_rpm(2),
    };
    int segment_count = s_active_theme == THEME_ID_MODERN ? SEG_COUNT :
                        (s_active_theme == THEME_ID_RACE_LCD ? RPK_TACH_COUNT :
                        (s_active_theme == THEME_ID_ENDURANCE ? END_SEG_COUNT :
                        (s_active_theme == THEME_ID_TOURING ? TOUR_SEG_COUNT : 0)));
    int fill_rpm = s_active_theme == THEME_ID_TOURING ? dash_config_get_redline_rpm() : MAXRPM;
    int bucket = segment_count > 0 ? ((int)rpm * segment_count) / (fill_rpm > 0 ? fill_rpm : 1) :
                 shift_light_stage_for_rpm((int)rpm, thresholds);
    if (segment_count > 0) {
        if (bucket < 0) bucket = 0;
        if (bucket > segment_count) bucket = segment_count;
    }

    int enabled = dash_config_get_shift_light_enabled() ? 1 : 0;
    bool flashing = enabled && dash_config_get_shift_redline_flash() &&
                    rpm >= dash_config_get_redline_rpm();
    int flash_on = flashing ? (int)((esp_timer_get_time() / 125000) & 1) : 1;
    int brightness = dash_config_get_shift_light_brightness();
    int colors[DASH_CONFIG_SHIFT_STAGE_COUNT];
    bool style_changed = enabled != s_shift_light_last_enabled ||
                         brightness != s_shift_light_last_brightness;
    for (int stage = 0; stage < DASH_CONFIG_SHIFT_STAGE_COUNT; ++stage) {
        colors[stage] = dash_config_get_shift_stage_color(stage);
        style_changed = style_changed || thresholds[stage] != s_shift_light_last_rpms[stage] ||
                        colors[stage] != s_shift_light_last_colors[stage];
    }
    if (s_active_theme == s_shift_light_last_theme && bucket == s_shift_light_last_bucket &&
            flash_on == s_shift_light_last_flash_on && !style_changed) return;

    s_shift_light_last_theme = s_active_theme;
    s_shift_light_last_bucket = bucket;
    s_shift_light_last_enabled = enabled;
    s_shift_light_last_flash_on = flash_on;
    s_shift_light_last_brightness = brightness;
    memcpy(s_shift_light_last_rpms, thresholds, sizeof(thresholds));
    memcpy(s_shift_light_last_colors, colors, sizeof(colors));
    lv_opa_t active_opa = (lv_opa_t)((brightness * 255) / 100);
    int current_stage = enabled ? shift_light_stage_for_rpm((int)rpm, thresholds) : -1;
    lv_color_t current_color = flashing ?
        shift_light_color((dash_config_shift_color_t)colors[2]) :
        (current_stage >= 0 ? shift_light_color((dash_config_shift_color_t)colors[current_stage]) : C_WHITE);

    if (s_active_theme == THEME_ID_MODERN) {
        for (int index = 0; index < SEG_COUNT; ++index) {
            bool active = flashing ? flash_on : index < bucket;
            lv_obj_set_style_bg_color(s_segs[index], active ? current_color : C_SEG_OFF, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_segs[index], active ? active_opa : LV_OPA_COVER, LV_PART_MAIN);
        }
        lv_obj_set_style_bg_color(s_rpm_mini_fill, flash_on ? current_color : C_SEG_OFF, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_rpm_mini_fill, flash_on ? active_opa : LV_OPA_30, LV_PART_INDICATOR);
    } else if (s_active_theme == THEME_ID_RACE_LCD) {
        for (int index = 0; index < RPK_TACH_COUNT; ++index) {
            bool active = index < bucket;
            lv_obj_set_style_bg_color(s_rpk_tach[index], active ? lv_color_hex(0x021c30) : lv_color_hex(0x087dbb),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_rpk_tach[index], active ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
        }
        int active_lamps = current_stage < 0 ? 0 : (current_stage == 0 ? 4 : (current_stage == 1 ? 7 : 10));
        for (int index = 0; index < RPK_LAMP_COUNT; ++index) {
            bool active = enabled && (flashing ? flash_on : index < active_lamps);
            lv_obj_set_style_bg_color(s_rpk_lamps[index], active ? current_color : lv_color_hex(0x252a2c),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_rpk_lamps[index], active ? active_opa : LV_OPA_COVER, LV_PART_MAIN);
        }
    } else if (s_active_theme == THEME_ID_ENDURANCE) {
        for (int index = 0; index < END_SEG_COUNT; ++index) {
            bool active = flashing ? flash_on : index < bucket;
            lv_obj_set_style_bg_color(s_end_segments[index], active ? current_color : HUNTER_SEG_OFF,
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_end_segments[index], active ? active_opa : LV_OPA_COVER, LV_PART_MAIN);
        }
        bool shift_indicator_on = enabled && current_stage >= 2 && (!flashing || flash_on);
        lv_obj_set_style_bg_color(s_end_shift_light, shift_indicator_on ? current_color : C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_end_shift_light, shift_indicator_on ? current_color : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_end_shift_label, shift_indicator_on ? C_WHITE : C_LABEL_DIM, LV_PART_MAIN);
    } else if (s_active_theme == THEME_ID_TOURING) {
        int active_pairs = (bucket + 1) / 2;
        for (int index = 0; index < TOUR_SEG_COUNT; ++index) {
            bool active = flashing ? flash_on : (index / 2) < active_pairs;
            lv_obj_set_style_bg_color(s_tour_segments[index], active ? current_color : RALLY_LIGHT_OFF,
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_tour_segments[index], active ? active_opa : LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_shadow_color(s_tour_segments[index], current_color, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(s_tour_segments[index], active ? active_opa / 2 : LV_OPA_TRANSP,
                                        LV_PART_MAIN);
        }
    } else if (s_active_theme == THEME_ID_HALDASH) {
        lv_obj_set_style_arc_color(s_hal_rpm_arc, current_color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(s_hal_rpm_arc, flash_on ? active_opa : LV_OPA_20, LV_PART_INDICATOR);
    }
}

static void build_settings_button(lv_obj_t *parent, bool rectangular)
{
    /* wraps the controls together so they occupy the same grid slot
       every theme's bottom strip already reserves for the settings
       button, and share one vertical-position fix */
    lv_obj_t *cluster = make_plain_container(parent);
    lv_obj_set_size(cluster, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(cluster, LV_GRID_ALIGN_CENTER, rectangular ? 4 : 2, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(cluster, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cluster, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cluster, 12, LV_PART_MAIN);
    /* bottom-aligned within its grid row (see LV_GRID_ALIGN_END above) --
       the strip containers were also given a bit of extra height so this
       has genuine room instead of clipping into the strip's own edge */
    lv_obj_move_foreground(cluster);

    /* --- simulation toggle: drives every gauge with smooth random
       values standing in for real CAN data, for quick FPS/CPU checks --- */
    lv_coord_t button_width = rectangular ? 62 : 40;
    lv_coord_t button_height = rectangular ? 34 : 40;
    lv_coord_t button_radius = rectangular ? 5 : LV_RADIUS_CIRCLE;

    lv_obj_t *sim_btn = lv_obj_create(cluster);
    lv_obj_set_size(sim_btn, button_width, button_height);
    lv_obj_set_style_radius(sim_btn, button_radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(sim_btn, 1, LV_PART_MAIN);
    lv_obj_clear_flag(sim_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sim_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sim_btn, sim_btn_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(sim_btn, sim_btn_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    add_press_feedback(sim_btn);
    lv_obj_t *sim_label = lv_label_create(sim_btn);
    lv_label_set_text(sim_label, "SIM");
    lv_obj_set_style_text_font(sim_label, DASH_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(sim_label, C_WHITE, LV_PART_MAIN);
    lv_obj_center(sim_label);
    sim_btn_apply_visual(sim_btn);
    if (s_sim_btn_count < MAX_SIM_BTN_INSTANCES) {
        s_sim_btn_instances[s_sim_btn_count++] = sim_btn;
    }
    if (!dash_config_get_show_sim_button()) {
        lv_obj_add_flag(sim_btn, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *record_btn = lv_obj_create(cluster);
    lv_obj_set_size(record_btn, button_width, button_height);
    lv_obj_set_style_radius(record_btn, button_radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(record_btn, 1, LV_PART_MAIN);
    lv_obj_clear_flag(record_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(record_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(record_btn, record_btn_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(record_btn);
    lv_obj_t *record_label = lv_label_create(record_btn);
    lv_label_set_text(record_label, "REC");
    lv_obj_set_style_text_font(record_label, DASH_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(record_label, C_WHITE, LV_PART_MAIN);
    lv_obj_center(record_label);
    if (s_record_btn_count < MAX_RECORD_BTN_INSTANCES) {
        s_record_btn_instances[s_record_btn_count++] = record_btn;
    }
    record_button_apply_visual(record_btn, data_logger_is_recording());

    /* --- settings button --- */
    lv_obj_t *btn = lv_obj_create(cluster);
    lv_obj_set_size(btn, button_width, button_height);
    lv_obj_set_style_radius(btn, button_radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, C_LINE, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, settings_btn_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(btn, settings_btn_cb, LV_EVENT_LONG_PRESSED, NULL);
    add_press_feedback(btn);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(icon, DASH_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, C_LABEL, LV_PART_MAIN);
    lv_obj_center(icon);
}

static void build_settings_overlay(lv_obj_t *cluster)
{
    s_settings_overlay = lv_obj_create(cluster);
    lv_obj_add_flag(s_settings_overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(s_settings_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_settings_overlay, SCR_W, SCR_H);
    lv_obj_set_pos(s_settings_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_settings_overlay, lv_color_hex(0x040405), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_settings_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_settings_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_settings_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_settings_overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_settings_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_settings_overlay);
    lv_obj_set_size(panel, 920, 540);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(panel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 24, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- page: main ---- */
    s_page_main = make_plain_container(panel);
    lv_obj_set_size(s_page_main, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_page_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page_main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_page_main, 12, LV_PART_MAIN);
    make_label(s_page_main, "SETTINGS", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *tiles = make_plain_container(s_page_main);
    lv_obj_set_size(tiles, 842, 316);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tiles, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_row(tiles, 16, LV_PART_MAIN);
    build_settings_tile(tiles, LV_SYMBOL_IMAGE, "Theme", settings_open_theme_cb);
    build_settings_tile(tiles, LV_SYMBOL_EYE_OPEN, "Display", settings_open_display_cb);
    build_settings_tile(tiles, LV_SYMBOL_LIST, "Logs", settings_open_logs_menu_cb);
    build_settings_tile(tiles, LV_SYMBOL_CHARGE, "ECU", settings_open_ecu_cb);
    build_settings_tile(tiles, LV_SYMBOL_CHARGE, "Engine Limits", settings_open_engine_limits_cb);
    build_settings_tile(tiles, LV_SYMBOL_SETTINGS, "System", settings_open_config_cb);

    /* ---- page: theme ---- */
    s_page_theme = make_plain_container(panel);
    lv_obj_set_size(s_page_theme, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_theme, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_theme, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page_theme, 14, LV_PART_MAIN);
    make_label(s_page_theme, "THEME", DASH_FONT_LABEL14, C_LABEL);

    s_theme_grid = lv_obj_create(s_page_theme);
    lv_obj_set_size(s_theme_grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(s_theme_grid, 1);
    lv_obj_set_style_bg_opa(s_theme_grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_theme_grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_theme_grid, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_theme_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_theme_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_theme_grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(s_theme_grid, 8, LV_PART_MAIN);
    static lv_coord_t theme_grid_col[3];
    static lv_coord_t theme_grid_row[18];
    theme_grid_col[0] = LV_GRID_FR(1); theme_grid_col[1] = LV_GRID_FR(1);
    theme_grid_col[2] = LV_GRID_TEMPLATE_LAST;
    size_t sd_theme_count = theme_storage_get_count();
    size_t theme_card_count = 5 + sd_theme_count;
    size_t theme_row_count = (theme_card_count + 1) / 2;
    for (size_t row = 0; row < theme_row_count; ++row) {
        theme_grid_row[row] = 180;
    }
    theme_grid_row[theme_row_count] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(s_theme_grid, theme_grid_col, theme_grid_row);
    lv_obj_set_style_pad_column(s_theme_grid, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_theme_grid, 14, LV_PART_MAIN);

    s_theme_card_modern  = build_theme_grid_card(s_theme_grid, "MackoDash V1", "Built in", THEME_ID_MODERN, 0, 0, true);
    s_theme_card_race_lcd = build_theme_grid_card(s_theme_grid, "Race LCD", "Built in", THEME_ID_RACE_LCD, 1, 0, true);
    s_theme_card_haldash = build_theme_grid_card(s_theme_grid, "HalDash", "Built in", THEME_ID_HALDASH, 0, 1, true);
    s_theme_card_endurance = build_theme_grid_card(s_theme_grid, "HunterDash", "Built in", THEME_ID_ENDURANCE, 1, 1, true);
    s_theme_card_touring = build_theme_grid_card(s_theme_grid, "Rally Stage", "Built in", THEME_ID_TOURING, 0, 2, true);
    theme_card_set_selected(s_theme_card_modern, true);

    for (size_t i = 0; i < sd_theme_count; ++i) {
        const theme_storage_package_t *package = theme_storage_get_package(i);
        if (!package) continue;
        size_t card_index = i + 5;
        s_theme_card_sd[i] = build_theme_grid_card(s_theme_grid, package->display_name, package->status,
                      100 + (int)i, (int)(card_index % 2), (int)(card_index / 2),
                      package->manifest_valid);
        add_theme_delete_button(s_theme_card_sd[i]);
    }

    lv_obj_t *logs_menu_content = build_config_subpage(panel, &s_page_logs_menu, "LOGS");
    build_config_menu_row(logs_menu_content, "Driving Logs", settings_open_logs_cb);
    build_config_menu_row(logs_menu_content, "Session Peaks", settings_open_peaks_cb);
    build_config_menu_row(logs_menu_content, "Achievements", settings_open_achievements_cb);

    lv_obj_t *theme_btn_row = make_plain_container(s_page_theme);
    lv_obj_set_size(theme_btn_row, LV_PCT(100), 60);
    lv_obj_set_flex_flow(theme_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(theme_btn_row, 14, LV_PART_MAIN);

    lv_obj_t *theme_set_btn = lv_obj_create(theme_btn_row);
    lv_obj_set_flex_grow(theme_set_btn, 1);
    lv_obj_set_height(theme_set_btn, 60);
    lv_obj_set_style_bg_color(theme_set_btn, C_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(theme_set_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(theme_set_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(theme_set_btn, 12, LV_PART_MAIN);
    lv_obj_clear_flag(theme_set_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(theme_set_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(theme_set_btn, theme_set_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(theme_set_btn);
    lv_obj_t *theme_set_lbl = make_label(theme_set_btn, "Set", DASH_FONT_LABEL14, C_WHITE);
    lv_obj_center(theme_set_lbl);

    /* ---- page: firmware update ---- */
    s_page_update = make_plain_container(panel);
    lv_obj_set_size(s_page_update, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_update, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_update, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page_update, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_page_update, 12, LV_PART_MAIN);

    make_label(s_page_update, "FIRMWARE UPDATE [DBG0721]", DASH_FONT_LABEL14, C_LABEL);
    make_label(s_page_update, "Join OTA Wi-Fi on your phone, then scan QR to open uploader.", DASH_FONT_LABEL14, C_WHITE);

    s_update_status_label = make_label(s_page_update, "Update Server: STOPPED", DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_width(s_update_status_label, LV_PCT(96));
    lv_label_set_long_mode(s_update_status_label, LV_LABEL_LONG_WRAP);
    s_update_details_label = make_label(s_page_update, "", DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_width(s_update_details_label, LV_PCT(96));
    lv_label_set_long_mode(s_update_details_label, LV_LABEL_LONG_WRAP);

    s_update_qr = lv_qrcode_create(s_page_update, 170, lv_color_hex(0xf4f3ef), lv_color_hex(0x151619));
    lv_obj_set_style_border_width(s_update_qr, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_update_qr, lv_color_hex(0x2a2c31), LV_PART_MAIN);

    lv_obj_t *upd_btn_row = make_plain_container(s_page_update);
    lv_obj_set_size(upd_btn_row, LV_PCT(100), 60);
    lv_obj_set_flex_flow(upd_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(upd_btn_row, 14, LV_PART_MAIN);

    lv_obj_t *upd_start_btn = lv_obj_create(upd_btn_row);
    lv_obj_set_flex_grow(upd_start_btn, 1);
    lv_obj_set_height(upd_start_btn, 60);
    lv_obj_set_style_bg_color(upd_start_btn, C_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(upd_start_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(upd_start_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(upd_start_btn, 12, LV_PART_MAIN);
    lv_obj_clear_flag(upd_start_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(upd_start_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(upd_start_btn, settings_start_update_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(upd_start_btn);
    lv_obj_t *upd_start_lbl = make_label(upd_start_btn, "Start", DASH_FONT_LABEL14, C_WHITE);
    lv_obj_center(upd_start_lbl);

    lv_obj_t *upd_stop_btn = lv_obj_create(upd_btn_row);
    lv_obj_set_flex_grow(upd_stop_btn, 1);
    lv_obj_set_height(upd_stop_btn, 60);
    lv_obj_set_style_bg_color(upd_stop_btn, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(upd_stop_btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(upd_stop_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(upd_stop_btn, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(upd_stop_btn, 12, LV_PART_MAIN);
    lv_obj_clear_flag(upd_stop_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(upd_stop_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(upd_stop_btn, settings_stop_update_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(upd_stop_btn);
    lv_obj_t *upd_stop_lbl = make_label(upd_stop_btn, "Stop", DASH_FONT_LABEL14, C_LABEL);
    lv_obj_center(upd_stop_lbl);

    settings_update_refresh_ui();

    /* ---- page: contact and support ---- */
    s_page_contact = make_plain_container(panel);
    lv_obj_set_size(s_page_contact, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_contact, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_contact, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_page_contact, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_page_contact, 12, LV_PART_MAIN);
    make_label(s_page_contact, "CONTACT & SUPPORT", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *contact_selector = lv_dropdown_create(s_page_contact);
    lv_obj_set_size(contact_selector, 330, 44);
    lv_dropdown_set_options(contact_selector,
                            "Report a Problem\nEmail\nInstagram\nTikTok\nLeave Feedback / Review");
    lv_obj_add_event_cb(contact_selector, settings_contact_select_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    s_contact_qr = lv_qrcode_create(s_page_contact, 220,
                                    lv_color_hex(0xf4f3ef), lv_color_hex(0x151619));
    lv_obj_set_style_border_width(s_contact_qr, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_contact_qr, C_LINE, LV_PART_MAIN);

    s_contact_destination_label = make_label(s_page_contact, "", DASH_FONT_LABEL, C_WHITE);
    lv_obj_set_width(s_contact_destination_label, LV_PCT(96));
    lv_label_set_long_mode(s_contact_destination_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_contact_destination_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *support_note = make_label(s_page_contact,
        "For problems, include the vehicle, ECU, selected protocol, firmware version, what happened, and a clear photo or video.",
        DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_width(support_note, LV_PCT(90));
    lv_label_set_long_mode(support_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(support_note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    settings_contact_show(0);

    /* ---- page: info ---- */
    s_page_info = make_plain_container(panel);
    lv_obj_set_size(s_page_info, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_info, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page_info, 10, LV_PART_MAIN);
    make_label(s_page_info, "DIAGNOSTICS", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *diagnostics_grid = make_plain_container(s_page_info);
    lv_obj_set_size(diagnostics_grid, LV_PCT(100), 342);
    lv_obj_set_flex_flow(diagnostics_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(diagnostics_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(diagnostics_grid, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(diagnostics_grid, 10, LV_PART_MAIN);
    lv_obj_set_scroll_dir(diagnostics_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(diagnostics_grid, LV_SCROLLBAR_MODE_AUTO);
    s_diagnostics_values[DIAG_CAN_BUS] = build_diagnostics_tile(diagnostics_grid, "CAN BUS");
    s_diagnostics_values[DIAG_PROTOCOL] = build_diagnostics_tile(diagnostics_grid, "PROTOCOL");
    s_diagnostics_values[DIAG_LAST_FRAME] = build_diagnostics_tile(diagnostics_grid, "LAST FRAME");
    s_diagnostics_values[DIAG_SIGNALS] = build_diagnostics_tile(diagnostics_grid, "SIGNALS");
    s_diagnostics_values[DIAG_ERRORS] = build_diagnostics_tile(diagnostics_grid, "ERRORS");
    s_diagnostics_values[DIAG_SYSTEM] = build_diagnostics_tile(diagnostics_grid, "SYSTEM");
    s_diagnostics_values[DIAG_LVGL_HEAP] = build_diagnostics_tile(diagnostics_grid, "LVGL HEAP");
    s_diagnostics_values[DIAG_MEMORY] = build_diagnostics_tile(diagnostics_grid, "MEMORY");
    s_diagnostics_values[DIAG_TASK_STACKS] = build_diagnostics_tile(diagnostics_grid, "TASK STACK MARGINS");
    s_diagnostics_values[DIAG_DISPLAY] = build_diagnostics_tile(diagnostics_grid, "DISPLAY");
    s_diagnostics_timer = lv_timer_create(settings_diagnostics_timer_cb, 1000, NULL);

    s_page_peaks = make_plain_container(panel);
    lv_obj_set_size(s_page_peaks, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_peaks, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_peaks, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page_peaks, 10, LV_PART_MAIN);
    make_label(s_page_peaks, "SESSION PEAKS", DASH_FONT_LABEL14, C_LABEL);

    s_peaks_scroll = lv_obj_create(s_page_peaks);
    lv_obj_set_size(s_peaks_scroll, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(s_peaks_scroll, 1);
    lv_obj_set_style_bg_opa(s_peaks_scroll, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_peaks_scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_peaks_scroll, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_peaks_scroll, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_peaks_scroll, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s_peaks_scroll, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_peaks_scroll, 10, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_peaks_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_peaks_scroll, LV_SCROLLBAR_MODE_AUTO);
    s_peak_values[PEAK_RPM] = build_diagnostics_tile(s_peaks_scroll, "MAX RPM");
    s_peak_values[PEAK_SPEED] = build_diagnostics_tile(s_peaks_scroll, "MAX SPEED");
    s_peak_values[PEAK_BOOST] = build_diagnostics_tile(s_peaks_scroll, "MAX BOOST / MAP");
    s_peak_values[PEAK_COOLANT] = build_diagnostics_tile(s_peaks_scroll, "MAX COOLANT");
    s_peak_values[PEAK_INTAKE] = build_diagnostics_tile(s_peaks_scroll, "MAX INTAKE TEMP");
    s_peak_values[PEAK_DUTY] = build_diagnostics_tile(s_peaks_scroll, "MAX INJECTOR DUTY");
    s_peak_values[PEAK_KNOCK] = build_diagnostics_tile(s_peaks_scroll, "MAX KNOCK");
    s_peak_values[PEAK_AFR_MIN] = build_diagnostics_tile(s_peaks_scroll, "MIN AFR");
    s_peak_values[PEAK_OIL_MIN] = build_diagnostics_tile(s_peaks_scroll, "MIN OIL PRESSURE");
    s_peak_values[PEAK_BATTERY_MIN] = build_diagnostics_tile(s_peaks_scroll, "MIN BATTERY");

    lv_obj_t *peaks_actions = make_plain_container(s_page_peaks);
    lv_obj_set_size(peaks_actions, LV_PCT(100), 60);
    lv_obj_set_flex_flow(peaks_actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(peaks_actions, 14, LV_PART_MAIN);
    lv_obj_t *peaks_reset = lv_obj_create(peaks_actions);
    lv_obj_set_flex_grow(peaks_reset, 1);
    lv_obj_set_size(peaks_reset, 100, 60);
    lv_obj_set_style_bg_color(peaks_reset, C_RED_DEEP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(peaks_reset, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(peaks_reset, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(peaks_reset, C_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(peaks_reset, 12, LV_PART_MAIN);
    lv_obj_clear_flag(peaks_reset, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(peaks_reset, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(peaks_reset, settings_peaks_reset_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(peaks_reset);
    lv_obj_center(make_label(peaks_reset, "Reset Session", DASH_FONT_LABEL14, C_WHITE));

    s_page_logs = make_plain_container(panel);
    lv_obj_set_size(s_page_logs, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_logs, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_logs, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page_logs, 8, LV_PART_MAIN);
    make_label(s_page_logs, "DRIVING LOGS", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *log_controls = make_plain_container(s_page_logs);
    lv_obj_set_size(log_controls, LV_PCT(100), 48);
    lv_obj_set_flex_flow(log_controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(log_controls, 10, LV_PART_MAIN);
    s_log_file_dropdown = lv_dropdown_create(log_controls);
    lv_obj_set_flex_grow(s_log_file_dropdown, 1);
    lv_obj_set_height(s_log_file_dropdown, 44);
    lv_dropdown_set_options(s_log_file_dropdown, "No logs found");
    lv_obj_add_event_cb(s_log_file_dropdown, settings_log_selection_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_log_preset_dropdown = lv_dropdown_create(log_controls);
    lv_obj_set_flex_grow(s_log_preset_dropdown, 1);
    lv_obj_set_height(s_log_preset_dropdown, 44);
    lv_dropdown_set_options(s_log_preset_dropdown,
                            "AFR / Timing / Boost\nBoost / Timing\nTemps / RPM / Boost\nEngine Health");
    lv_obj_add_event_cb(s_log_preset_dropdown, settings_log_selection_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *auto_record_control = make_plain_container(log_controls);
    lv_obj_set_size(auto_record_control, 190, 44);
    lv_obj_set_flex_flow(auto_record_control, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(auto_record_control, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(auto_record_control, "Auto Record", DASH_FONT_LABEL, C_LABEL);
    s_cfg_auto_record_switch = lv_switch_create(auto_record_control);
    lv_obj_set_style_bg_color(s_cfg_auto_record_switch, C_RED,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (dash_config_get_auto_record()) lv_obj_add_state(s_cfg_auto_record_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_cfg_auto_record_switch, cfg_auto_record_switch_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *log_name_control = make_plain_container(s_page_logs);
    lv_obj_set_size(log_name_control, LV_PCT(100), 44);
    lv_obj_set_flex_flow(log_name_control, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(log_name_control, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(log_name_control, 12, LV_PART_MAIN);
    make_label(log_name_control, "Next Log Name", DASH_FONT_LABEL, C_LABEL);
    s_cfg_log_name_dropdown = lv_dropdown_create(log_name_control);
    lv_obj_set_width(s_cfg_log_name_dropdown, 220);
    lv_obj_set_height(s_cfg_log_name_dropdown, 42);
    lv_dropdown_set_options(s_cfg_log_name_dropdown,
                            "General (LOG)\nDyno\nTune\nRace\nTest");
    lv_dropdown_set_selected(s_cfg_log_name_dropdown, dash_config_get_log_name());
    lv_obj_add_event_cb(s_cfg_log_name_dropdown, cfg_log_name_dropdown_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    s_log_status_label = make_label(s_page_logs, "Select a recorded log", DASH_FONT_LABEL, C_LABEL);
    lv_obj_set_width(s_log_status_label, LV_PCT(100));
    s_log_readout_label = make_label(s_page_logs, "Touch a graph for exact values", DASH_FONT_LABEL, C_WHITE);
    lv_obj_set_width(s_log_readout_label, LV_PCT(100));
    lv_label_set_long_mode(s_log_readout_label, LV_LABEL_LONG_DOT);

    s_log_chart_view = lv_obj_create(s_page_logs);
    lv_obj_set_size(s_log_chart_view, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(s_log_chart_view, 1);
    lv_obj_set_style_bg_color(s_log_chart_view, C_VOID, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_log_chart_view, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_log_chart_view, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_log_chart_view, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_log_chart_view, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_log_chart_view, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_log_chart_view, LV_SCROLLBAR_MODE_ACTIVE);
    s_log_chart_content = make_plain_container(s_log_chart_view);
    lv_obj_set_size(s_log_chart_content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_log_chart_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_log_chart_content, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_log_chart_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *units_content = build_config_subpage(panel, &s_page_units, "UNITS");
    lv_obj_t *display_content = build_config_subpage(panel, &s_page_display, "DISPLAY");
    lv_obj_t *shortcuts_content = build_config_subpage(panel, &s_page_shortcuts, "SHORTCUTS & TIPS");
    lv_obj_t *readme_content = build_config_subpage(panel, &s_page_readme, "READ ME");
    lv_obj_t *odometer_content = build_config_subpage(panel, &s_page_odometer, "ODOMETER & TRIPS");
    lv_obj_t *fuel_content = build_config_subpage(panel, &s_page_fuel, "FUEL GAUGE SETUP");
    lv_obj_t *engine_limits_content = build_config_subpage(panel, &s_page_engine_limits, "ENGINE LIMITS");
    lv_obj_t *achievements_content = build_config_subpage(panel, &s_page_achievements, "ACHIEVEMENTS");

    s_achievement_summary_label = make_label(achievements_content, "0 OF 10 UNLOCKED",
                                             DASH_FONT_LABEL14, C_AMBER);
    for (uint8_t index = 0; index < ACHIEVEMENT_SYSTEM_TOTAL; ++index) {
        achievement_system_entry_t entry;
        achievement_system_get_entry(index, &entry);

        lv_obj_t *row = lv_obj_create(achievements_content);
        lv_obj_set_size(row, LV_PCT(100), 72);
        lv_obj_set_style_bg_color(row, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 12, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *name = make_label(row, entry.name, DASH_FONT_LABEL14, C_WHITE);
        lv_obj_set_width(name, 330);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

        lv_obj_t *stats = make_plain_container(row);
        lv_obj_set_size(stats, 360, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(stats, 24, LV_PART_MAIN);
        s_achievement_hit_labels[index] = make_label(stats, "Hits  0",
                                                     DASH_FONT_LABEL14, C_LABEL);
        s_achievement_record_labels[index] = make_label(stats, "Record  --",
                                                        DASH_FONT_LABEL14, C_AMBER);
        lv_obj_set_width(s_achievement_record_labels[index], 170);
        lv_obj_set_style_text_align(s_achievement_record_labels[index],
                                    LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    settings_achievements_refresh();

    /* ---- page: ECU protocol ---- */
    s_page_ecu = make_plain_container(panel);
    lv_obj_set_size(s_page_ecu, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_ecu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_ecu, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page_ecu, 10, LV_PART_MAIN);
    make_label(s_page_ecu, "ECU / CAN PROTOCOL", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *protocol_grid = make_plain_container(s_page_ecu);
    lv_obj_set_size(protocol_grid, LV_PCT(100), 330);
    lv_obj_set_flex_flow(protocol_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(protocol_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(protocol_grid, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(protocol_grid, 12, LV_PART_MAIN);
    for (size_t index = 0; index < CFG_PROTOCOL_COUNT; ++index) {
        lv_obj_t *tile = lv_obj_create(protocol_grid);
        s_cfg_protocol_tiles[index] = tile;
        lv_obj_set_size(tile, 198, 145);
        lv_obj_set_style_bg_color(tile, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 10, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, cfg_protocol_tile_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
        add_press_feedback(tile);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, 8, LV_PART_MAIN);
        make_label(tile, LV_SYMBOL_CHARGE, DASH_FONT_TILEVAL, C_WHITE);
        lv_obj_t *name = make_label(tile, CFG_PROTOCOL_LABELS[index], DASH_FONT_LABEL14, C_WHITE);
        lv_obj_set_width(name, 174);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    }
    s_cfg_protocol_status_label = make_label(s_page_ecu, "", DASH_FONT_LABEL14, C_WHITE);
    s_ecu_restart_note = make_label(s_page_ecu, "Restart required to activate this ECU protocol",
                                    DASH_FONT_LABEL, lv_color_hex(0xffb020));
    lv_obj_add_flag(s_ecu_restart_note, LV_OBJ_FLAG_HIDDEN);
    cfg_protocol_tiles_refresh();

    /* ---- page: theme layout resets ---- */
    s_page_theme_resets = make_plain_container(panel);
    lv_obj_set_size(s_page_theme_resets, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_theme_resets, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_theme_resets, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page_theme_resets, 12, LV_PART_MAIN);
    make_label(s_page_theme_resets, "RESET THEME LAYOUT", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *theme_reset_grid = make_plain_container(s_page_theme_resets);
    lv_obj_set_size(theme_reset_grid, LV_PCT(100), 300);
    lv_obj_set_flex_flow(theme_reset_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(theme_reset_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(theme_reset_grid, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(theme_reset_grid, 12, LV_PART_MAIN);
    build_theme_layout_reset_button(theme_reset_grid, "MackoDash V1", SYSTEM_FIELD_MODERN);
    build_theme_layout_reset_button(theme_reset_grid, "Race LCD", SYSTEM_FIELD_RACE);
    build_theme_layout_reset_button(theme_reset_grid, "HalDash", SYSTEM_FIELD_HAL);
    build_theme_layout_reset_button(theme_reset_grid, "HunterDash", SYSTEM_FIELD_ENDURANCE);
    build_theme_layout_reset_button(theme_reset_grid, "Rally Stage", SYSTEM_FIELD_TOURING);
    make_label(s_page_theme_resets, "Choose a built-in theme to restore its default gauge assignments.",
               DASH_FONT_LABEL, C_LABEL_DIM);
    /* ---- page: config -- installer/new-user tweaks, card-based layout ---- */
    s_page_config = make_plain_container(panel);
    lv_obj_set_size(s_page_config, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_page_config, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_page_config, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_page_config, 10, LV_PART_MAIN);
    lv_obj_clear_flag(s_page_config, LV_OBJ_FLAG_SCROLLABLE);
    make_label(s_page_config, "SYSTEM SETTINGS", DASH_FONT_LABEL14, C_LABEL);

    lv_obj_t *cfg_scroll = lv_obj_create(s_page_config);
    lv_obj_set_size(cfg_scroll, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(cfg_scroll, 1);
    lv_obj_set_style_bg_opa(cfg_scroll, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(cfg_scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cfg_scroll, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(cfg_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cfg_scroll, 10, LV_PART_MAIN);
    lv_obj_set_scroll_dir(cfg_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cfg_scroll, LV_SCROLLBAR_MODE_AUTO);

    build_config_section_header(cfg_scroll, "SYSTEM");
    build_config_menu_row(cfg_scroll, "Diagnostics", settings_open_info_cb);
    build_config_menu_row(cfg_scroll, "Contact & Support", settings_open_contact_cb);
    build_config_menu_row(cfg_scroll, "Shortcuts & Tips", settings_open_shortcuts_cb);
    build_config_menu_row(cfg_scroll, "Read Me", settings_open_readme_cb);

    /* --- independent unit controls --- */
    static const char *const unit_titles[UNIT_SETTING_COUNT] = {
        "Road Speed", "Temperature", "Pressure", "Distance & Odometer",
    };
    for (int index = 0; index < UNIT_SETTING_COUNT; ++index) {
        unit_setting_t setting = (unit_setting_t)index;
        bool enabled = cfg_unit_setting_get(setting);
        lv_obj_t *card = lv_obj_create(units_content);
        lv_obj_set_size(card, LV_PCT(100), 76);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *left = make_plain_container(card);
        lv_obj_set_size(left, 360, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(left, 3, LV_PART_MAIN);
        make_label(left, unit_titles[index], DASH_FONT_LABEL14, C_LABEL);
        s_cfg_unit_values[index] = make_label(left, cfg_unit_setting_value(setting, enabled),
                                              DASH_FONT_LABEL14, C_WHITE);

        lv_obj_t *sw = lv_switch_create(card);
        s_cfg_unit_switches[index] = sw;
        lv_obj_set_style_bg_color(sw, C_RED, LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, cfg_unit_switch_cb, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)setting);
    }

    /* --- display controls --- */
    {
        lv_obj_t *card = lv_obj_create(display_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);

        lv_obj_t *brightness_top = make_plain_container(card);
        lv_obj_set_size(brightness_top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(brightness_top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(brightness_top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(brightness_top, "Brightness", DASH_FONT_LABEL14, C_LABEL);
        char brightness_buf[8];
        snprintf(brightness_buf, sizeof(brightness_buf), "%d%%", dash_config_get_brightness());
        s_brightness_value_label = make_label(brightness_top, brightness_buf, DASH_FONT_LABEL14, C_WHITE);

        s_brightness_slider = lv_slider_create(card);
        configure_menu_slider(s_brightness_slider);
        lv_obj_set_size(s_brightness_slider, LV_PCT(100), 16);
        lv_slider_set_range(s_brightness_slider, 20, 100);
        lv_slider_set_value(s_brightness_slider, dash_config_get_brightness(), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_brightness_slider, C_RED, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_brightness_slider, C_WHITE, LV_PART_KNOB);
        lv_obj_add_event_cb(s_brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(s_brightness_slider, brightness_slider_released_cb, LV_EVENT_RELEASED, NULL);

        card = lv_obj_create(display_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(card, "RPM / Speed Smoothing", DASH_FONT_LABEL14, C_LABEL);
        s_cfg_smoothing_dropdown = lv_dropdown_create(card);
        lv_dropdown_set_options(s_cfg_smoothing_dropdown, "Off\nLight\nMedium\nStrong");
        lv_dropdown_set_selected(s_cfg_smoothing_dropdown, dash_config_get_smoothing_strength());
        lv_obj_set_width(s_cfg_smoothing_dropdown, 150);
        lv_obj_add_event_cb(s_cfg_smoothing_dropdown, cfg_smoothing_dropdown_cb,
                    LV_EVENT_VALUE_CHANGED, NULL);

        card = lv_obj_create(display_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                      LV_FLEX_ALIGN_CENTER);
        make_label(card, "Language", DASH_FONT_LABEL14, C_LABEL);
        lv_obj_t *language_dropdown = lv_dropdown_create(card);
        lv_dropdown_set_options(language_dropdown, dashboard_i18n_language_options());
        lv_dropdown_set_selected(language_dropdown, dash_config_get_language());
        lv_obj_set_width(language_dropdown, 190);
        lv_obj_add_event_cb(language_dropdown, cfg_language_dropdown_cb,
                    LV_EVENT_VALUE_CHANGED, NULL);

        card = lv_obj_create(fuel_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);
        make_label(card, "Fuel Gauge Setup", DASH_FONT_LABEL14, C_WHITE);

        lv_obj_t *fuel_input_row = make_plain_container(card);
        lv_obj_set_size(fuel_input_row, LV_PCT(100), 44);
        lv_obj_set_flex_flow(fuel_input_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(fuel_input_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(fuel_input_row, "Hondata Analog Input", DASH_FONT_LABEL14, C_LABEL);
        s_cfg_fuel_input_dropdown = lv_dropdown_create(fuel_input_row);
        lv_dropdown_set_options(s_cfg_fuel_input_dropdown,
                                "Analog 0\nAnalog 1\nAnalog 2\nAnalog 3\nAnalog 4\nAnalog 5\nAnalog 6");
        lv_dropdown_set_selected(s_cfg_fuel_input_dropdown, dash_config_get_fuel_analog_input());
        lv_obj_set_width(s_cfg_fuel_input_dropdown, 160);
        lv_obj_add_event_cb(s_cfg_fuel_input_dropdown, cfg_fuel_input_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);

        const char *fuel_endpoint_names[] = {"Empty voltage", "Full voltage"};
        int fuel_endpoint_values[] = {
            dash_config_get_fuel_empty_mv(), dash_config_get_fuel_full_mv()
        };
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            lv_obj_t *top = make_plain_container(card);
            lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            make_label(top, fuel_endpoint_names[endpoint], DASH_FONT_LABEL, C_LABEL);
            char voltage[12];
            snprintf(voltage, sizeof(voltage), "%.2f V",
                     (double)fuel_endpoint_values[endpoint] / 1000.0);
            s_cfg_fuel_voltage_labels[endpoint] =
                make_label(top, voltage, DASH_FONT_LABEL14, C_WHITE);

            lv_obj_t *slider = lv_slider_create(card);
            s_cfg_fuel_voltage_sliders[endpoint] = slider;
            configure_menu_slider(slider);
            lv_obj_set_size(slider, LV_PCT(100), 16);
            lv_slider_set_range(slider, 0, 5000);
            lv_slider_set_value(slider, fuel_endpoint_values[endpoint], LV_ANIM_OFF);
            lv_obj_set_style_bg_color(slider, C_RED, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(slider, C_WHITE, LV_PART_KNOB);
            lv_obj_add_event_cb(slider, cfg_fuel_voltage_changed_cb,
                                LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)endpoint);
            lv_obj_add_event_cb(slider, cfg_fuel_voltage_released_cb,
                                LV_EVENT_RELEASED, (void *)(intptr_t)endpoint);
        }

        lv_obj_t *fuel_live_row = make_plain_container(card);
        lv_obj_set_size(fuel_live_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(fuel_live_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(fuel_live_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(fuel_live_row, "Live Reading", DASH_FONT_LABEL, C_LABEL);
        s_cfg_fuel_live_label = make_label(fuel_live_row, "Analog 0  0.00 V  0%",
                                           DASH_FONT_LABEL14, C_AMBER);
        cfg_fuel_live_refresh();
        lv_timer_create(cfg_fuel_live_timer_cb, 250, NULL);
        make_label(card, "Hondata analog inputs must not exceed 5 V.",
                   DASH_FONT_LABEL, C_RED);

        lv_obj_t *wiring_card = lv_obj_create(fuel_content);
        lv_obj_set_size(wiring_card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(wiring_card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(wiring_card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(wiring_card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(wiring_card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(wiring_card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(wiring_card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(wiring_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(wiring_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(wiring_card, 8, LV_PART_MAIN);
        make_label(wiring_card, "WIRING", DASH_FONT_LABEL14, C_WHITE);
        lv_obj_t *wiring_text = make_label(wiring_card,
            "1. Disconnect the fuel sender wire from the stock cluster. Make sure no other device is powering the sender.\n"
            "2. Wire the sender to the selected Hondata analog input using a 56 ohm, 1 watt resistor.\n"
            "3. With a multimeter, verify the signal at the Hondata input never exceeds 5.00 V before connecting the ECU.\n"
            "4. K-series normally uses ECU pin E5. B-series uses ECU pin D19. The K-series E5 terminal is TE Connectivity 316836-1.",
            DASH_FONT_LABEL14, C_LABEL);
        lv_obj_set_width(wiring_text, LV_PCT(100));
        lv_label_set_long_mode(wiring_text, LV_LABEL_LONG_WRAP);

        lv_obj_t *calibration_card = lv_obj_create(fuel_content);
        lv_obj_set_size(calibration_card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(calibration_card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(calibration_card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(calibration_card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(calibration_card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(calibration_card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(calibration_card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(calibration_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(calibration_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(calibration_card, 8, LV_PART_MAIN);
        make_label(calibration_card, "CALIBRATION", DASH_FONT_LABEL14, C_WHITE);
        lv_obj_t *calibration_text = make_label(calibration_card,
            "Measure the selected analog input with the tank empty and full, then enter those two voltages above. The dashboard supports either voltage direction and clamps the result to 0-100%.\n\n"
            "Reference values (Empty / Full):\n"
            "Civic EG  3.31 / 0.17 V    Civic EK  3.29 / 0.29 V\n"
            "S2000  3.50 / 0.90 V       Integra DC5  3.51 / 0.82 V\n"
            "Accord CL9  4.66 / 1.25 V",
            DASH_FONT_LABEL14, C_LABEL);
        lv_obj_set_width(calibration_text, LV_PCT(100));
        lv_label_set_long_mode(calibration_text, LV_LABEL_LONG_WRAP);

        card = lv_obj_create(display_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);
        make_label(card, "Boot Logo", DASH_FONT_LABEL14, C_LABEL);

        char logo_options[BOOT_LOGO_STORAGE_MAX_LOGOS * BOOT_LOGO_STORAGE_NAME_MAX + 32] =
            "Built-in MackoDash";
        for (size_t index = 0; index < boot_logo_storage_get_count(); ++index) {
            const boot_logo_storage_entry_t *entry = boot_logo_storage_get_entry(index);
            strlcat(logo_options, "\n", sizeof(logo_options));
            strlcat(logo_options, entry->display_name, sizeof(logo_options));
        }
        s_cfg_boot_logo_dropdown = lv_dropdown_create(card);
        lv_obj_set_width(s_cfg_boot_logo_dropdown, LV_PCT(100));
        lv_dropdown_set_options(s_cfg_boot_logo_dropdown, logo_options);
        int selected_logo = boot_logo_storage_get_selected_index();
        lv_dropdown_set_selected(s_cfg_boot_logo_dropdown, (uint16_t)(selected_logo + 1));
        lv_obj_add_event_cb(s_cfg_boot_logo_dropdown, cfg_boot_logo_dropdown_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_t *logo_actions = make_plain_container(card);
        lv_obj_set_size(logo_actions, LV_PCT(100), 42);
        lv_obj_set_flex_flow(logo_actions, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(logo_actions, 10, LV_PART_MAIN);
        lv_obj_t *set_logo = lv_btn_create(logo_actions);
        lv_obj_set_size(set_logo, 150, 40);
        lv_obj_set_style_bg_color(set_logo, C_RED, LV_PART_MAIN);
        lv_obj_add_event_cb(set_logo, cfg_boot_logo_set_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_center(make_label(set_logo, "Set", DASH_FONT_LABEL14, C_WHITE));
        s_cfg_boot_logo_delete_button = lv_btn_create(logo_actions);
        lv_obj_set_size(s_cfg_boot_logo_delete_button, 150, 40);
        lv_obj_set_style_bg_color(s_cfg_boot_logo_delete_button, C_RED_DEEP, LV_PART_MAIN);
        lv_obj_add_event_cb(s_cfg_boot_logo_delete_button, cfg_boot_logo_delete_cb,
                            LV_EVENT_CLICKED, NULL);
        lv_obj_center(make_label(s_cfg_boot_logo_delete_button, "Delete", DASH_FONT_LABEL14, C_WHITE));
        if (selected_logo < 0) lv_obj_add_state(s_cfg_boot_logo_delete_button, LV_STATE_DISABLED);
        s_cfg_boot_logo_status = make_label(card, "Changes appear on the next startup.",
                                            DASH_FONT_LABEL, C_LABEL_DIM);

        card = lv_obj_create(display_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *sim_row = make_plain_container(card);
        lv_obj_set_size(sim_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(sim_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(sim_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(sim_row, "Show Simulation Button", DASH_FONT_LABEL14, C_LABEL);
        s_cfg_sim_button_switch = lv_switch_create(sim_row);
        lv_obj_set_style_bg_color(s_cfg_sim_button_switch, C_RED, LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (dash_config_get_show_sim_button()) lv_obj_add_state(s_cfg_sim_button_switch, LV_STATE_CHECKED);
        lv_obj_add_event_cb(s_cfg_sim_button_switch, cfg_sim_button_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

        build_config_menu_row(display_content, "Units", settings_open_units_cb);
    }

    build_shortcut_tip(shortcuts_content, "HOLD SETTINGS",
                       "Open quick Day, Dim, and Night brightness modes.");
    build_shortcut_tip(shortcuts_content, "HOLD A DATA VALUE",
                       "Choose which channel that value shows on built-in themes.");
    build_shortcut_tip(shortcuts_content, "TAP / HOLD SIM",
                       "Tap to toggle simulation. Hold to choose an RPM and driving mode.");
    build_shortcut_tip(shortcuts_content, "TAP REC",
                       "Start or stop a driving log on the SD card.");
    build_shortcut_tip(shortcuts_content, "TAP ODO",
                       "On MackoDash, cycle between odometer, Trip A, and Trip B.");
    build_shortcut_tip(shortcuts_content, "HOLD ODO",
                       "Reset the displayed Trip A or Trip B after confirmation.");
    build_shortcut_tip(shortcuts_content, "TOUCH A LOG GRAPH",
                       "Show the exact recorded values at that point in the drive.");
    build_shortcut_tip(shortcuts_content, "TAP A THEME",
                       "Stage your selection. Press Set to apply it and return to the dashboard.");

    make_label(readme_content, "GETTING STARTED", DASH_FONT_LABEL, C_AMBER);
    lv_obj_t *readme = make_label(readme_content,
        "1. Open Settings > ECU and select the correct protocol, or use Auto.\n"
        "2. Open Display > Units and choose speed, temperature, pressure, and distance units.\n"
        "3. Set VTEC, redline, shift lights, and warning limits under Engine Limits.\n"
        "4. Use SIM while parked to verify the selected theme and warning behavior.\n"
        "5. Start the engine while stationary and compare every value with a trusted source.",
        DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(readme, LV_PCT(100));
    lv_label_set_long_mode(readme, LV_LABEL_LONG_WRAP);

    make_label(readme_content, "DASHBOARD CONTROLS", DASH_FONT_LABEL, C_AMBER);
    readme = make_label(readme_content,
        "Settings opens configuration. Hold Settings for Day, Dim, and Night brightness. "
        "Tap SIM to start or stop simulation; hold SIM to choose an RPM and driving mode. "
        "Tap REC to start or stop an SD-card driving log. Tap ODO to cycle Odometer, Trip A, "
        "and Trip B. Hold a displayed trip to reset it after confirmation. Hold a configurable "
        "data value to choose the channel shown in that position.", DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(readme, LV_PCT(100));
    lv_label_set_long_mode(readme, LV_LABEL_LONG_WRAP);

    make_label(readme_content, "THEMES AND DISPLAY", DASH_FONT_LABEL, C_AMBER);
    readme = make_label(readme_content,
        "Choose a built-in or SD-card theme under Theme, then press Set. Only the selected theme "
        "updates and renders. Display contains brightness, RPM/speed smoothing, simulation-button "
        "visibility, and Units. Smoothing levels are Off, Light, Medium, and Strong. Theme Resets "
        "restores the default channel assignments for one built-in theme.", DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(readme, LV_PCT(100));
    lv_label_set_long_mode(readme, LV_LABEL_LONG_WRAP);

    make_label(readme_content, "LOGS", DASH_FONT_LABEL, C_AMBER);
    readme = make_label(readme_content,
        "Driving Logs records ECU data to the SD card and lets you inspect saved graphs. Auto Record "
        "starts a log when live engine data meets the recording conditions. Session Peaks shows the "
        "highest or lowest values from the current session. Achievements tracks threshold events and "
        "personal records. The red REC badge shows the active filename and elapsed recording time.",
        DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(readme, LV_PCT(100));
    lv_label_set_long_mode(readme, LV_LABEL_LONG_WRAP);

    make_label(readme_content, "ECU, WARNINGS, AND DIAGNOSTICS", DASH_FONT_LABEL, C_AMBER);
    readme = make_label(readme_content,
        "The ECU page selects Auto, Hondata, Haltech, Link G4X, MegaSquirt, Emtron, MaxxECU, or "
        "ECUMaster Black. Available channels depend on the ECU broadcast. The centered amber label "
        "means ECU data is disconnected or stale and disappears when live data returns. Engine Limits "
        "controls shift-light stages, VTEC, redline, and warning thresholds. Diagnostics reports CAN "
        "state, frame age, errors, memory, task stack margins, reset reason, and actual display FPS.",
        DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(readme, LV_PCT(100));
    lv_label_set_long_mode(readme, LV_LABEL_LONG_WRAP);

    make_label(readme_content, "UPDATES, STORAGE, AND SAFETY", DASH_FONT_LABEL, C_AMBER);
    readme = make_label(readme_content,
        "Use the official MackoDash Utility on Windows for firmware updates. Keep dashboard power and "
        "USB connected until verification finishes. Custom .mdtheme.zip packages belong in "
        "/MACKODASH/THEMES on a FAT-formatted microSD card. Configure and test while stationary. "
        "Verify CAN scaling, units, thresholds, wiring, and displayed values against trusted equipment. "
        "Do not use MackoDash as a replacement for required factory safety systems.",
        DASH_FONT_LABEL14, C_WHITE);
    lv_obj_set_width(readme, LV_PCT(100));
    lv_label_set_long_mode(readme, LV_LABEL_LONG_WRAP);

    build_config_section_header(cfg_scroll, "VEHICLE SETUP");
    build_config_menu_row(cfg_scroll, "Odometer & Trip Settings", settings_open_odometer_cb);
    build_config_menu_row(cfg_scroll, "Fuel Gauge Setup", settings_open_fuel_cb);

    /* --- odometer calibration --- */
    {
        lv_obj_t *card = lv_obj_create(odometer_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);

        lv_obj_t *odo_top = make_plain_container(card);
        lv_obj_set_size(odo_top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(odo_top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(odo_top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(odo_top, "Odometer Calibration", DASH_FONT_LABEL14, C_LABEL_DIM);
        s_cfg_odo_label = make_label(odo_top, "0 mi", DASH_FONT_LABEL14, C_WHITE);

        lv_obj_t *odo_steps = make_plain_container(card);
        lv_obj_set_size(odo_steps, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(odo_steps, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(odo_steps, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(odo_steps, 6, LV_PART_MAIN);
        {
            static const int odo_step_values[] = { -1000, -100, -10, 10, 100, 1000 };
            for (size_t i = 0; i < sizeof(odo_step_values) / sizeof(odo_step_values[0]); i++) {
                char lbl_buf[8];
                snprintf(lbl_buf, sizeof(lbl_buf), "%+d", odo_step_values[i]);
                lv_obj_t *step_btn = lv_obj_create(odo_steps);
                lv_obj_set_size(step_btn, 62, 38);
                lv_obj_set_style_bg_color(step_btn, C_VOID, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(step_btn, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_border_width(step_btn, 1, LV_PART_MAIN);
                lv_obj_set_style_border_color(step_btn, C_LINE, LV_PART_MAIN);
                lv_obj_set_style_radius(step_btn, 10, LV_PART_MAIN);
                lv_obj_clear_flag(step_btn, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(step_btn, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(step_btn, cfg_odo_step_cb, LV_EVENT_CLICKED,
                                     (void *)(intptr_t)odo_step_values[i]);
                add_press_feedback(step_btn);
                lv_obj_t *step_lbl = make_label(step_btn, lbl_buf, DASH_FONT_LABEL, C_LABEL);
                lv_obj_center(step_lbl);
            }
            lv_obj_t *odo_save_btn = lv_obj_create(odo_steps);
            lv_obj_set_size(odo_save_btn, 80, 38);
            lv_obj_set_style_bg_color(odo_save_btn, C_RED, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(odo_save_btn, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(odo_save_btn, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(odo_save_btn, 10, LV_PART_MAIN);
            lv_obj_clear_flag(odo_save_btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(odo_save_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(odo_save_btn, cfg_odo_save_cb, LV_EVENT_CLICKED, NULL);
            add_press_feedback(odo_save_btn);
            lv_obj_t *odo_save_lbl = make_label(odo_save_btn, "Save", DASH_FONT_LABEL14, C_WHITE);
            lv_obj_center(odo_save_lbl);
        }
    }

    /* --- trip meters --- */
    {
        lv_obj_t *card = lv_obj_create(odometer_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);
        make_label(card, "Trip Meters  \xC2\xB7  tap ODO on the dashboard to cycle", DASH_FONT_LABEL14, C_LABEL_DIM);

        lv_obj_t *trip_a_row = make_plain_container(card);
        lv_obj_set_size(trip_a_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(trip_a_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(trip_a_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(trip_a_row, "Trip A", DASH_FONT_LABEL14, C_LABEL);
        s_cfg_trip_a_label = make_label(trip_a_row, "0 mi", DASH_FONT_LABEL14, C_WHITE);
        lv_obj_t *trip_a_reset = lv_obj_create(trip_a_row);
        lv_obj_set_size(trip_a_reset, 90, 36);
        lv_obj_set_style_bg_color(trip_a_reset, C_VOID, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(trip_a_reset, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(trip_a_reset, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(trip_a_reset, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(trip_a_reset, 8, LV_PART_MAIN);
        lv_obj_clear_flag(trip_a_reset, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(trip_a_reset, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(trip_a_reset, cfg_trip_a_reset_cb, LV_EVENT_CLICKED, NULL);
        add_press_feedback(trip_a_reset);
        lv_obj_t *trip_a_reset_lbl = make_label(trip_a_reset, "Reset", DASH_FONT_LABEL14, C_LABEL);
        lv_obj_center(trip_a_reset_lbl);

        lv_obj_t *trip_b_row = make_plain_container(card);
        lv_obj_set_size(trip_b_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(trip_b_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(trip_b_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(trip_b_row, "Trip B", DASH_FONT_LABEL14, C_LABEL);
        s_cfg_trip_b_label = make_label(trip_b_row, "0 mi", DASH_FONT_LABEL14, C_WHITE);
        lv_obj_t *trip_b_reset = lv_obj_create(trip_b_row);
        lv_obj_set_size(trip_b_reset, 90, 36);
        lv_obj_set_style_bg_color(trip_b_reset, C_VOID, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(trip_b_reset, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(trip_b_reset, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(trip_b_reset, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(trip_b_reset, 8, LV_PART_MAIN);
        lv_obj_clear_flag(trip_b_reset, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(trip_b_reset, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(trip_b_reset, cfg_trip_b_reset_cb, LV_EVENT_CLICKED, NULL);
        add_press_feedback(trip_b_reset);
        lv_obj_t *trip_b_reset_lbl = make_label(trip_b_reset, "Reset", DASH_FONT_LABEL14, C_LABEL);
        lv_obj_center(trip_b_reset_lbl);
    }

    make_label(engine_limits_content, "SHIFT LIGHTS", DASH_FONT_LABEL, C_LABEL_DIM);

    {
        lv_obj_t *card = lv_obj_create(engine_limits_content);
        lv_obj_set_size(card, LV_PCT(100), 64);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(card, "RPM Bar Shift Colors", DASH_FONT_LABEL14, C_WHITE);
        s_cfg_shift_enable_switch = lv_switch_create(card);
        lv_obj_set_style_bg_color(s_cfg_shift_enable_switch, C_RED,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(s_cfg_shift_enable_switch, cfg_shift_enable_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }

    static const char *const stage_names[DASH_CONFIG_SHIFT_STAGE_COUNT] = {
        "Stage 1 Start", "Stage 2 Start", "Stage 3 Start",
    };
    for (int stage = 0; stage < DASH_CONFIG_SHIFT_STAGE_COUNT; ++stage) {
        s_cfg_shift_stage_sliders[stage] = build_shift_rpm_card(
            engine_limits_content, stage_names[stage], LV_PCT(100),
            &s_cfg_shift_stage_labels[stage], &s_cfg_shift_color_dropdowns[stage],
            cfg_shift_stage_slider_cb, cfg_shift_stage_released_cb, stage);
    }

    {
        lv_obj_t *card = lv_obj_create(engine_limits_content);
        lv_obj_set_size(card, LV_PCT(100), 88);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);
        lv_obj_t *top = make_plain_container(card);
        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(top, "Shift Color Brightness", DASH_FONT_LABEL14, C_LABEL);
        s_cfg_shift_brightness_label = make_label(top, "100%", DASH_FONT_LABEL14, C_WHITE);
        s_cfg_shift_brightness_slider = lv_slider_create(card);
        configure_menu_slider(s_cfg_shift_brightness_slider);
        lv_obj_set_size(s_cfg_shift_brightness_slider, LV_PCT(100), 14);
        lv_slider_set_range(s_cfg_shift_brightness_slider, 20, 100);
        lv_obj_set_style_bg_color(s_cfg_shift_brightness_slider, C_RED, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_cfg_shift_brightness_slider, C_WHITE, LV_PART_KNOB);
        lv_obj_add_event_cb(s_cfg_shift_brightness_slider, cfg_shift_brightness_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(s_cfg_shift_brightness_slider, cfg_shift_brightness_released_cb,
                            LV_EVENT_RELEASED, NULL);
    }

    {
        lv_obj_t *card = lv_obj_create(engine_limits_content);
        lv_obj_set_size(card, LV_PCT(100), 64);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(card, "Flash All at Redline", DASH_FONT_LABEL14, C_WHITE);
        s_cfg_shift_flash_switch = lv_switch_create(card);
        lv_obj_set_style_bg_color(s_cfg_shift_flash_switch, C_RED,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(s_cfg_shift_flash_switch, cfg_shift_flash_cb,
                            LV_EVENT_VALUE_CHANGED, NULL);
    }

    make_label(engine_limits_content, "VTEC & REDLINE", DASH_FONT_LABEL, C_LABEL_DIM);

    /* --- VTEC / redline sliders --- */
    {
        lv_obj_t *card = lv_obj_create(engine_limits_content);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 14, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 6, LV_PART_MAIN);

        lv_obj_t *vtec_top = make_plain_container(card);
        lv_obj_set_size(vtec_top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(vtec_top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(vtec_top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(vtec_top, "VTEC RPM", DASH_FONT_LABEL14, C_LABEL_DIM);
        s_cfg_vtec_label = make_label(vtec_top, "5600 RPM", DASH_FONT_LABEL14, C_WHITE);

        s_cfg_vtec_slider = lv_slider_create(card);
        configure_menu_slider(s_cfg_vtec_slider);
        lv_obj_set_size(s_cfg_vtec_slider, LV_PCT(100), 16);
        lv_slider_set_range(s_cfg_vtec_slider, 3000, 7500);
        lv_slider_set_value(s_cfg_vtec_slider, 5600, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_cfg_vtec_slider, C_RED, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_cfg_vtec_slider, C_WHITE, LV_PART_KNOB);
        lv_obj_add_event_cb(s_cfg_vtec_slider, cfg_vtec_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_t *redline_top = make_plain_container(card);
        lv_obj_set_size(redline_top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(redline_top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(redline_top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(redline_top, 8, LV_PART_MAIN);
        make_label(redline_top, "Redline RPM", DASH_FONT_LABEL14, C_LABEL_DIM);
        s_cfg_redline_label = make_label(redline_top, "8400 RPM", DASH_FONT_LABEL14, C_WHITE);

        s_cfg_redline_slider = lv_slider_create(card);
        configure_menu_slider(s_cfg_redline_slider);
        lv_obj_set_size(s_cfg_redline_slider, LV_PCT(100), 16);
        lv_slider_set_range(s_cfg_redline_slider, 6000, 11000);
        lv_slider_set_value(s_cfg_redline_slider, 8400, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_cfg_redline_slider, C_RED, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_cfg_redline_slider, C_WHITE, LV_PART_KNOB);
        lv_obj_add_event_cb(s_cfg_redline_slider, cfg_redline_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }

    shift_settings_refresh();

    s_cfg_restart_note = make_label(cfg_scroll,
        "Units / Protocol / Redline changes apply after a restart", DASH_FONT_LABEL, lv_color_hex(0xffb020));
    lv_obj_add_flag(s_cfg_restart_note, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *system_tiles = make_plain_container(cfg_scroll);
    lv_obj_set_size(system_tiles, LV_PCT(100), 166);
    lv_obj_set_flex_flow(system_tiles, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(system_tiles, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(system_tiles, 12, LV_PART_MAIN);
    build_settings_tile(system_tiles, LV_SYMBOL_REFRESH, "Reboot", cfg_reboot_cb);
    build_settings_tile(system_tiles, LV_SYMBOL_WARNING, "Factory Reset", cfg_factory_reset_request_cb);
    build_settings_tile(system_tiles, LV_SYMBOL_REFRESH, "Theme Resets", settings_open_theme_resets_cb);

    s_settings_nav_button = lv_obj_create(s_settings_overlay);
    lv_obj_add_flag(s_settings_nav_button, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_settings_nav_button, 42, 64);
    lv_obj_align(s_settings_nav_button, LV_ALIGN_LEFT_MID, 5, 20);
    lv_obj_set_style_bg_color(s_settings_nav_button, C_RED_DEEP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_settings_nav_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_settings_nav_button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_settings_nav_button, C_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(s_settings_nav_button, 8, LV_PART_MAIN);
    lv_obj_clear_flag(s_settings_nav_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_settings_nav_button, settings_nav_cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(s_settings_nav_button);
    s_settings_nav_label = make_label(s_settings_nav_button, LV_SYMBOL_CLOSE,
                                      DASH_FONT_LABEL14, C_WHITE);
    lv_obj_center(s_settings_nav_label);
    lv_obj_move_foreground(s_settings_nav_button);
    s_settings_current_page = s_page_main;
}

#if 0
/* Retired pre-SquareLine themes retained temporarily outside the build. */
/* ================= THEME: TRACK (monochrome data-logger) ==================== */

static lv_obj_t *build_theme_track(lv_obj_t *cluster)
{
    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ---- RPM bar: white segments, red past FULL_RED_RPM, same dome taper ---- */
    lv_obj_t *rpm_wrap = make_plain_container(root);
    lv_obj_set_size(rpm_wrap, LV_PCT(100), (lv_coord_t)(SCR_H * 0.23f));
    lv_obj_set_style_bg_color(rpm_wrap, lv_color_hex(0x0a0a0a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rpm_wrap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(rpm_wrap, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(rpm_wrap, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(rpm_wrap, lv_color_hex(0x262626), LV_PART_MAIN);
    lv_obj_set_style_radius(rpm_wrap, 28, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(rpm_wrap, true, LV_PART_MAIN);
    lv_obj_set_style_pad_left(rpm_wrap, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(rpm_wrap, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(rpm_wrap, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(rpm_wrap, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(rpm_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rpm_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rpm_wrap, 5, LV_PART_MAIN);

    lv_obj_t *segbar = make_plain_container(rpm_wrap);
    lv_obj_set_size(segbar, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(segbar, 1);
    lv_obj_set_flex_flow(segbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(segbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(segbar, 3, LV_PART_MAIN);
    for (int i = 0; i < SEG_COUNT; i++) {
        lv_obj_t *seg = lv_obj_create(segbar);
        lv_obj_set_style_bg_color(seg, lv_color_hex(0x1c1c1c), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(seg, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(seg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_grow(seg, 1);
        float t = ((float)i / (float)(SEG_COUNT - 1)) * 2.0f - 1.0f;
        lv_obj_set_height(seg, (lv_coord_t)(DOME_MAX_SEG_H * dome_height_frac(t)));
        s_trk_segs[i] = seg;
    }

    lv_obj_t *scale_row = make_plain_container(rpm_wrap);
    lv_obj_set_size(scale_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scale_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scale_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i <= 9; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i);
        make_label(scale_row, buf, DASH_FONT_LABEL, lv_color_hex(0x5a5a5a));
    }

    /* ---- content: RPM | divider | SPEED | divider | GEAR, then tile grid ---- */
    lv_obj_t *content = make_plain_container(root);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(content, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(content, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 8, LV_PART_MAIN);

    lv_obj_t *center_row = make_plain_container(content);
    lv_obj_set_size(center_row, LV_PCT(100), 130);
    lv_obj_set_flex_flow(center_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center_row, 48, LV_PART_MAIN);

    lv_obj_t *rpm_block = make_plain_container(center_row);
    lv_obj_set_size(rpm_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rpm_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rpm_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(rpm_block, "RPM", DASH_FONT_LABEL, lv_color_hex(0x6a6a6a));
    s_trk_rpm_val = make_label(rpm_block, "850", DASH_FONT_RPM, C_WHITE);

    lv_obj_t *div1 = lv_obj_create(center_row);
    lv_obj_set_size(div1, 1, 96);
    lv_obj_set_style_bg_color(div1, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div1, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(div1, 0, LV_PART_MAIN);
    lv_obj_clear_flag(div1, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *speed_block = make_plain_container(center_row);
    lv_obj_set_size(speed_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(speed_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(speed_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_trk_speed_val = make_label(speed_block, "0", DASH_FONT_SPEED, C_WHITE);
    make_label(speed_block, dash_config_get_speed_kph() ? "KPH" : "MPH", DASH_FONT_LABEL14, lv_color_hex(0x6a6a6a));

    lv_obj_t *div2 = lv_obj_create(center_row);
    lv_obj_set_size(div2, 1, 96);
    lv_obj_set_style_bg_color(div2, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(div2, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(div2, 0, LV_PART_MAIN);
    lv_obj_clear_flag(div2, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *gear_block = make_plain_container(center_row);
    lv_obj_set_size(gear_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(gear_block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gear_block, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(gear_block, 6, LV_PART_MAIN);
    make_label(gear_block, "GEAR", DASH_FONT_LABEL14, lv_color_hex(0x6a6a6a));
    lv_obj_t *gear_box = lv_obj_create(gear_block);
    lv_obj_set_size(gear_box, 84, 84);
    lv_obj_set_style_bg_color(gear_box, lv_color_hex(0x0a0a0a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gear_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(gear_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(gear_box, lv_color_hex(0x3a3a3a), LV_PART_MAIN);
    lv_obj_set_style_radius(gear_box, 4, LV_PART_MAIN);
    lv_obj_clear_flag(gear_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(gear_box);
    s_trk_gear_val = lv_label_create(gear_box);
    lv_label_set_text(s_trk_gear_val, "N");
    lv_obj_set_style_text_font(s_trk_gear_val, DASH_FONT_GEAR, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_trk_gear_val, C_WHITE, LV_PART_MAIN);
    lv_obj_center(s_trk_gear_val);

    /* ---- tile grid: plain, monochrome, red text only when a channel is critical ---- */
    lv_obj_t *grid = lv_obj_create(content);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    static lv_coord_t trk_col_dsc[6];
    static lv_coord_t trk_row_dsc[3];
    trk_col_dsc[0] = LV_GRID_FR(1); trk_col_dsc[1] = LV_GRID_FR(1); trk_col_dsc[2] = LV_GRID_FR(1);
    trk_col_dsc[3] = LV_GRID_FR(1); trk_col_dsc[4] = LV_GRID_FR(1); trk_col_dsc[5] = LV_GRID_TEMPLATE_LAST;
    trk_row_dsc[0] = LV_GRID_FR(1); trk_row_dsc[1] = LV_GRID_FR(1); trk_row_dsc[2] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(grid, trk_col_dsc, trk_row_dsc);
    lv_obj_set_style_pad_column(grid, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 8, LV_PART_MAIN);

    const int trk_cols[TILE_COUNT] = {0,1,2,3,4,0,1,2,3,4};
    const int trk_rows[TILE_COUNT] = {0,0,0,0,0,1,1,1,1,1};
    for (int id = 0; id < TILE_COUNT; id++) {
        const tile_def_t *def = &TILE_DEFS[id];
        lv_obj_t *tile = lv_obj_create(grid);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x0a0a0a), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x262626), LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 8, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, trk_cols[id], 1, LV_GRID_ALIGN_STRETCH, trk_rows[id], 1);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        make_label(tile, def->name, DASH_FONT_LABEL, lv_color_hex(0x6a6a6a));
        s_trk_tile_val[id] = make_label(tile, "--", DASH_FONT_TILEVAL, C_WHITE);
    }

    /* ---- bottom strip: odo | telltales | settings | fuel (same 4-col pattern as Modern) ---- */
    lv_obj_t *strip = make_plain_container(root);
    lv_obj_set_size(strip, LV_PCT(100), (lv_coord_t)(SCR_H * 0.085f));
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x0a0a0a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(strip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(strip, lv_color_hex(0x262626), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(strip, 56, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(strip, 0, LV_PART_MAIN);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t trk_strip_col[5];
    static lv_coord_t trk_strip_row[2];
    trk_strip_col[0] = LV_GRID_FR(1); trk_strip_col[1] = LV_GRID_CONTENT;
    trk_strip_col[2] = LV_GRID_CONTENT; trk_strip_col[3] = LV_GRID_FR(1);
    trk_strip_col[4] = LV_GRID_TEMPLATE_LAST;
    trk_strip_row[0] = LV_GRID_FR(1); trk_strip_row[1] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(strip, trk_strip_col, trk_strip_row);
    lv_obj_set_style_pad_column(strip, 18, LV_PART_MAIN);

    lv_obj_t *odo_block = make_plain_container(strip);
    lv_obj_set_size(odo_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(odo_block, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(odo_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(odo_block, 6, LV_PART_MAIN);
    lv_obj_add_flag(odo_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(odo_block, odo_cycle_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(odo_block, odo_reset_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    add_press_feedback(odo_block);
    s_trk_odo_caption = make_label(odo_block, "ODO", DASH_FONT_LABEL, lv_color_hex(0x6a6a6a));
    s_trk_odo_val = make_label(odo_block, "0 MI", DASH_FONT_LABEL14, C_WHITE);

    lv_obj_t *tell_group = make_plain_container(strip);
    lv_obj_set_size(tell_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(tell_group, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(tell_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tell_group, 22, LV_PART_MAIN);
    s_trk_tell_cel     = build_telltale(tell_group, "CEL",       C_RED);
    s_trk_tell_knock   = build_telltale(tell_group, "KNOCK",     C_RED);
    s_trk_tell_oil     = build_telltale(tell_group, "OIL PRESS", C_WHITE);
    s_trk_tell_coolant = build_telltale(tell_group, "COOLANT",   C_WHITE);
    s_trk_tell_vtec    = build_telltale(tell_group, "VTEC READY",C_WHITE);

    build_settings_button(strip);

    lv_obj_t *fuel_block = make_plain_container(strip);
    lv_obj_set_size(fuel_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(fuel_block, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(fuel_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fuel_block, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fuel_block, 8, LV_PART_MAIN);
    make_label(fuel_block, "FUEL", DASH_FONT_LABEL, lv_color_hex(0x6a6a6a));
    s_trk_fuel_bar = lv_bar_create(fuel_block);
    lv_obj_set_size(s_trk_fuel_bar, 70, 9);
    lv_obj_set_style_radius(s_trk_fuel_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_trk_fuel_bar, lv_color_hex(0x1c1c1c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_trk_fuel_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_trk_fuel_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_trk_fuel_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_trk_fuel_bar, C_WHITE, LV_PART_INDICATOR);
    lv_bar_set_range(s_trk_fuel_bar, 0, 100);
    lv_bar_set_value(s_trk_fuel_bar, 0, LV_ANIM_OFF);
    s_trk_fuel_val = make_label(fuel_block, "0", DASH_FONT_LABEL14, C_WHITE);

    /* small spacer to lift the strip off the true bottom edge, same
       technique used for Modern -- must come after the strip in the flow */
    lv_obj_t *spacer = lv_obj_create(root);
    lv_obj_set_size(spacer, LV_PCT(100), 21);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(spacer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    return root;
}

/* ================= THEME: RETRO (80s/90s green vector-fluorescent) ========== */

static lv_obj_t *build_theme_retro(lv_obj_t *cluster)
{
    const lv_color_t GREEN  = lv_color_hex(0x39ff8c);
    const lv_color_t GDIM   = lv_color_hex(0x1f6b3a);
    const lv_color_t GOFF   = lv_color_hex(0x0a2412);
    const lv_color_t GBOX   = lv_color_hex(0x020a04);
    const lv_color_t GBRDR  = lv_color_hex(0x123a1f);

    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ---- RPM bar: green segments, red past FULL_RED_RPM ---- */
    lv_obj_t *rpm_wrap = make_plain_container(root);
    lv_obj_set_size(rpm_wrap, LV_PCT(100), (lv_coord_t)(SCR_H * 0.23f));
    lv_obj_set_style_bg_color(rpm_wrap, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rpm_wrap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(rpm_wrap, 28, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(rpm_wrap, true, LV_PART_MAIN);
    lv_obj_set_style_pad_left(rpm_wrap, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(rpm_wrap, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(rpm_wrap, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(rpm_wrap, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(rpm_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rpm_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rpm_wrap, 5, LV_PART_MAIN);

    lv_obj_t *segbar = make_plain_container(rpm_wrap);
    lv_obj_set_size(segbar, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(segbar, 1);
    lv_obj_set_flex_flow(segbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(segbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(segbar, 4, LV_PART_MAIN);
    for (int i = 0; i < SEG_COUNT; i++) {
        lv_obj_t *seg = lv_obj_create(segbar);
        lv_obj_set_style_bg_color(seg, GOFF, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(seg, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(seg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_grow(seg, 1);
        float t = ((float)i / (float)(SEG_COUNT - 1)) * 2.0f - 1.0f;
        lv_obj_set_height(seg, (lv_coord_t)(DOME_MAX_SEG_H * dome_height_frac(t)));
        s_ret_segs[i] = seg;
    }

    lv_obj_t *scale_row = make_plain_container(rpm_wrap);
    lv_obj_set_size(scale_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scale_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scale_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i <= 9; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i);
        make_label(scale_row, buf, DASH_FONT_LABEL, GDIM);
    }

    /* ---- content: RPM/GEAR mini stats beside a big bordered LCD speed box ---- */
    lv_obj_t *content = make_plain_container(root);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(content, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(content, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 8, LV_PART_MAIN);

    lv_obj_t *center_row = make_plain_container(content);
    lv_obj_set_size(center_row, LV_PCT(100), 130);
    lv_obj_set_flex_flow(center_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center_row, 26, LV_PART_MAIN);

    lv_obj_t *rpm_mini = lv_obj_create(center_row);
    lv_obj_set_size(rpm_mini, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(rpm_mini, GBOX, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rpm_mini, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rpm_mini, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(rpm_mini, GBRDR, LV_PART_MAIN);
    lv_obj_set_style_radius(rpm_mini, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(rpm_mini, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(rpm_mini, 8, LV_PART_MAIN);
    lv_obj_clear_flag(rpm_mini, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(rpm_mini, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rpm_mini, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(rpm_mini, "RPM", DASH_FONT_LABEL, GDIM);
    s_ret_rpm_val = make_label(rpm_mini, "850", DASH_FONT_TILEVAL, GREEN);

    lv_obj_t *speed_box = lv_obj_create(center_row);
    lv_obj_set_size(speed_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(speed_box, GBOX, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(speed_box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(speed_box, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(speed_box, GBRDR, LV_PART_MAIN);
    lv_obj_set_style_radius(speed_box, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(speed_box, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(speed_box, 4, LV_PART_MAIN);
    lv_obj_clear_flag(speed_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(speed_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(speed_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_ret_speed_val = make_label(speed_box, "0", DASH_FONT_SPEED, GREEN);
    make_label(speed_box, dash_config_get_speed_kph() ? "KPH" : "MPH", DASH_FONT_LABEL, GDIM);

    lv_obj_t *gear_mini = lv_obj_create(center_row);
    lv_obj_set_size(gear_mini, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(gear_mini, GBOX, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gear_mini, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(gear_mini, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(gear_mini, GBRDR, LV_PART_MAIN);
    lv_obj_set_style_radius(gear_mini, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(gear_mini, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(gear_mini, 8, LV_PART_MAIN);
    lv_obj_clear_flag(gear_mini, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(gear_mini, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gear_mini, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(gear_mini, "GEAR", DASH_FONT_LABEL, GDIM);
    s_ret_gear_val = make_label(gear_mini, "N", DASH_FONT_TILEVAL, GREEN);

    /* ---- tile grid: dark LCD boxes, green text, red only when critical ---- */
    lv_obj_t *grid = lv_obj_create(content);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(grid, 0, LV_PART_MAIN);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    static lv_coord_t ret_col_dsc[6];
    static lv_coord_t ret_row_dsc[3];
    ret_col_dsc[0] = LV_GRID_FR(1); ret_col_dsc[1] = LV_GRID_FR(1); ret_col_dsc[2] = LV_GRID_FR(1);
    ret_col_dsc[3] = LV_GRID_FR(1); ret_col_dsc[4] = LV_GRID_FR(1); ret_col_dsc[5] = LV_GRID_TEMPLATE_LAST;
    ret_row_dsc[0] = LV_GRID_FR(1); ret_row_dsc[1] = LV_GRID_FR(1); ret_row_dsc[2] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(grid, ret_col_dsc, ret_row_dsc);
    lv_obj_set_style_pad_column(grid, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 8, LV_PART_MAIN);

    const int ret_cols[TILE_COUNT] = {0,1,2,3,4,0,1,2,3,4};
    const int ret_rows[TILE_COUNT] = {0,0,0,0,0,1,1,1,1,1};
    for (int id = 0; id < TILE_COUNT; id++) {
        const tile_def_t *def = &TILE_DEFS[id];
        lv_obj_t *tile = lv_obj_create(grid);
        lv_obj_set_style_bg_color(tile, GBOX, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, GBRDR, LV_PART_MAIN);
        lv_obj_set_style_radius(tile, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 6, LV_PART_MAIN);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, ret_cols[id], 1, LV_GRID_ALIGN_STRETCH, ret_rows[id], 1);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        make_label(tile, def->name, DASH_FONT_LABEL, GDIM);
        s_ret_tile_val[id] = make_label(tile, "--", DASH_FONT_TILEVAL, GREEN);
    }

    /* ---- bottom strip: odo | telltales | settings | fuel ---- */
    lv_obj_t *strip = make_plain_container(root);
    lv_obj_set_size(strip, LV_PCT(100), (lv_coord_t)(SCR_H * 0.085f));
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(strip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(strip, GBRDR, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(strip, 56, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(strip, 0, LV_PART_MAIN);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    static lv_coord_t ret_strip_col[5];
    static lv_coord_t ret_strip_row[2];
    ret_strip_col[0] = LV_GRID_FR(1); ret_strip_col[1] = LV_GRID_CONTENT;
    ret_strip_col[2] = LV_GRID_CONTENT; ret_strip_col[3] = LV_GRID_FR(1);
    ret_strip_col[4] = LV_GRID_TEMPLATE_LAST;
    ret_strip_row[0] = LV_GRID_FR(1); ret_strip_row[1] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(strip, ret_strip_col, ret_strip_row);
    lv_obj_set_style_pad_column(strip, 18, LV_PART_MAIN);

    lv_obj_t *odo_block = make_plain_container(strip);
    lv_obj_set_size(odo_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(odo_block, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(odo_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(odo_block, 6, LV_PART_MAIN);
    lv_obj_add_flag(odo_block, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(odo_block, odo_cycle_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(odo_block, odo_reset_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    add_press_feedback(odo_block);
    s_ret_odo_caption = make_label(odo_block, "ODO", DASH_FONT_LABEL, GDIM);
    s_ret_odo_val = make_label(odo_block, "0 MI", DASH_FONT_LABEL14, GREEN);

    lv_obj_t *tell_group = make_plain_container(strip);
    lv_obj_set_size(tell_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(tell_group, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(tell_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tell_group, 22, LV_PART_MAIN);
    s_ret_tell_cel     = build_telltale(tell_group, "CEL",       C_RED);
    s_ret_tell_knock   = build_telltale(tell_group, "KNOCK",     C_RED);
    s_ret_tell_oil     = build_telltale(tell_group, "OIL PRESS", GREEN);
    s_ret_tell_coolant = build_telltale(tell_group, "COOLANT",   GREEN);
    s_ret_tell_vtec    = build_telltale(tell_group, "VTEC READY",GREEN);

    build_settings_button(strip);

    lv_obj_t *fuel_block = make_plain_container(strip);
    lv_obj_set_size(fuel_block, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(fuel_block, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_flex_flow(fuel_block, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fuel_block, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fuel_block, 8, LV_PART_MAIN);
    make_label(fuel_block, "FUEL", DASH_FONT_LABEL, GDIM);
    s_ret_fuel_bar = lv_bar_create(fuel_block);
    lv_obj_set_size(s_ret_fuel_bar, 70, 9);
    lv_obj_set_style_radius(s_ret_fuel_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ret_fuel_bar, GOFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ret_fuel_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ret_fuel_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ret_fuel_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ret_fuel_bar, GREEN, LV_PART_INDICATOR);
    lv_bar_set_range(s_ret_fuel_bar, 0, 100);
    lv_bar_set_value(s_ret_fuel_bar, 0, LV_ANIM_OFF);
    s_ret_fuel_val = make_label(fuel_block, "0", DASH_FONT_LABEL14, GREEN);

    lv_obj_t *spacer = lv_obj_create(root);
    lv_obj_set_size(spacer, LV_PCT(100), 21);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(spacer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    return root;
}

/* ================= THEME: MINIMAL (huge clean speed, everything else quiet) = */

static lv_obj_t *build_theme_minimal(lv_obj_t *cluster)
{
    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0a0a0a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* ---- top half: one big, bold RPM bar -- same dome-taper segment
       technique as the other themes, just scaled up to dominate half
       the screen instead of a thin accent line ---- */
    lv_obj_t *rpm_wrap = make_plain_container(root);
    lv_obj_set_size(rpm_wrap, LV_PCT(100), (lv_coord_t)(SCR_H * 0.5f));
    lv_obj_set_style_bg_color(rpm_wrap, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rpm_wrap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_side(rpm_wrap, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(rpm_wrap, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(rpm_wrap, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_radius(rpm_wrap, 28, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(rpm_wrap, true, LV_PART_MAIN);
    lv_obj_set_style_pad_left(rpm_wrap, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_right(rpm_wrap, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_top(rpm_wrap, 22, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(rpm_wrap, 14, LV_PART_MAIN);
    lv_obj_set_flex_flow(rpm_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rpm_wrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rpm_wrap, 10, LV_PART_MAIN);

    lv_obj_t *segbar = make_plain_container(rpm_wrap);
    lv_obj_set_size(segbar, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(segbar, 1);
    lv_obj_set_flex_flow(segbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(segbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(segbar, 5, LV_PART_MAIN);
    for (int i = 0; i < SEG_COUNT; i++) {
        lv_obj_t *seg = lv_obj_create(segbar);
        lv_obj_set_style_bg_color(seg, C_SEG_OFF, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(seg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(seg, 3, LV_PART_MAIN);
        lv_obj_set_style_pad_all(seg, 0, LV_PART_MAIN);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_grow(seg, 1);
        float t = ((float)i / (float)(SEG_COUNT - 1)) * 2.0f - 1.0f;
        lv_obj_set_height(seg, (lv_coord_t)(MIN_DOME_MAX_SEG_H * dome_height_frac(t)));
        s_min_segs[i] = seg;
    }

    lv_obj_t *scale_row = make_plain_container(rpm_wrap);
    lv_obj_set_size(scale_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scale_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scale_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i <= 9; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", i);
        make_label(scale_row, buf, DASH_FONT_LABEL14, lv_color_hex(0x6a6a6a));
    }

    /* ---- bottom half: just MPH and the RPM number, nothing else ---- */
    lv_obj_t *bottom = make_plain_container(root);
    lv_obj_set_size(bottom, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(bottom, 1);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_min_speed_val = make_label(bottom, "0", DASH_FONT_SPEED, C_WHITE);
    lv_obj_t *unit = make_label(bottom, dash_config_get_speed_kph() ? "KPH" : "MPH", DASH_FONT_LABEL14, C_LABEL);
    lv_obj_set_style_text_letter_space(unit, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(unit, 4, LV_PART_MAIN);

    lv_obj_t *rpm_row = make_plain_container(bottom);
    lv_obj_set_size(rpm_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rpm_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rpm_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(rpm_row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(rpm_row, 24, LV_PART_MAIN);
    s_min_sub_label = make_label(rpm_row, "850", DASH_FONT_RPM, lv_color_hex(0x9a9ca2));
    make_label(rpm_row, "RPM", DASH_FONT_LABEL14, C_LABEL);

    /* thin bottom strip just for the settings button -- same safe 4-column
       slot as every other theme, just with nothing else in it now */
    lv_obj_t *strip = make_plain_container(root);
    lv_obj_set_size(strip, LV_PCT(100), (lv_coord_t)(SCR_H * 0.085f));
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(strip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(strip, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(strip, 56, LV_PART_MAIN);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    static lv_coord_t min_strip_col[5];
    static lv_coord_t min_strip_row[2];
    min_strip_col[0] = LV_GRID_FR(1); min_strip_col[1] = LV_GRID_CONTENT;
    min_strip_col[2] = LV_GRID_CONTENT; min_strip_col[3] = LV_GRID_FR(1);
    min_strip_col[4] = LV_GRID_TEMPLATE_LAST;
    min_strip_row[0] = LV_GRID_FR(1); min_strip_row[1] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(strip, min_strip_col, min_strip_row);
    build_settings_button(strip);

    /* small spacer under the strip for bezel clearance, same trick as
       the other themes -- lifts the whole strip up off the true edge */
    lv_obj_t *spacer = lv_obj_create(root);
    lv_obj_set_size(spacer, LV_PCT(100), 21);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(spacer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    return root;
}

/* ================= THEME: CLASSIC ANALOG (round needle gauges) ============== */

/* rotates a needle to point at `fraction` (0-1) of the gauge's sweep.
   Needle must already be positioned with its pivot set to its own
   bottom-center, sitting exactly at the gauge's center point. */
static void cla_set_needle_angle(lv_obj_t *needle, float fraction)
{
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;
    float angle_deg = (float)CLA_GAUGE_START_DEG + fraction * (float)CLA_GAUGE_SWEEP_DEG - 270.0f;
    lv_obj_set_style_transform_angle(needle, (int16_t)(angle_deg * 10.0f), LV_PART_MAIN);
}

/* builds one round needle gauge: dark face, colored arc fill, tick labels
   around the rim, a needle, and a center hub. Returns the arc (caller sets
   its value/range) and writes the needle handle to *out_needle. */
/* static radial tick mark, reusing the exact same rotation trick as the
   needle: position a short rect near the rim, then set its transform
   pivot far below its own bottom edge -- reaching all the way down to
   the gauge's true center -- so rotating it sweeps like a real tick. */
static void add_cla_tick(lv_obj_t *face, lv_coord_t cx, lv_coord_t cy, lv_coord_t arc_r,
                          float angle_deg_gauge_space, lv_coord_t len, lv_coord_t w, lv_color_t color)
{
    lv_obj_t *tick = lv_obj_create(face);
    lv_obj_add_flag(tick, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(tick, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tick, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tick, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tick, 0, LV_PART_MAIN);
    lv_obj_set_size(tick, w, len);
    lv_obj_set_pos(tick, cx - w / 2, cy - arc_r);
    lv_obj_set_style_transform_pivot_x(tick, w / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(tick, arc_r, LV_PART_MAIN);
    float needle_space_deg = angle_deg_gauge_space - 270.0f;
    lv_obj_set_style_transform_angle(tick, (int16_t)(needle_space_deg * 10.0f), LV_PART_MAIN);
}

static lv_obj_t *build_cla_gauge(lv_obj_t *parent, lv_coord_t box_size, lv_coord_t face_size, int tick_count,
                                  void (*tick_label_fn)(int i, int tick_count, char *buf, size_t buflen),
                                  bool has_redline, float redline_start_frac,
                                  lv_color_t indicator_color, lv_obj_t **out_needle)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, box_size, box_size);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

    /* chrome bezel: a lighter ring peeking out from behind the dark face */
    lv_obj_t *bezel = lv_obj_create(box);
    lv_obj_set_size(bezel, face_size, face_size);
    lv_obj_center(bezel);
    lv_obj_set_style_bg_color(bezel, lv_color_hex(0x8a8d94), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bezel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bezel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bezel, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(bezel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bezel, LV_OBJ_FLAG_CLICKABLE);

    /* the actual dark gauge face -- pad_all(0) here is the fix for the
       needle/arc misalignment bug: without it, this object's default
       theme padding shifts where child coordinates land relative to
       lv_obj_center() (which DOES account for padding), so the arc ends
       up centered correctly while the manually-positioned needle/ticks
       (which assumed zero padding) land on a different center point. */
    lv_coord_t face_inner = face_size - 12;
    lv_obj_t *face = lv_obj_create(bezel);
    lv_obj_set_size(face, face_inner, face_inner);
    lv_obj_center(face);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x0c0c0c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(face, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(face, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);

    lv_coord_t cx = face_inner / 2, cy = face_inner / 2;
    lv_coord_t arc_r = face_inner / 2 - 22;

    /* static dark track (always visible, full range) */
    lv_obj_t *arc_bg = lv_arc_create(face);
    lv_obj_set_size(arc_bg, arc_r * 2, arc_r * 2);
    lv_obj_center(arc_bg);
    lv_arc_set_bg_angles(arc_bg, CLA_GAUGE_START_DEG, (CLA_GAUGE_START_DEG + CLA_GAUGE_SWEEP_DEG) % 360);
    lv_arc_set_rotation(arc_bg, 0);
    lv_arc_set_range(arc_bg, 0, 100);
    lv_arc_set_value(arc_bg, 100);
    lv_obj_remove_style(arc_bg, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc_bg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_bg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_bg, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_bg, lv_color_hex(0x2a2a2a), LV_PART_INDICATOR);

    /* static redline zone -- a permanent red hint painted over the dark
       track for the top slice of the range, sitting under the live
       indicator so it still shows through wherever the needle hasn't
       reached yet */
    if (has_redline) {
        lv_obj_t *arc_redline = lv_arc_create(face);
        lv_obj_set_size(arc_redline, arc_r * 2, arc_r * 2);
        lv_obj_center(arc_redline);
        int redline_start_deg = (int)(CLA_GAUGE_START_DEG + redline_start_frac * CLA_GAUGE_SWEEP_DEG);
        lv_arc_set_bg_angles(arc_redline, redline_start_deg, (CLA_GAUGE_START_DEG + CLA_GAUGE_SWEEP_DEG) % 360);
        lv_arc_set_rotation(arc_redline, 0);
        lv_arc_set_range(arc_redline, 0, 100);
        lv_arc_set_value(arc_redline, 100);
        lv_obj_remove_style(arc_redline, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(arc_redline, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(arc_redline, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc_redline, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc_redline, 16, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_redline, lv_color_hex(0x6b1414), LV_PART_INDICATOR);
    }

    /* live value indicator -- only its INDICATOR part is visible, the
       base track is handled entirely by arc_bg/arc_redline above */
    lv_obj_t *arc = lv_arc_create(face);
    lv_obj_set_size(arc, arc_r * 2, arc_r * 2);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, CLA_GAUGE_START_DEG, (CLA_GAUGE_START_DEG + CLA_GAUGE_SWEEP_DEG) % 360);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, indicator_color, LV_PART_INDICATOR);

    /* minor tick marks -- 3 unlabeled dashes between each major number */
    const int minor_per_major = 4;
    for (int i = 0; i < tick_count * minor_per_major; i++) {
        if (i % minor_per_major == 0) continue; /* major tick drawn separately, below */
        float frac = (float)i / (float)(tick_count * minor_per_major);
        float ang = (float)CLA_GAUGE_START_DEG + frac * (float)CLA_GAUGE_SWEEP_DEG;
        add_cla_tick(face, cx, cy, arc_r - 10, ang, 12, 3, lv_color_hex(0x5a5a5a));
    }

    /* major tick marks + numbers */
    for (int i = 0; i <= tick_count; i++) {
        float frac = (float)i / (float)tick_count;
        float ang_deg = (float)CLA_GAUGE_START_DEG + frac * (float)CLA_GAUGE_SWEEP_DEG;
        bool in_redline = has_redline && (frac >= redline_start_frac);
        add_cla_tick(face, cx, cy, arc_r - 6, ang_deg, 20, 4, in_redline ? lv_color_hex(0xff5a3d) : C_WHITE);

        float ang = ang_deg * 3.14159265f / 180.0f;
        lv_coord_t lx = cx + (lv_coord_t)((arc_r - 42) * cosf(ang));
        lv_coord_t ly = cy + (lv_coord_t)((arc_r - 42) * sinf(ang));
        char buf[6];
        tick_label_fn(i, tick_count, buf, sizeof(buf));
        lv_obj_t *lbl = make_label(face, buf, DASH_FONT_TILEVAL, in_redline ? lv_color_hex(0xff5a3d) : lv_color_hex(0xe8e6e2));
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_pos(lbl, lx - 14, ly - 16);
    }

    /* needle */
    lv_coord_t needle_len = (lv_coord_t)(arc_r * 0.86f);
    lv_obj_t *needle = lv_obj_create(face);
    lv_obj_add_flag(needle, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(needle, 6, needle_len);
    lv_obj_set_pos(needle, cx - 3, cy - needle_len);
    lv_obj_set_style_bg_color(needle, lv_color_hex(0xff3b3b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(needle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(needle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(needle, 3, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(needle, 3, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(needle, needle_len, LV_PART_MAIN);
    cla_set_needle_angle(needle, 0.0f);

    /* two-layer hub for a bit of depth: dark outer ring + bright center */
    lv_obj_t *hub_ring = lv_obj_create(face);
    lv_obj_set_size(hub_ring, 24, 24);
    lv_obj_set_style_bg_color(hub_ring, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hub_ring, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(hub_ring, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hub_ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(hub_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hub_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(hub_ring);

    lv_obj_t *hub = lv_obj_create(face);
    lv_obj_set_size(hub, 14, 14);
    lv_obj_set_style_bg_color(hub, lv_color_hex(0xe8e8e8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(hub, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(hub);

    *out_needle = needle;
    return arc;
}

static void cla_rpm_tick_label(int i, int tick_count, char *buf, size_t buflen)
{
    (void)tick_count;
    snprintf(buf, buflen, "%d", i);
}
static void cla_speed_tick_label(int i, int tick_count, char *buf, size_t buflen)
{
    (void)tick_count;
    snprintf(buf, buflen, "%d", i * 20);
}

static lv_obj_t *build_ek9_combo_gauge(lv_obj_t *parent, lv_coord_t box_size, lv_coord_t face_size)
{
    /* same bezel+face construction as build_cla_gauge, sized down and
       hosting two small sub-dials (fuel bottom-left, temp top-right)
       instead of one full-sweep gauge */
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, box_size, box_size);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *bezel = lv_obj_create(box);
    lv_obj_set_size(bezel, face_size, face_size);
    lv_obj_center(bezel);
    lv_obj_set_style_bg_color(bezel, lv_color_hex(0x8a8d94), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bezel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bezel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bezel, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(bezel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bezel, LV_OBJ_FLAG_CLICKABLE);

    lv_coord_t face_inner = face_size - 12;
    lv_obj_t *face = lv_obj_create(bezel);
    lv_obj_set_size(face, face_inner, face_inner);
    lv_obj_center(face);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x0c0c0c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(face, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(face, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(face, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);

    lv_coord_t half = face_inner / 2;
    /* fuel hub: left-of-center, arc opens toward the left edge (F top, E bottom) */
    lv_coord_t fcx = half - face_inner / 5, fcy = half;
    /* temp hub: right-of-center, arc opens toward the right edge (H top, C bottom) */
    lv_coord_t tcx = half + face_inner / 5, tcy = half;
    lv_coord_t sub_r = face_inner / 4;

    /* small helper drawing one letter-zone tick+label at a given angle */
    #define EK9_SUBTICK(cx_, cy_, r_, ang_, redEnd_) do { \
        float rad_ = ((ang_) - 90.0f) * 3.14159265f / 180.0f; \
        lv_coord_t x1_ = (cx_) + (lv_coord_t)(((r_) - 8) * cosf(rad_)); \
        lv_coord_t y1_ = (cy_) + (lv_coord_t)(((r_) - 8) * sinf(rad_)); \
        lv_coord_t x2_ = (cx_) + (lv_coord_t)(((r_) + 4) * cosf(rad_)); \
        lv_coord_t y2_ = (cy_) + (lv_coord_t)(((r_) + 4) * sinf(rad_)); \
        static lv_point_t pts_[2]; pts_[0].x = x1_; pts_[0].y = y1_; pts_[1].x = x2_; pts_[1].y = y2_; \
        lv_obj_t *l_ = lv_line_create(face); \
        lv_line_set_points(l_, pts_, 2); \
        lv_obj_set_style_line_color(l_, (redEnd_) ? EK9_ORANGE : lv_color_hex(0xece8e6), LV_PART_MAIN); \
        lv_obj_set_style_line_width(l_, 2, LV_PART_MAIN); \
        lv_obj_add_flag(l_, LV_OBJ_FLAG_IGNORE_LAYOUT); \
    } while (0)

    /* fuel ticks: F at -12deg (upper-left-ish), E at -168deg, arc bulges west */
    for (int i = 0; i <= 8; i++) {
        float a = -12.0f - ((float)i / 8.0f) * 156.0f;
        EK9_SUBTICK(fcx, fcy, sub_r, a, i >= 7);
    }
    lv_obj_t *lbl_f = make_label(face, "F", DASH_FONT_LABEL, lv_color_hex(0xece8e6));
    lv_obj_t *lbl_e = make_label(face, "E", DASH_FONT_LABEL, lv_color_hex(0xece8e6));
    /* temp ticks: H at +12deg, C at +168deg, arc bulges east */
    for (int i = 0; i <= 8; i++) {
        float a = 12.0f + ((float)i / 8.0f) * 156.0f;
        EK9_SUBTICK(tcx, tcy, sub_r, a, i == 0);
    }
    lv_obj_t *lbl_h = make_label(face, "H", DASH_FONT_LABEL, lv_color_hex(0xece8e6));
    lv_obj_t *lbl_c = make_label(face, "C", DASH_FONT_LABEL, lv_color_hex(0xece8e6));
    #undef EK9_SUBTICK

    /* position the F/E/H/C letters explicitly (make_label doesn't take a
       position, and these are captured directly from their creation call
       rather than searched for afterward) */
    lv_obj_add_flag(lbl_f, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(lbl_e, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(lbl_h, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(lbl_c, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(lbl_f, fcx + (lv_coord_t)((sub_r + 22) * cosf((-14 - 90) * 3.14159265f/180)) - 8,
                           fcy + (lv_coord_t)((sub_r + 22) * sinf((-14 - 90) * 3.14159265f/180)) - 10);
    lv_obj_set_pos(lbl_e, fcx + (lv_coord_t)((sub_r + 20) * cosf((-166 - 90) * 3.14159265f/180)) - 8,
                           fcy + (lv_coord_t)((sub_r + 20) * sinf((-166 - 90) * 3.14159265f/180)) - 10);
    lv_obj_set_pos(lbl_h, tcx + (lv_coord_t)((sub_r + 22) * cosf((14 - 90) * 3.14159265f/180)) - 8,
                           tcy + (lv_coord_t)((sub_r + 22) * sinf((14 - 90) * 3.14159265f/180)) - 10);
    lv_obj_set_pos(lbl_c, tcx + (lv_coord_t)((sub_r + 20) * cosf((166 - 90) * 3.14159265f/180)) - 8,
                           tcy + (lv_coord_t)((sub_r + 20) * sinf((166 - 90) * 3.14159265f/180)) - 10);

    /* fuel needle */
    lv_coord_t fn_len = (lv_coord_t)(sub_r * 0.8f);
    s_ek9_fuel_needle = lv_obj_create(face);
    lv_obj_add_flag(s_ek9_fuel_needle, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_ek9_fuel_needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ek9_fuel_needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_ek9_fuel_needle, 4, fn_len);
    lv_obj_set_pos(s_ek9_fuel_needle, fcx - 2, fcy - fn_len);
    lv_obj_set_style_bg_color(s_ek9_fuel_needle, EK9_YELLOW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ek9_fuel_needle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ek9_fuel_needle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ek9_fuel_needle, 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(s_ek9_fuel_needle, 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(s_ek9_fuel_needle, fn_len, LV_PART_MAIN);
    lv_obj_set_style_transform_angle(s_ek9_fuel_needle, 0, LV_PART_MAIN);
    lv_obj_t *fhub = lv_obj_create(face);
    lv_obj_set_size(fhub, 10, 10);
    lv_obj_set_style_bg_color(fhub, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_radius(fhub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(fhub, 0, LV_PART_MAIN);
    lv_obj_clear_flag(fhub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(fhub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(fhub, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(fhub, fcx - 5, fcy - 5);

    /* temp needle */
    lv_coord_t tn_len = (lv_coord_t)(sub_r * 0.8f);
    s_ek9_temp_needle = lv_obj_create(face);
    lv_obj_add_flag(s_ek9_temp_needle, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_ek9_temp_needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ek9_temp_needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_ek9_temp_needle, 4, tn_len);
    lv_obj_set_pos(s_ek9_temp_needle, tcx - 2, tcy - tn_len);
    lv_obj_set_style_bg_color(s_ek9_temp_needle, EK9_YELLOW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ek9_temp_needle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ek9_temp_needle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ek9_temp_needle, 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(s_ek9_temp_needle, 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(s_ek9_temp_needle, tn_len, LV_PART_MAIN);
    lv_obj_set_style_transform_angle(s_ek9_temp_needle, 0, LV_PART_MAIN);
    lv_obj_t *thub = lv_obj_create(face);
    lv_obj_set_size(thub, 10, 10);
    lv_obj_set_style_bg_color(thub, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
    lv_obj_set_style_radius(thub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(thub, 0, LV_PART_MAIN);
    lv_obj_clear_flag(thub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(thub, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(thub, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(thub, tcx - 5, tcy - 5);

    return box;
}

static lv_obj_t *build_theme_classic(lv_obj_t *cluster)
{
    return build_si_cluster_theme(cluster, lv_color_hex(0xffed00), &s_si_yellow_state);

    lv_obj_t *root = make_plain_container(cluster);

    {
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_t *face = lv_img_create(root);
        lv_img_set_src(face, &s_ek9_face);
        lv_obj_add_flag(face, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(face, 0, 0);

        const struct {
            lv_coord_t cx;
            lv_coord_t cy;
            lv_coord_t width;
            lv_coord_t length;
            lv_obj_t **handle;
        } needles[] = {
            {162, 300, 5, 127, &s_cla_rpm_needle},
            {512, 300, 5, 164, &s_cla_speed_needle},
            {818, 300, 3, 56, &s_ek9_fuel_needle},
            {906, 300, 3, 56, &s_ek9_temp_needle},
        };
        for (size_t i = 0; i < sizeof(needles) / sizeof(needles[0]); i++) {
            lv_obj_t *needle = lv_obj_create(root);
            lv_obj_add_flag(needle, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_clear_flag(needle, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(needle, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(needle, needles[i].width, needles[i].length + 18);
            lv_obj_set_pos(needle, needles[i].cx - needles[i].width / 2, needles[i].cy - needles[i].length);
            lv_obj_set_style_bg_color(needle, EK9_YELLOW, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(needle, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(needle, 0, LV_PART_MAIN);
            lv_obj_set_style_radius(needle, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(needle, 0, LV_PART_MAIN);
            lv_obj_set_style_transform_pivot_x(needle, needles[i].width / 2, LV_PART_MAIN);
            lv_obj_set_style_transform_pivot_y(needle, needles[i].length, LV_PART_MAIN);
            *needles[i].handle = needle;
        }

        for (int i = 0; i < 6; i++) {
            s_cla_odo_digits[i] = lv_label_create(root);
            lv_obj_add_flag(s_cla_odo_digits[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_size(s_cla_odo_digits[i], 20, 26);
            lv_obj_set_pos(s_cla_odo_digits[i], 452 + i * 20, 247);
            lv_obj_set_style_text_font(s_cla_odo_digits[i], DASH_FONT_LABEL14, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_cla_odo_digits[i], i == 5 ? lv_color_hex(0x161616) : lv_color_hex(0xe8e6de), LV_PART_MAIN);
            lv_obj_set_style_text_align(s_cla_odo_digits[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_label_set_text(s_cla_odo_digits[i], "0");
        }
        for (int i = 0; i < 4; i++) {
            s_cla_trip_digits[i] = lv_label_create(root);
            lv_obj_add_flag(s_cla_trip_digits[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_size(s_cla_trip_digits[i], 17, 21);
            lv_obj_set_pos(s_cla_trip_digits[i], 478 + i * 17, 337);
            lv_obj_set_style_text_font(s_cla_trip_digits[i], DASH_FONT_LABEL, LV_PART_MAIN);
            lv_obj_set_style_text_color(s_cla_trip_digits[i], i == 3 ? lv_color_hex(0x161616) : lv_color_hex(0xe8e6de), LV_PART_MAIN);
            lv_obj_set_style_text_align(s_cla_trip_digits[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_label_set_text(s_cla_trip_digits[i], "0");
        }

        const struct { lv_coord_t x, y, size; } hubs[] = {
            {148, 286, 28}, {494, 282, 36}, {808, 290, 20}, {896, 290, 20},
        };
        for (size_t i = 0; i < sizeof(hubs) / sizeof(hubs[0]); i++) {
            lv_obj_t *hub = lv_obj_create(root);
            lv_obj_add_flag(hub, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_size(hub, hubs[i].size, hubs[i].size);
            lv_obj_set_pos(hub, hubs[i].x, hubs[i].y);
            lv_obj_set_style_bg_color(hub, lv_color_hex(0x0c0c0c), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(hub, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(hub, lv_color_hex(0x2e2e2e), LV_PART_MAIN);
            lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_pad_all(hub, 0, LV_PART_MAIN);
        }

        lv_obj_t *settings_hotspot = lv_obj_create(root);
        lv_obj_add_flag(settings_hotspot, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(settings_hotspot, 120, 58);
        lv_obj_set_pos(settings_hotspot, 452, 384);
        lv_obj_set_style_bg_opa(settings_hotspot, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(settings_hotspot, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(settings_hotspot, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(settings_hotspot, settings_btn_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_event_cb(settings_hotspot, settings_btn_cb, LV_EVENT_LONG_PRESSED, NULL);

        build_settings_button(root);
        lv_obj_t *corner_controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
        lv_obj_add_flag(corner_controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(corner_controls, DASH_CONTROL_ROW_WIDTH, 40);
        lv_obj_set_flex_align(corner_controls, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_align(corner_controls, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

        return root;
    }

    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x1c1e21), LV_PART_MAIN);
    lv_obj_set_style_bg_img_src(root, &ek9_carbon_tile, LV_PART_MAIN);
    lv_obj_set_style_bg_img_tiled(root, true, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *gauges_row = make_plain_container(root);
    lv_obj_set_width(gauges_row, LV_PCT(100));
    lv_obj_set_flex_grow(gauges_row, 1);
    lv_obj_set_flex_flow(gauges_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gauges_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(gauges_row, 10, LV_PART_MAIN);

    /* EK9 tach: 0-10 x1000rpm, redline at 8.5 -- matches the reference photos,
       not FULL_RED_RPM/MAXRPM (those drive the OTHER themes' bar-style redline;
       this gauge's numbers are printed on the face itself, so they need to
       actually read 0-10 to look like the real cluster) */
    s_cla_rpm_arc = build_cla_gauge(gauges_row, 360, 330, 10, cla_rpm_tick_label,
                                     true, 0.85f, EK9_ORANGE, &s_cla_rpm_needle);
    lv_arc_set_range(s_cla_rpm_arc, 0, 10000);
    lv_obj_set_style_arc_opa(s_cla_rpm_arc, LV_OPA_TRANSP, LV_PART_INDICATOR); /* real gauge has no filling ring, needle only */
    lv_obj_set_style_bg_color(s_cla_rpm_needle, EK9_YELLOW, LV_PART_MAIN);

    /* EK9 speedo: 0-180 (matches the actual JDM cluster's scale in the
       reference photos), no redline, no filling ring -- needle + static
       ticks only, same as the real thing */
    s_cla_speed_arc = build_cla_gauge(gauges_row, 400, 370, 9, cla_speed_tick_label,
                                       false, 0.0f, lv_color_hex(0x4d8fff), &s_cla_speed_needle);
    lv_arc_set_range(s_cla_speed_arc, 0, 180);
    lv_obj_set_style_arc_opa(s_cla_speed_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_cla_speed_needle, EK9_YELLOW, LV_PART_MAIN);

    /* odometer window embedded in the speedo face itself, matching the
       reference cluster's actual layout (not a separate footer element) */
    lv_obj_t *speed_face = lv_obj_get_parent(s_cla_speed_arc);
    lv_obj_t *odo_win = lv_obj_create(speed_face);
    lv_obj_set_size(odo_win, 118, 26);
    lv_obj_set_style_bg_color(odo_win, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(odo_win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(odo_win, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(odo_win, lv_color_hex(0x242424), LV_PART_MAIN);
    lv_obj_set_style_radius(odo_win, 2, LV_PART_MAIN);
    lv_obj_clear_flag(odo_win, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(odo_win, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(odo_win, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(odo_win, LV_ALIGN_CENTER, 0, -56);
    s_cla_odo_val = lv_label_create(odo_win);
    lv_label_set_text(s_cla_odo_val, "050412");
    lv_obj_set_style_text_font(s_cla_odo_val, DASH_FONT_LABEL14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_cla_odo_val, lv_color_hex(0xe8e6de), LV_PART_MAIN);
    lv_obj_center(s_cla_odo_val);

    build_ek9_combo_gauge(gauges_row, 360, 330);

    /* ---- footer: odo/fuel/all remaining channels + settings, one compact line ---- */
    lv_obj_t *footer = make_plain_container(root);
    lv_obj_set_size(footer, LV_PCT(100), 50);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(footer, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(footer, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(footer, 24, LV_PART_MAIN);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    static lv_coord_t cla_col_dsc[5];
    static lv_coord_t cla_row_dsc[2];
    cla_col_dsc[0] = LV_GRID_FR(1); cla_col_dsc[1] = LV_GRID_CONTENT;
    cla_col_dsc[2] = LV_GRID_CONTENT; cla_col_dsc[3] = LV_GRID_FR(1);
    cla_col_dsc[4] = LV_GRID_TEMPLATE_LAST;
    cla_row_dsc[0] = LV_GRID_FR(1); cla_row_dsc[1] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(footer, cla_col_dsc, cla_row_dsc);
    lv_obj_set_style_pad_column(footer, 18, LV_PART_MAIN);

    s_cla_footer_label = lv_label_create(footer);
    lv_label_set_text(s_cla_footer_label, "");
    lv_obj_set_style_text_font(s_cla_footer_label, DASH_FONT_LABEL, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_cla_footer_label, lv_color_hex(0x6b6e76), LV_PART_MAIN);
    lv_obj_set_grid_cell(s_cla_footer_label, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_width(s_cla_footer_label, 360);
    lv_label_set_long_mode(s_cla_footer_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_clear_flag(s_cla_footer_label, LV_OBJ_FLAG_CLICKABLE);

    build_settings_button(footer);

    lv_obj_t *spacer = lv_obj_create(root);
    lv_obj_set_size(spacer, LV_PCT(100), 21);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(spacer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    return root;
}

static lv_obj_t *si_create_needle(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy,
                                  lv_coord_t width, lv_coord_t length, lv_coord_t tail,
                                  lv_color_t color)
{
    lv_obj_t *needle = lv_obj_create(parent);
    lv_obj_add_flag(needle, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(needle, width, length + tail);
    lv_obj_set_pos(needle, cx - width / 2, cy - length);
    lv_obj_set_style_bg_color(needle, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(needle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(needle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(needle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(needle, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(needle, width / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(needle, length, LV_PART_MAIN);
    return needle;
}

static void si_set_needle_angle(lv_obj_t *needle, lv_coord_t radius, int16_t angle)
{
    lv_area_t needle_coords;
    lv_obj_get_coords(needle, &needle_coords);
    lv_coord_t pivot_x = needle_coords.x1 + lv_obj_get_width(needle) / 2;
    lv_coord_t pivot_y = needle_coords.y1 + radius;
    lv_coord_t redraw_radius = radius + lv_obj_get_width(needle);
    lv_area_t sweep = {
        .x1 = pivot_x - redraw_radius,
        .y1 = pivot_y - redraw_radius,
        .x2 = pivot_x + redraw_radius,
        .y2 = pivot_y + redraw_radius,
    };
    lv_obj_invalidate_area(lv_obj_get_parent(needle), &sweep);
    lv_obj_set_style_transform_angle(needle, angle, LV_PART_MAIN);
}

static lv_obj_t *build_si_cluster_theme(lv_obj_t *cluster, lv_color_t needle_color,
                                        si_theme_state_t *state)
{
    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *face = lv_img_create(root);
    lv_img_set_src(face, &s_si_cluster_face);
    lv_obj_add_flag(face, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(face, 0, 0);

    state->rpm_needle = si_create_needle(root, 159, 329, 6, 122, 18, needle_color);
    state->speed_needle = si_create_needle(root, 515, 295, 6, 154, 20, needle_color);
    state->fuel_needle = si_create_needle(root, 764, 329, 4, 64, 12, needle_color);
    state->temp_needle = si_create_needle(root, 967, 329, 4, 64, 12, needle_color);

    const struct { lv_coord_t cx, cy, size; } hubs[] = {
        {159, 329, 46}, {515, 295, 48}, {764, 329, 36}, {967, 329, 36},
    };
    for (size_t i = 0; i < sizeof(hubs) / sizeof(hubs[0]); i++) {
        lv_obj_t *hub = lv_obj_create(root);
        lv_obj_add_flag(hub, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(hub, hubs[i].size, hubs[i].size);
        lv_obj_set_pos(hub, hubs[i].cx - hubs[i].size / 2, hubs[i].cy - hubs[i].size / 2);
        lv_obj_set_style_bg_color(hub, lv_color_hex(0x111111), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(hub, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(hub, lv_color_hex(0x4b4d4d), LV_PART_MAIN);
        lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(hub, 0, LV_PART_MAIN);
    }

    build_settings_button(root);
    lv_obj_t *corner_controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_add_flag(corner_controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(corner_controls, DASH_CONTROL_ROW_WIDTH, 40);
    lv_obj_set_flex_align(corner_controls, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(corner_controls, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    return root;
}

static lv_obj_t *build_theme_civic_si(lv_obj_t *cluster)
{
    return build_si_cluster_theme(cluster, lv_color_hex(0xed493f), &s_si_state);
}

static void update_si_cluster_theme(const honda_dash_data_t *data, int rpm, float fuel,
                                    si_theme_state_t *state)
{
    if (rpm != state->last_rpm) {
        state->last_rpm = rpm;
        float value = (float)rpm / 10000.0f;
        if (value < 0) value = 0;
        if (value > 1) value = 1;
        si_set_needle_angle(state->rpm_needle, 122, (int16_t)((-133.0f + value * 266.0f) * 10.0f));
    }

    int speed_kph = (int)(data->speed_mph * 1.609344f + 0.5f);
    if (speed_kph != state->last_speed) {
        state->last_speed = speed_kph;
        float value = (float)speed_kph / 180.0f;
        if (value < 0) value = 0;
        if (value > 1) value = 1;
        si_set_needle_angle(state->speed_needle, 154, (int16_t)((-128.0f + value * 256.0f) * 10.0f));
    }

    int fuel_i = (int)(fuel + 0.5f);
    if (fuel_i != state->last_fuel) {
        state->last_fuel = fuel_i;
        float value = fuel / 100.0f;
        if (value < 0) value = 0;
        if (value > 1) value = 1;
        si_set_needle_angle(state->fuel_needle, 64, (int16_t)((121.0f - value * 71.0f) * 10.0f));
    }

    int ect_i = (int)(data->ect_f + 0.5f);
    if (ect_i != state->last_ect) {
        state->last_ect = ect_i;
        float value = (data->ect_f - 150.0f) / 90.0f;
        if (value < 0) value = 0;
        if (value > 1) value = 1;
        si_set_needle_angle(state->temp_needle, 64, (int16_t)((-121.0f + value * 71.0f) * 10.0f));
    }
}

static void update_theme_civic_si(const honda_dash_data_t *data, int rpm, float fuel)
{
    update_si_cluster_theme(data, rpm, fuel, &s_si_state);
}

#endif

static lv_obj_t *rpk_make_field(lv_obj_t *parent, int slot,
                                lv_coord_t x, lv_coord_t y, lv_coord_t width)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, 76);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(panel, system_field_long_press_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)(SYSTEM_FIELD_RACE * 16 + slot));
    add_press_feedback(panel);

    lv_obj_t *caption_label = make_label(panel, "", DASH_FONT_LABEL14, lv_color_hex(0x05324a));
    lv_obj_set_pos(caption_label, 10, 0);
    lv_obj_set_width(caption_label, width - 20);
    lv_obj_set_style_text_align(caption_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *value = make_label(panel, "--", DASH_FONT_TILEVAL, lv_color_hex(0x021c30));
    lv_obj_set_pos(value, 10, 20);
    lv_obj_set_width(value, width - 20);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_rpk_field_label[slot] = caption_label;
    s_rpk_field_val[slot] = value;
    return value;
}

static lv_obj_t *build_theme_race_lcd(lv_obj_t *cluster)
{
    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, RPK_LCD_BLUE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *lamp_bezel = lv_obj_create(root);
    lv_obj_add_flag(lamp_bezel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(lamp_bezel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(lamp_bezel, 0, 0);
    lv_obj_set_size(lamp_bezel, 1024, 68);
    lv_obj_set_style_bg_color(lamp_bezel, lv_color_hex(0x080b0d), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lamp_bezel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(lamp_bezel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(lamp_bezel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lamp_bezel, 0, LV_PART_MAIN);

    for (int index = 0; index < RPK_LAMP_COUNT; ++index) {
        lv_obj_t *lamp = lv_obj_create(root);
        lv_obj_add_flag(lamp, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(lamp, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(lamp, 262 + index * 52, 22);
        lv_obj_set_size(lamp, 28, 28);
        lv_obj_set_style_bg_color(lamp, lv_color_hex(0x252a2c), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lamp, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(lamp, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(lamp, lv_color_hex(0x41484b), LV_PART_MAIN);
        lv_obj_set_style_radius(lamp, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_pad_all(lamp, 0, LV_PART_MAIN);
        s_rpk_lamps[index] = lamp;
    }

    for (int index = 0; index < RPK_TACH_COUNT; ++index) {
        float normalized = ((float)index - (RPK_TACH_COUNT - 1) * 0.5f) /
                           ((RPK_TACH_COUNT - 1) * 0.5f);
        lv_coord_t y = 82 + (lv_coord_t)(normalized * normalized * 122.0f);
        lv_obj_t *cell = lv_obj_create(root);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(cell, 68 + index * 25, y);
        lv_obj_set_size(cell, 14, 34);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0x087dbb), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(cell, LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(cell, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
        s_rpk_tach[index] = cell;
    }

    static const lv_coord_t scale_x[9] = {86, 184, 282, 380, 478, 576, 674, 772, 870};
    static const lv_coord_t scale_y[9] = {224, 176, 142, 122, 116, 122, 142, 176, 224};
    for (int index = 0; index < 9; ++index) {
        char text[2] = {(char)('1' + index), '\0'};
        lv_obj_t *scale = make_label(root, text, DASH_FONT_LABEL14, lv_color_hex(0x05324a));
        lv_obj_set_pos(scale, scale_x[index], scale_y[index]);
    }

    lv_obj_t *rpm_label = make_label(root, "TACH", DASH_FONT_LABEL14, lv_color_hex(0x05324a));
    lv_obj_set_pos(rpm_label, 738, 246);
    s_rpk_rpm_val = make_label(root, "0", DASH_FONT_RPM, lv_color_hex(0x021c30));
    lv_obj_set_pos(s_rpk_rpm_val, 688, 270);
    lv_obj_set_width(s_rpk_rpm_val, 240);
    lv_obj_set_style_text_align(s_rpk_rpm_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *gear_label = make_label(root, "GEAR", DASH_FONT_LABEL14, lv_color_hex(0x05324a));
    lv_obj_align(gear_label, LV_ALIGN_TOP_MID, 0, 210);
    s_rpk_gear_val = make_label(root, "N", DASH_FONT_GEAR, lv_color_hex(0x021c30));
    lv_obj_set_width(s_rpk_gear_val, 140);
    lv_obj_set_style_text_align(s_rpk_gear_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_rpk_gear_val, LV_ALIGN_TOP_MID, 0, 234);

    s_rpk_speed_label = make_label(root, "MPH", DASH_FONT_LABEL14, lv_color_hex(0x05324a));
    lv_obj_set_pos(s_rpk_speed_label, 212, 246);
    s_rpk_speed_val = make_label(root, "0", DASH_FONT_SPEED, lv_color_hex(0x021c30));
    lv_obj_set_pos(s_rpk_speed_val, 160, 270);
    lv_obj_set_width(s_rpk_speed_val, 220);
    lv_obj_set_style_text_align(s_rpk_speed_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    rpk_make_field(root, 0, 52, 360, 210);
    rpk_make_field(root, 1, 286, 360, 210);
    rpk_make_field(root, 2, 520, 360, 210);
    rpk_make_field(root, 3, 754, 360, 210);
    rpk_make_field(root, 4, 168, 462, 210);
    rpk_make_field(root, 5, 407, 462, 210);
    rpk_make_field(root, 6, 646, 462, 210);
    for (int slot = 0; slot < DASH_CONFIG_RACE_FIELD_COUNT; ++slot) {
        system_field_refresh_identity(SYSTEM_FIELD_RACE, slot);
    }

    build_settings_button(root, true);
    lv_obj_t *corner_controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_add_flag(corner_controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(corner_controls, DASH_CONTROL_ROW_WIDTH, 40);
    lv_obj_set_flex_align(corner_controls, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(corner_controls, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    return root;
}

static void update_theme_race_lcd(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    (void)limiter_hit;
    static int last_metric = -1;
    int metric = (dash_config_get_temperature_celsius() ? 1 : 0) |
                 (dash_config_get_pressure_kpa() ? 2 : 0);
    if (metric != last_metric) {
        last_metric = metric;
        for (int slot = 0; slot < DASH_CONFIG_RACE_FIELD_COUNT; ++slot) {
            system_field_refresh_identity(SYSTEM_FIELD_RACE, slot);
        }
    }
    if (rpm != s_rpk_last_rpm) {
        s_rpk_last_rpm = rpm;
        lv_label_set_text_fmt(s_rpk_rpm_val, "%d", rpm);
    }

    if (data->gear != s_rpk_last_gear) {
        s_rpk_last_gear = data->gear;
        if (data->gear <= 0) lv_label_set_text(s_rpk_gear_val, "N");
        else lv_label_set_text_fmt(s_rpk_gear_val, "%d", data->gear);
    }
    int speed = (int)(data->speed_mph + 0.5f);
    if (speed != s_rpk_last_speed) {
        s_rpk_last_speed = speed;
        lv_label_set_text_fmt(s_rpk_speed_val, "%d", speed);
    }
    int speed_kph = dash_config_get_speed_kph() ? 1 : 0;
    if (speed_kph != s_rpk_last_speed_kph) {
        s_rpk_last_speed_kph = speed_kph;
        lv_label_set_text(s_rpk_speed_label, speed_kph ? "KPH" : "MPH");
    }

    float values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg, fuel,
    };
    bool available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        true, true, true, true, true, true, true, data->oil_valid,
        data->duty_valid, data->knock_valid, true,
    };
    for (int slot = 0; slot < DASH_CONFIG_RACE_FIELD_COUNT; ++slot) {
        int channel = dash_config_get_race_field_channel(slot);
        float displayed_value = system_field_display_value(channel, values[channel]);
        system_field_refresh_live_unit(SYSTEM_FIELD_RACE, slot, channel, values[channel]);
        char text[16];
        if (!available[channel]) snprintf(text, sizeof(text), "N/A");
        else if (channel == TILE_AFR || channel == TILE_MAP || channel == TILE_BATT || channel == TILE_KNOCK) {
            snprintf(text, sizeof(text), "%.1f", displayed_value);
        } else {
            float value = displayed_value;
            snprintf(text, sizeof(text), "%d", (int)(value + (value >= 0 ? 0.5f : -0.5f)));
        }
        if (strcmp(text, s_rpk_last_field_text[slot]) != 0) {
            snprintf(s_rpk_last_field_text[slot], sizeof(s_rpk_last_field_text[slot]), "%s", text);
            lv_label_set_text(s_rpk_field_val[slot], text);
        }
        lv_obj_set_style_text_color(s_rpk_field_val[slot],
            system_field_value_color(channel, values[channel], available[channel]), LV_PART_MAIN);
    }
}

static const char *const HAL_CHANNEL_NAMES[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
    "Coolant", "Intake Air", "AFR", "Ignition Timing", "Boost",
    "Battery", "Throttle", "Oil Pressure", "Injector Duty", "Knock", "Fuel",
};

static const char *hal_channel_unit(int channel)
{
    if (channel == TILE_ECT || channel == TILE_IAT) return dash_config_get_temperature_celsius() ? "DEG C" : "DEG F";
    if (channel == TILE_AFR) return "RATIO";
    if (channel == TILE_TIMING || channel == TILE_KNOCK) return "DEG";
    if (channel == TILE_MAP || channel == TILE_OIL) return dash_config_get_pressure_kpa() ? "KPA" : "PSI";
    if (channel == TILE_BATT) return "V";
    if (channel == TILE_TPS || channel == TILE_DUTY || channel == TILE_COUNT) return "%";
    return "";
}

static bool system_field_is_vacuum(int channel, float value)
{
    return channel == TILE_MAP && !dash_config_get_pressure_kpa() && value < 0.0f;
}

static float system_field_display_value(int channel, float value)
{
    return system_field_is_vacuum(channel, value) ? value * 2.03602f : value;
}

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static void system_field_refresh_live_unit(system_field_theme_t theme, int slot,
                                           int channel, float value)
{
    const char *unit = system_field_is_vacuum(channel, value) ? "inHg" : hal_channel_unit(channel);
    if (theme == SYSTEM_FIELD_MODERN && slot >= 0 && slot < DASH_CONFIG_MODERN_FIELD_COUNT) {
        set_label_text_if_changed(s_tiles[slot].unit, unit);
    } else if (theme == SYSTEM_FIELD_RACE && slot >= 0 && slot < DASH_CONFIG_RACE_FIELD_COUNT) {
        char caption[32];
        snprintf(caption, sizeof(caption), "%s  %s", HAL_CHANNEL_NAMES[channel], unit);
        set_label_text_if_changed(s_rpk_field_label[slot], caption);
    } else if (theme == SYSTEM_FIELD_ENDURANCE && slot >= 0 && slot < DASH_CONFIG_ENDURANCE_FIELD_COUNT) {
        set_label_text_if_changed(s_end_field_unit[slot], unit);
    } else if (theme == SYSTEM_FIELD_TOURING && slot >= 0 && slot < DASH_CONFIG_TOURING_FIELD_COUNT) {
        set_label_text_if_changed(s_tour_field_unit[slot], unit);
    } else if (theme == SYSTEM_FIELD_HAL && slot >= 0 && slot < DASH_CONFIG_HAL_FIELD_COUNT) {
        set_label_text_if_changed(s_hal_field_unit[slot], unit);
    }
}

static void hal_refresh_field_identity(int slot)
{
    if (slot < 0 || slot >= DASH_CONFIG_HAL_FIELD_COUNT) return;
    int channel = dash_config_get_hal_field_channel(slot);
    if (channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) channel = 0;
    if (s_hal_field_label[slot]) lv_label_set_text(s_hal_field_label[slot], HAL_CHANNEL_NAMES[channel]);
    if (s_hal_field_unit[slot]) lv_label_set_text(s_hal_field_unit[slot], hal_channel_unit(channel));
}

static int system_field_get_channel(system_field_theme_t theme, int slot)
{
    if (theme == SYSTEM_FIELD_MODERN) return dash_config_get_modern_field_channel(slot);
    if (theme == SYSTEM_FIELD_RACE) return dash_config_get_race_field_channel(slot);
    if (theme == SYSTEM_FIELD_ENDURANCE) return dash_config_get_endurance_field_channel(slot);
    if (theme == SYSTEM_FIELD_TOURING) return dash_config_get_touring_field_channel(slot);
    return dash_config_get_hal_field_channel(slot);
}

static void system_field_set_channel(system_field_theme_t theme, int slot, int channel)
{
    if (theme == SYSTEM_FIELD_MODERN) dash_config_set_modern_field_channel(slot, channel);
    else if (theme == SYSTEM_FIELD_RACE) dash_config_set_race_field_channel(slot, channel);
    else if (theme == SYSTEM_FIELD_ENDURANCE) dash_config_set_endurance_field_channel(slot, channel);
    else if (theme == SYSTEM_FIELD_TOURING) dash_config_set_touring_field_channel(slot, channel);
    else dash_config_set_hal_field_channel(slot, channel);
}

static void system_field_refresh_identity(system_field_theme_t theme, int slot)
{
    int channel = system_field_get_channel(theme, slot);
    if (channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) channel = 0;
    if (theme == SYSTEM_FIELD_MODERN) {
        if (slot < 0 || slot >= DASH_CONFIG_MODERN_FIELD_COUNT) return;
        if (s_tiles[slot].label) lv_label_set_text(s_tiles[slot].label, HAL_CHANNEL_NAMES[channel]);
        if (s_tiles[slot].unit) lv_label_set_text(s_tiles[slot].unit, hal_channel_unit(channel));
        s_tiles[slot].last_text[0] = '\0';
    } else if (theme == SYSTEM_FIELD_RACE) {
        if (slot < 0 || slot >= DASH_CONFIG_RACE_FIELD_COUNT) return;
        if (s_rpk_field_label[slot]) {
            char caption[32];
            snprintf(caption, sizeof(caption), "%s  %s", HAL_CHANNEL_NAMES[channel], hal_channel_unit(channel));
            lv_label_set_text(s_rpk_field_label[slot], caption);
        }
        s_rpk_last_field_text[slot][0] = '\0';
    } else if (theme == SYSTEM_FIELD_ENDURANCE) {
        if (slot < 0 || slot >= DASH_CONFIG_ENDURANCE_FIELD_COUNT) return;
        if (s_end_field_label[slot]) lv_label_set_text(s_end_field_label[slot], HAL_CHANNEL_NAMES[channel]);
        if (s_end_field_unit[slot]) lv_label_set_text(s_end_field_unit[slot], hal_channel_unit(channel));
        s_end_last_field_text[slot][0] = '\0';
    } else if (theme == SYSTEM_FIELD_TOURING) {
        if (slot < 0 || slot >= DASH_CONFIG_TOURING_FIELD_COUNT) return;
        if (s_tour_field_label[slot]) lv_label_set_text(s_tour_field_label[slot], HAL_CHANNEL_NAMES[channel]);
        if (s_tour_field_unit[slot]) lv_label_set_text(s_tour_field_unit[slot], hal_channel_unit(channel));
        s_tour_last_field_text[slot][0] = '\0';
    } else {
        hal_refresh_field_identity(slot);
        s_hal_last_field_text[slot][0] = '\0';
    }
}

static void hal_channel_modal_close(void)
{
    lv_obj_t *modal = s_hal_channel_modal;
    s_hal_channel_modal = NULL;
    s_hal_channel_dropdown = NULL;
    s_hal_threshold_panel = NULL;
    s_hal_edit_slot = -1;
    memset(s_cfg_threshold_sliders, 0, sizeof(s_cfg_threshold_sliders));
    memset(s_cfg_threshold_labels, 0, sizeof(s_cfg_threshold_labels));
    if (modal) {
        ui_fade(modal, lv_obj_get_style_opa(modal, LV_PART_MAIN), LV_OPA_TRANSP,
                120, ui_delete_after_anim_cb);
    }
}

static int system_field_channel_thresholds(int channel, dash_config_threshold_t out[4])
{
    switch (channel) {
        case TILE_ECT: out[0] = DASH_CONFIG_THRESHOLD_ECT_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_ECT_HIGH; return 2;
        case TILE_IAT: out[0] = DASH_CONFIG_THRESHOLD_IAT_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_IAT_HIGH; return 2;
        case TILE_AFR:
            out[0] = DASH_CONFIG_THRESHOLD_AFR_RICH_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_AFR_RICH;
            out[2] = DASH_CONFIG_THRESHOLD_AFR_LEAN_YELLOW; out[3] = DASH_CONFIG_THRESHOLD_AFR_LEAN;
            return 4;
        case TILE_MAP: out[0] = DASH_CONFIG_THRESHOLD_MAP_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_MAP_HIGH; return 2;
        case TILE_BATT: out[0] = DASH_CONFIG_THRESHOLD_BATT_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_BATT_LOW; return 2;
        case TILE_TPS: out[0] = DASH_CONFIG_THRESHOLD_TPS_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_TPS_HIGH; return 2;
        case TILE_OIL: out[0] = DASH_CONFIG_THRESHOLD_OIL_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_OIL_LOW; return 2;
        case TILE_DUTY: out[0] = DASH_CONFIG_THRESHOLD_DUTY_YELLOW; out[1] = DASH_CONFIG_THRESHOLD_DUTY_HIGH; return 2;
        case TILE_KNOCK: out[0] = DASH_CONFIG_THRESHOLD_KNOCK_AMBER; out[1] = DASH_CONFIG_THRESHOLD_KNOCK_RED; return 2;
        default: return 0;
    }
}

static void hal_channel_thresholds_rebuild(void)
{
    if (!s_hal_threshold_panel || !s_hal_channel_dropdown) return;
    lv_obj_clean(s_hal_threshold_panel);
    memset(s_cfg_threshold_sliders, 0, sizeof(s_cfg_threshold_sliders));
    memset(s_cfg_threshold_labels, 0, sizeof(s_cfg_threshold_labels));

    dash_config_threshold_t thresholds[4];
    int count = system_field_channel_thresholds((int)lv_dropdown_get_selected(s_hal_channel_dropdown), thresholds);
    if (count == 0) {
        lv_obj_t *note = make_label(s_hal_threshold_panel, "No warning color limits for this value.",
                                    DASH_FONT_LABEL14, C_LABEL);
        lv_obj_set_width(note, LV_PCT(100));
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    for (int index = 0; index < count; ++index) {
        dash_config_threshold_t threshold = thresholds[index];
        lv_obj_t *card = lv_obj_create(s_hal_threshold_panel);
        lv_obj_set_size(card, 330, 94);
        lv_obj_set_style_bg_color(card, C_VOID, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);

        lv_obj_t *top = make_plain_container(card);
        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(top, THRESHOLD_UI[threshold].label, DASH_FONT_LABEL, C_LABEL);
        int value = dash_config_get_threshold_tenths(threshold);
        char value_text[24];
        if (THRESHOLD_UI[threshold].step_tenths == 1 || THRESHOLD_UI[threshold].step_tenths == 5) {
            snprintf(value_text, sizeof(value_text), "%.1f%s", value / 10.0f, THRESHOLD_UI[threshold].unit);
        } else {
            snprintf(value_text, sizeof(value_text), "%d%s", value / 10, THRESHOLD_UI[threshold].unit);
        }
        s_cfg_threshold_labels[threshold] = make_label(top, value_text, DASH_FONT_LABEL14, C_WHITE);

        lv_obj_t *slider = lv_slider_create(card);
        configure_menu_slider(slider);
        s_cfg_threshold_sliders[threshold] = slider;
        lv_obj_set_size(slider, LV_PCT(100), 14);
        lv_slider_set_range(slider, THRESHOLD_UI[threshold].min_tenths, THRESHOLD_UI[threshold].max_tenths);
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, (index % 2 == 0) ? C_AMBER : C_RED, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, C_WHITE, LV_PART_KNOB);
        lv_obj_add_event_cb(slider, cfg_threshold_slider_cb, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)threshold);
        lv_obj_add_event_cb(slider, cfg_threshold_slider_released_cb, LV_EVENT_RELEASED,
                            (void *)(intptr_t)threshold);
    }
}

static void hal_channel_dropdown_changed_cb(lv_event_t *e)
{
    (void)e;
    hal_channel_thresholds_rebuild();
}

static void hal_channel_cancel_cb(lv_event_t *e)
{
    (void)e;
    hal_channel_modal_close();
}

static void hal_channel_save_cb(lv_event_t *e)
{
    (void)e;
    if (s_hal_edit_slot >= 0 && s_hal_channel_dropdown) {
        int slot = s_hal_edit_slot;
        system_field_set_channel(s_system_edit_theme, slot,
                                 (int)lv_dropdown_get_selected(s_hal_channel_dropdown));
        system_field_refresh_identity(s_system_edit_theme, slot);
    }
    hal_channel_modal_close();
}

static void system_field_long_press_cb(lv_event_t *e)
{
    if (s_hal_channel_modal) return;
    int encoded = (int)(intptr_t)lv_event_get_user_data(e);
    system_field_theme_t theme = (system_field_theme_t)(encoded / 16);
    int slot = encoded % 16;
    int slot_count = theme == SYSTEM_FIELD_MODERN ? DASH_CONFIG_MODERN_FIELD_COUNT :
                     (theme == SYSTEM_FIELD_RACE ? DASH_CONFIG_RACE_FIELD_COUNT :
                     (theme == SYSTEM_FIELD_ENDURANCE ? DASH_CONFIG_ENDURANCE_FIELD_COUNT :
                     (theme == SYSTEM_FIELD_TOURING ? DASH_CONFIG_TOURING_FIELD_COUNT :
                      DASH_CONFIG_HAL_FIELD_COUNT)));
    if (slot < 0 || slot >= slot_count) return;
    s_system_edit_theme = theme;
    s_hal_edit_slot = slot;

    s_hal_channel_modal = lv_obj_create(s_cluster);
    lv_obj_add_flag(s_hal_channel_modal, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(s_hal_channel_modal, SCR_W, SCR_H);
    lv_obj_set_pos(s_hal_channel_modal, 0, 0);
    lv_obj_set_style_bg_color(s_hal_channel_modal, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_hal_channel_modal, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_hal_channel_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hal_channel_modal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_hal_channel_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_hal_channel_modal);
    ui_fade(s_hal_channel_modal, LV_OPA_TRANSP, LV_OPA_COVER, 160, NULL);

    lv_obj_t *panel = lv_obj_create(s_hal_channel_modal);
    lv_obj_set_size(panel, 760, 520);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 24, LV_PART_MAIN);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);

    make_label(panel, "SELECT GAUGE CHANNEL", DASH_FONT_LABEL14, C_WHITE);
    s_hal_channel_dropdown = lv_dropdown_create(panel);
    lv_obj_set_width(s_hal_channel_dropdown, LV_PCT(100));
    lv_dropdown_set_options(s_hal_channel_dropdown,
        "Coolant\nIntake Air\nAFR\nIgnition Timing\nBoost\nBattery\nThrottle\nOil Pressure\nInjector Duty\nKnock\nFuel");
    lv_dropdown_set_selected(s_hal_channel_dropdown,
                             (uint16_t)system_field_get_channel(theme, slot));
    lv_obj_set_style_bg_color(s_hal_channel_dropdown, C_VOID, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_hal_channel_dropdown, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_hal_channel_dropdown, C_WHITE, LV_PART_MAIN);
    lv_obj_add_event_cb(s_hal_channel_dropdown, hal_channel_dropdown_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(panel, "WARNING COLOR LIMITS", DASH_FONT_LABEL, C_LABEL_DIM);
    s_hal_threshold_panel = make_plain_container(panel);
    lv_obj_set_size(s_hal_threshold_panel, LV_PCT(100), 214);
    lv_obj_set_flex_flow(s_hal_threshold_panel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_hal_threshold_panel, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_hal_threshold_panel, 10, LV_PART_MAIN);
    hal_channel_thresholds_rebuild();

    lv_obj_t *buttons = make_plain_container(panel);
    lv_obj_set_size(buttons, LV_PCT(100), 50);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(buttons, 14, LV_PART_MAIN);
    const char *button_text[2] = {"Cancel", "Apply"};
    lv_event_cb_t callbacks[2] = {hal_channel_cancel_cb, hal_channel_save_cb};
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *button = lv_obj_create(buttons);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_height(button, 50);
        lv_obj_set_style_bg_color(button, i == 1 ? C_RED : C_VOID, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, i == 1 ? C_RED : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, callbacks[i], LV_EVENT_CLICKED, NULL);
        add_press_feedback(button);
        lv_obj_center(make_label(button, button_text[i], DASH_FONT_LABEL14, C_WHITE));
    }
}

static lv_obj_t *hal_make_field(lv_obj_t *parent, int slot,
                                lv_coord_t x, lv_coord_t y, lv_coord_t width)
{
    lv_obj_t *field = lv_obj_create(parent);
    lv_obj_add_flag(field, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(field, x, y);
    lv_obj_set_size(field, width, 68);
    lv_obj_set_style_bg_color(field, lv_color_hex(0x101010), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 2, LV_PART_MAIN);
    lv_obj_set_style_border_side(field, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, HAL_RED, LV_PART_MAIN);
    lv_obj_set_style_radius(field, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(field, 7, LV_PART_MAIN);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(field, system_field_long_press_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)(SYSTEM_FIELD_HAL * 16 + slot));
    add_press_feedback(field);

    s_hal_field_label[slot] = make_label(field, "", DASH_FONT_LABEL, lv_color_hex(0x9a9a9a));
    lv_obj_align(s_hal_field_label[slot], LV_ALIGN_TOP_LEFT, 0, 0);
    s_hal_field_unit[slot] = make_label(field, "", DASH_FONT_LABEL, HAL_RED);
    lv_obj_align(s_hal_field_unit[slot], LV_ALIGN_TOP_RIGHT, 0, 0);
    hal_refresh_field_identity(slot);
    lv_obj_t *value = make_label(field, "--", DASH_FONT_TILEVAL, C_WHITE);
    lv_obj_set_width(value, LV_PCT(100));
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(value, LV_ALIGN_BOTTOM_RIGHT, 0, 2);
    return value;
}

static void hal_set_needle_line(lv_obj_t *needle, lv_point_t points[2],
                                float x, float y, float tail_length, float tip_length,
                                lv_coord_t margin)
{
    lv_coord_t x0 = HAL_GAUGE_CENTER - (lv_coord_t)(x * tail_length);
    lv_coord_t y0 = HAL_GAUGE_CENTER - (lv_coord_t)(y * tail_length);
    lv_coord_t x1 = HAL_GAUGE_CENTER + (lv_coord_t)(x * tip_length);
    lv_coord_t y1 = HAL_GAUGE_CENTER + (lv_coord_t)(y * tip_length);
    lv_coord_t left = LV_MIN(x0, x1) - margin;
    lv_coord_t top = LV_MIN(y0, y1) - margin;
    lv_coord_t right = LV_MAX(x0, x1) + margin;
    lv_coord_t bottom = LV_MAX(y0, y1) + margin;
    lv_obj_set_pos(needle, left, top);
    lv_obj_set_size(needle, right - left + 1, bottom - top + 1);
    points[0].x = x0 - left;
    points[0].y = y0 - top;
    points[1].x = x1 - left;
    points[1].y = y1 - top;
    lv_line_set_points(needle, points, 2);
}

static void hal_set_needle(lv_obj_t *base, lv_point_t base_points[2],
                           lv_obj_t *spine, lv_point_t spine_points[2], float fraction)
{
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    float angle_rad = (135.0f + fraction * 270.0f) * ((float)M_PI / 180.0f);
    float x = cosf(angle_rad);
    float y = sinf(angle_rad);
    hal_set_needle_line(base, base_points, x, y, 24.0f, 143.0f, 6);
    hal_set_needle_line(spine, spine_points, x, y, 16.0f, 143.0f, 3);
}

static lv_obj_t *hal_make_gauge(lv_obj_t *parent, lv_coord_t x, int scale_max, int scale_step,
                                int label_divisor, lv_color_t indicator_color,
                                lv_obj_t **out_needle_base, lv_point_t needle_base_points[2],
                                lv_obj_t **out_needle, lv_point_t needle_points[2])
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(arc, x, 108);
    lv_obj_set_size(arc, HAL_GAUGE_OUTER_SIZE, HAL_GAUGE_OUTER_SIZE);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 1000);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x242424), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, indicator_color, LV_PART_INDICATOR);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *inner = lv_obj_create(parent);
    lv_obj_add_flag(inner, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(inner, x + 57, 165);
    lv_obj_set_size(inner, HAL_GAUGE_INNER_SIZE, HAL_GAUGE_INNER_SIZE);
    lv_obj_set_style_bg_color(inner, lv_color_hex(0x050505), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(inner, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x282828), LV_PART_MAIN);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(inner, 0, LV_PART_MAIN);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t center = HAL_GAUGE_CENTER;
    const lv_coord_t tick_radius = 142;
    for (int i = 0; i <= 20; ++i) {
        bool major = (i % 2) == 0;
        lv_coord_t width = major ? 3 : 2;
        lv_coord_t length = major ? 12 : 7;
        lv_obj_t *tick = lv_obj_create(inner);
        lv_obj_add_flag(tick, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(tick, width, length);
        lv_obj_set_pos(tick, center - width / 2, center - tick_radius);
        lv_obj_set_style_bg_color(tick, major ? C_WHITE : lv_color_hex(0x777777), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(tick, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(tick, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(tick, 0, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_x(tick, width / 2, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(tick, tick_radius, LV_PART_MAIN);
        float angle = 135.0f + ((float)i / 20.0f) * 270.0f - 270.0f;
        lv_obj_set_style_transform_angle(tick, (int16_t)(angle * 10.0f), LV_PART_MAIN);
    }

    int label_count = scale_max / scale_step;
    for (int i = 0; i <= label_count; ++i) {
        float fraction = (float)i / (float)label_count;
        float angle_rad = (135.0f + fraction * 270.0f) * ((float)M_PI / 180.0f);
        char text[8];
        snprintf(text, sizeof(text), "%d", (i * scale_step) / label_divisor);
        lv_obj_t *label = make_label(inner, text, DASH_FONT_LABEL, C_WHITE);
        lv_obj_add_flag(label, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(label, 38, 18);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_pos(label,
                       center + (lv_coord_t)(cosf(angle_rad) * 114.0f) - 19,
                       center + (lv_coord_t)(sinf(angle_rad) * 114.0f) - 9);
    }

    lv_obj_t *needle_base = lv_line_create(inner);
    lv_obj_add_flag(needle_base, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(needle_base, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(needle_base, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_color(needle_base, lv_color_hex(0x050505), LV_PART_MAIN);
    lv_obj_set_style_line_width(needle_base, 10, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(needle_base, false, LV_PART_MAIN);

    lv_obj_t *needle = lv_line_create(inner);
    lv_obj_add_flag(needle, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(needle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_line_color(needle, indicator_color, LV_PART_MAIN);
    lv_obj_set_style_line_width(needle, 4, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(needle, false, LV_PART_MAIN);
    hal_set_needle(needle_base, needle_base_points, needle, needle_points, 0.0f);

    lv_obj_t *hub = lv_obj_create(inner);
    lv_obj_add_flag(hub, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(hub, 22, 22);
    lv_obj_set_pos(hub, center - 11, center - 11);
    lv_obj_set_style_bg_color(hub, lv_color_hex(0x111111), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(hub, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(hub, indicator_color, LV_PART_MAIN);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hub, 0, LV_PART_MAIN);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE);

    *out_needle_base = needle_base;
    *out_needle = needle;
    return arc;
}

static lv_obj_t *build_theme_haldash(lv_obj_t *cluster)
{
    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x050505), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *header_line = lv_obj_create(root);
    lv_obj_add_flag(header_line, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(header_line, 386, 18);
    lv_obj_set_size(header_line, 252, 2);
    lv_obj_set_style_bg_color(header_line, HAL_RED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header_line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header_line, 0, LV_PART_MAIN);
    lv_obj_clear_flag(header_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(header_line, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *brand = make_label(root, "HALDASH  //  COMPETITION DISPLAY", DASH_FONT_LABEL14, C_WHITE);
    lv_obj_add_flag(brand, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 25);

    s_hal_field_val[0] = hal_make_field(root, 0, 18, 61, 150);
    s_hal_field_val[1] = hal_make_field(root, 1, 178, 34, 150);
    s_hal_field_val[2] = hal_make_field(root, 2, 696, 34, 150);
    s_hal_field_val[3] = hal_make_field(root, 3, 856, 61, 150);
    s_hal_field_val[4] = hal_make_field(root, 4, 18, 512, 150);
    s_hal_field_val[5] = hal_make_field(root, 5, 178, 512, 150);
    s_hal_field_val[6] = hal_make_field(root, 6, 696, 512, 150);
    s_hal_field_val[7] = hal_make_field(root, 7, 856, 512, 150);

    int speed_max = dash_config_get_speed_kph() ? 320 : 200;
    int speed_step = dash_config_get_speed_kph() ? 40 : 20;
    s_hal_rpm_arc = hal_make_gauge(root, 82, 10000, 1000, 1000,
                                                                     HAL_RED,
                                                                     &s_hal_rpm_needle_base, s_hal_rpm_needle_base_points,
                                                                     &s_hal_rpm_needle, s_hal_rpm_needle_points);
    s_hal_speed_arc = hal_make_gauge(root, 512, speed_max, speed_step, 1,
                                                                         C_WHITE,
                                                                         &s_hal_speed_needle_base, s_hal_speed_needle_base_points,
                                                                         &s_hal_speed_needle, s_hal_speed_needle_points);

    lv_obj_t *rpm_caption = make_label(root, "ENGINE  RPM x1000", DASH_FONT_LABEL, lv_color_hex(0x8b8b8b));
    lv_obj_add_flag(rpm_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(rpm_caption, 180);
    lv_obj_set_style_text_align(rpm_caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(rpm_caption, 207, 136);

    lv_obj_t *speed_caption = make_label(root, "VEHICLE  SPEED", DASH_FONT_LABEL, lv_color_hex(0x8b8b8b));
    lv_obj_add_flag(speed_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(speed_caption, 180);
    lv_obj_set_style_text_align(speed_caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(speed_caption, 637, 136);

    s_hal_rpm_val = make_label(root, "0", DASH_FONT_HAL_VALUE, C_WHITE);
    lv_obj_add_flag(s_hal_rpm_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(s_hal_rpm_val, 212, 378);
    lv_obj_set_width(s_hal_rpm_val, 170);
    lv_obj_set_style_text_align(s_hal_rpm_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    s_hal_speed_val = make_label(root, "0", DASH_FONT_HAL_VALUE, C_WHITE);
    lv_obj_add_flag(s_hal_speed_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(s_hal_speed_val, 642, 378);
    lv_obj_set_width(s_hal_speed_val, 170);
    lv_obj_set_style_text_align(s_hal_speed_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_t *gear_caption = make_label(root, "GEAR", DASH_FONT_LABEL, HAL_RED);
    lv_obj_add_flag(gear_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(gear_caption, LV_ALIGN_TOP_MID, 0, 68);
    s_hal_gear_val = make_label(root, "N", DASH_FONT_GEAR, C_WHITE);
    lv_obj_add_flag(s_hal_gear_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_hal_gear_val, 80);
    lv_obj_set_style_text_align(s_hal_gear_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_hal_gear_val, LV_ALIGN_TOP_MID, 0, 82);

    build_settings_button(root, true);
    lv_obj_t *controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_add_flag(controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(controls, DASH_CONTROL_ROW_WIDTH, 40);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, -28);
    return root;
}

static void update_theme_haldash(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    int speed = (int)(data->speed_mph + 0.5f);
    int speed_max = dash_config_get_speed_kph() ? 320 : 200;
    static int last_needle_rpm = -1;
    static int last_needle_speed = -1;
    if (rpm != last_needle_rpm) {
        last_needle_rpm = rpm;
        hal_set_needle(s_hal_rpm_needle_base, s_hal_rpm_needle_base_points,
                       s_hal_rpm_needle, s_hal_rpm_needle_points,
                       rpm / 10000.0f);
    }
    if (speed != last_needle_speed) {
        last_needle_speed = speed;
        hal_set_needle(s_hal_speed_needle_base, s_hal_speed_needle_base_points,
                       s_hal_speed_needle, s_hal_speed_needle_points,
                       (float)speed / (float)speed_max);
    }

    static bool last_limiter_hit = false;
    static bool limiter_initialized = false;
    if (!limiter_initialized || limiter_hit != last_limiter_hit) {
        limiter_initialized = true;
        last_limiter_hit = limiter_hit;
        lv_obj_set_style_arc_color(s_hal_rpm_arc, limiter_hit ? C_RED : HAL_RED, LV_PART_INDICATOR);
    }

    static int last_rpm = -1;
    static int last_speed = -1;
    static int last_gear = -99;
    static int last_metric = -1;
    int metric = (dash_config_get_temperature_celsius() ? 1 : 0) |
                 (dash_config_get_pressure_kpa() ? 2 : 0);
    bool metric_changed = metric != last_metric;
    if (rpm != last_rpm) {
        last_rpm = rpm;
        lv_label_set_text_fmt(s_hal_rpm_val, "%d", rpm);
    }
    if (speed != last_speed) {
        last_speed = speed;
        lv_label_set_text_fmt(s_hal_speed_val, "%d", speed);
    }
    if (metric_changed) {
        last_metric = metric;
    }
    if (data->gear != last_gear) {
        last_gear = data->gear;
        if (data->gear <= 0) lv_label_set_text(s_hal_gear_val, "N");
        else lv_label_set_text_fmt(s_hal_gear_val, "%d", data->gear);
    }

    float channel_values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg, fuel,
    };
    bool channel_available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        true, true, true, true, true, true, true, data->oil_valid,
        data->duty_valid, data->knock_valid, true,
    };
    if (metric_changed) {
        for (int slot = 0; slot < DASH_CONFIG_HAL_FIELD_COUNT; ++slot) hal_refresh_field_identity(slot);
    }
    for (int slot = 0; slot < DASH_CONFIG_HAL_FIELD_COUNT; ++slot) {
        int channel = dash_config_get_hal_field_channel(slot);
        float displayed_value = system_field_display_value(channel, channel_values[channel]);
        system_field_refresh_live_unit(SYSTEM_FIELD_HAL, slot, channel, channel_values[channel]);
        char field_text[16];
        if (!channel_available[channel]) snprintf(field_text, sizeof(field_text), "N/A");
        else if (channel == TILE_AFR || channel == TILE_MAP || channel == TILE_BATT || channel == TILE_KNOCK) {
            snprintf(field_text, sizeof(field_text), "%.1f", displayed_value);
        } else {
            float value = displayed_value;
            snprintf(field_text, sizeof(field_text), "%d", (int)(value + (value >= 0 ? 0.5f : -0.5f)));
        }
        if (strcmp(field_text, s_hal_last_field_text[slot]) != 0) {
            snprintf(s_hal_last_field_text[slot], sizeof(s_hal_last_field_text[slot]), "%s", field_text);
            lv_label_set_text(s_hal_field_val[slot], field_text);
        }
        lv_obj_set_style_text_color(s_hal_field_val[slot],
            system_field_value_color(channel, channel_values[channel], channel_available[channel]),
            LV_PART_MAIN);
    }
}

static lv_obj_t *endurance_make_field(lv_obj_t *parent, int slot, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *field = lv_obj_create(parent);
    lv_obj_add_flag(field, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(field, x, y);
    lv_obj_set_size(field, 150, 126);
    lv_obj_set_style_bg_color(field, lv_color_hex(0x131416), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(0x34363a), LV_PART_MAIN);
    lv_obj_set_style_border_side(field, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_radius(field, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(field, 10, LV_PART_MAIN);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(field, system_field_long_press_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)(SYSTEM_FIELD_ENDURANCE * 16 + slot));
    add_press_feedback(field);

    s_end_field_label[slot] = make_label(field, "", DASH_FONT_LABEL, lv_color_hex(0xb7b9bc));
    lv_obj_set_width(s_end_field_label[slot], LV_PCT(100));
    lv_label_set_long_mode(s_end_field_label[slot], LV_LABEL_LONG_DOT);
    lv_obj_align(s_end_field_label[slot], LV_ALIGN_TOP_LEFT, 0, 0);
    s_end_field_unit[slot] = make_label(field, "", DASH_FONT_LABEL, HUNTER_RED);
    lv_obj_align(s_end_field_unit[slot], LV_ALIGN_TOP_LEFT, 0, 22);
    s_end_field_val[slot] = make_label(field, "--", DASH_FONT_HAL_VALUE, C_WHITE);
    lv_obj_set_width(s_end_field_val[slot], LV_PCT(100));
    lv_obj_set_style_text_align(s_end_field_val[slot], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(s_end_field_val[slot], LV_ALIGN_BOTTOM_RIGHT, 0, 3);
    system_field_refresh_identity(SYSTEM_FIELD_ENDURANCE, slot);
    return field;
}

static lv_obj_t *hunter_make_indicator(lv_obj_t *parent, const char *text, lv_obj_t **label)
{
    lv_obj_t *indicator = lv_obj_create(parent);
    lv_obj_set_size(indicator, 62, 34);
    lv_obj_set_style_bg_color(indicator, C_PANEL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(indicator, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(indicator, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(indicator, 5, LV_PART_MAIN);
    lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    *label = make_label(indicator, text, DASH_FONT_LABEL, C_LABEL_DIM);
    lv_obj_center(*label);
    return indicator;
}

static lv_obj_t *build_theme_endurance(lv_obj_t *cluster)
{
    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x090a0b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *logo = lv_img_create(root);
    lv_obj_add_flag(logo, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_img_set_src(logo, &huntertuned_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *tagline = make_label(root, "DYNO TUNING  /  BUILDING  /  RACING", DASH_FONT_LABEL, lv_color_hex(0x8b8d91));
    lv_obj_add_flag(tagline, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(tagline, LV_ALIGN_TOP_MID, 0, 76);

    lv_obj_t *bar = make_plain_container(root);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(bar, 32, 100);
    lv_obj_set_size(bar, 960, 58);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 4, LV_PART_MAIN);
    for (int i = 0; i < END_SEG_COUNT; ++i) {
        lv_obj_t *segment = lv_obj_create(bar);
        lv_obj_set_height(segment, 58);
        lv_obj_set_flex_grow(segment, 1);
        lv_obj_set_style_bg_color(segment, HUNTER_SEG_OFF, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(segment, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(segment, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(segment, 0, LV_PART_MAIN);
        lv_obj_clear_flag(segment, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE);
        s_end_segments[i] = segment;
    }

    make_label(root, "RPM", DASH_FONT_LABEL14, HUNTER_RED);
    lv_obj_t *rpm_caption = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_add_flag(rpm_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(rpm_caption, 280);
    lv_obj_set_style_text_align(rpm_caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(rpm_caption, 52, 180);
    s_end_rpm_val = make_label(root, "0", &lv_font_montserrat_48, C_WHITE);
    lv_obj_add_flag(s_end_rpm_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_end_rpm_val, 280);
    lv_obj_set_style_text_align(s_end_rpm_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_end_rpm_val, 52, 210);

    s_end_speed_unit = make_label(root, dash_config_get_speed_kph() ? "KPH" : "MPH", DASH_FONT_LABEL14, HUNTER_RED);
    lv_obj_add_flag(s_end_speed_unit, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_end_speed_unit, 280);
    lv_obj_set_style_text_align(s_end_speed_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_end_speed_unit, 372, 180);
    s_end_speed_val = make_label(root, "0", &lv_font_montserrat_48, C_WHITE);
    lv_obj_add_flag(s_end_speed_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_end_speed_val, 280);
    lv_obj_set_style_text_align(s_end_speed_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_end_speed_val, 372, 210);

    lv_obj_t *gear_caption = make_label(root, "GEAR", DASH_FONT_LABEL14, HUNTER_RED);
    lv_obj_add_flag(gear_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(gear_caption, 280);
    lv_obj_set_style_text_align(gear_caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(gear_caption, 692, 180);
    s_end_gear_val = make_label(root, "N", &lv_font_montserrat_48, C_WHITE);
    lv_obj_add_flag(s_end_gear_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_end_gear_val, 280);
    lv_obj_set_style_text_align(s_end_gear_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_end_gear_val, 692, 210);

    for (int slot = 0; slot < DASH_CONFIG_ENDURANCE_FIELD_COUNT; ++slot) {
        endurance_make_field(root, slot, 32 + slot * 160, 350);
    }

    lv_obj_t *indicators = make_plain_container(root);
    lv_obj_add_flag(indicators, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(indicators, DASH_CONTROL_ROW_WIDTH, 40);
    lv_obj_set_flex_flow(indicators, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(indicators, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(indicators, 12, LV_PART_MAIN);
    lv_obj_align(indicators, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    s_end_shift_light = hunter_make_indicator(indicators, "SHIFT", &s_end_shift_label);
    s_end_vtec_light = hunter_make_indicator(indicators, "VTEC", &s_end_vtec_label);
    s_end_cel_light = hunter_make_indicator(indicators, "CEL", &s_end_cel_label);

    build_settings_button(root, true);
    lv_obj_t *controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_add_flag(controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(controls, DASH_CONTROL_ROW_WIDTH, 40);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    return root;
}

static void update_theme_endurance(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    static int last_indicator_mask = -1;
    int indicator_mask = (rpm >= VTEC_RPM ? 1 : 0) | (data->cel ? 2 : 0);
    if (indicator_mask != last_indicator_mask) {
        last_indicator_mask = indicator_mask;
        bool vtec_on = (indicator_mask & 1) != 0;
        bool cel_on = (indicator_mask & 2) != 0;
        lv_color_t vtec_color = lv_color_hex(0x39ff8c);
        lv_obj_set_style_bg_color(s_end_vtec_light, vtec_on ? vtec_color : C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_end_vtec_light, vtec_on ? vtec_color : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_end_vtec_label, vtec_on ? lv_color_black() : C_LABEL_DIM, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_end_cel_light, cel_on ? HUNTER_RED : C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_end_cel_light, cel_on ? HUNTER_RED : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_end_cel_label, cel_on ? C_WHITE : C_LABEL_DIM, LV_PART_MAIN);
    }

    static int last_metric = -1;
    int metric = (dash_config_get_temperature_celsius() ? 1 : 0) |
                 (dash_config_get_pressure_kpa() ? 2 : 0) |
                 (dash_config_get_speed_kph() ? 4 : 0);
    if (metric != last_metric) {
        last_metric = metric;
        lv_label_set_text(s_end_speed_unit, dash_config_get_speed_kph() ? "KPH" : "MPH");
        for (int slot = 0; slot < DASH_CONFIG_ENDURANCE_FIELD_COUNT; ++slot) {
            system_field_refresh_identity(SYSTEM_FIELD_ENDURANCE, slot);
        }
    }

    static int last_bucket = -1;
    static bool last_limiter = false;
    int bucket = (rpm * END_SEG_COUNT) / (MAXRPM > 0 ? MAXRPM : 1);
    if (bucket < 0) bucket = 0;
    if (bucket > END_SEG_COUNT) bucket = END_SEG_COUNT;
    if (bucket != last_bucket || limiter_hit != last_limiter) {
        last_bucket = bucket;
        last_limiter = limiter_hit;
        for (int i = 0; i < END_SEG_COUNT; ++i) {
            lv_color_t color = HUNTER_SEG_OFF;
            if (i < bucket) {
                float fraction = (float)i / (float)(END_SEG_COUNT - 1);
                color = limiter_hit ? C_WHITE : (fraction > 0.78f ? C_WHITE : HUNTER_RED);
            }
            lv_obj_set_style_bg_color(s_end_segments[i], color, LV_PART_MAIN);
        }
    }

    static int last_rpm = -1;
    static int last_speed = -1;
    static int last_gear = -99;
    int speed = (int)(data->speed_mph + 0.5f);
    if (rpm != last_rpm) {
        last_rpm = rpm;
        lv_label_set_text_fmt(s_end_rpm_val, "%d", rpm);
    }
    if (speed != last_speed) {
        last_speed = speed;
        lv_label_set_text_fmt(s_end_speed_val, "%d", speed);
    }
    if (data->gear != last_gear) {
        last_gear = data->gear;
        if (data->gear <= 0) lv_label_set_text(s_end_gear_val, "N");
        else lv_label_set_text_fmt(s_end_gear_val, "%d", data->gear);
    }

    float values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg, fuel,
    };
    bool available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        true, true, true, true, true, true, true, data->oil_valid,
        data->duty_valid, data->knock_valid, true,
    };
    for (int slot = 0; slot < DASH_CONFIG_ENDURANCE_FIELD_COUNT; ++slot) {
        int channel = dash_config_get_endurance_field_channel(slot);
        if (channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) channel = 0;
        float displayed_value = system_field_display_value(channel, values[channel]);
        system_field_refresh_live_unit(SYSTEM_FIELD_ENDURANCE, slot, channel, values[channel]);
        char text[16];
        if (!available[channel]) snprintf(text, sizeof(text), "N/A");
        else if (channel == TILE_AFR || channel == TILE_MAP || channel == TILE_BATT || channel == TILE_KNOCK) {
            snprintf(text, sizeof(text), "%.1f", displayed_value);
        } else {
            float value = displayed_value;
            snprintf(text, sizeof(text), "%d", (int)(value + (value >= 0 ? 0.5f : -0.5f)));
        }
        if (strcmp(text, s_end_last_field_text[slot]) != 0) {
            snprintf(s_end_last_field_text[slot], sizeof(s_end_last_field_text[slot]), "%s", text);
            lv_label_set_text(s_end_field_val[slot], text);
        }
        lv_obj_set_style_text_color(s_end_field_val[slot],
            system_field_value_color(channel, values[channel], available[channel]), LV_PART_MAIN);
    }
}

static lv_obj_t *touring_make_field(lv_obj_t *parent, int slot, lv_coord_t x, lv_coord_t y,
                                    lv_text_align_t align)
{
    lv_obj_t *field = lv_obj_create(parent);
    lv_obj_add_flag(field, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(field, x, y);
    lv_obj_set_size(field, 226, 118);
    lv_obj_set_style_bg_color(field, lv_color_hex(0x101214), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(0x303438), LV_PART_MAIN);
    lv_obj_set_style_radius(field, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(field, 14, LV_PART_MAIN);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(field, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(field, system_field_long_press_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)(SYSTEM_FIELD_TOURING * 16 + slot));

    lv_obj_t *accent = lv_obj_create(field);
    lv_obj_add_flag(accent, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(accent, LV_PCT(100), 2);
    lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(accent, lv_color_hex(0x6e7479), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(accent, 0, LV_PART_MAIN);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);

    s_tour_field_label[slot] = make_label(field, "", DASH_FONT_LABEL14, RALLY_TEXT_DIM);
    lv_obj_set_width(s_tour_field_label[slot], LV_PCT(100));
    lv_obj_set_style_text_align(s_tour_field_label[slot], align, LV_PART_MAIN);
    lv_obj_set_pos(s_tour_field_label[slot], 0, 5);
    s_tour_field_val[slot] = make_label(field, "--", DASH_FONT_HAL_VALUE, C_WHITE);
    lv_obj_set_width(s_tour_field_val[slot], LV_PCT(100));
    lv_obj_set_style_text_align(s_tour_field_val[slot], align, LV_PART_MAIN);
    lv_obj_set_pos(s_tour_field_val[slot], 0, 30);
    s_tour_field_unit[slot] = make_label(field, "", DASH_FONT_LABEL, lv_color_hex(0xb8bdc2));
    lv_obj_set_width(s_tour_field_unit[slot], LV_PCT(100));
    lv_obj_set_style_text_align(s_tour_field_unit[slot], align, LV_PART_MAIN);
    lv_obj_set_pos(s_tour_field_unit[slot], 0, 84);
    system_field_refresh_identity(SYSTEM_FIELD_TOURING, slot);
    return field;
}

static lv_obj_t *build_theme_touring(lv_obj_t *cluster)
{
    lv_obj_t *root = make_plain_container(cluster);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(0x0b0d0f), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = make_label(root, "RALLY STAGE", DASH_FONT_LABEL14, RALLY_TEXT_DIM);
    lv_obj_add_flag(title, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    for (int pair = 0; pair < TOUR_SEG_COUNT / 2; ++pair) {
        lv_coord_t y = 118 - pair * 7;
        lv_coord_t left_x = 62 + pair * 44;
        lv_coord_t right_x = 930 - pair * 44;
        for (int side = 0; side < 2; ++side) {
            int index = pair * 2 + side;
            lv_obj_t *light = lv_obj_create(root);
            s_tour_segments[index] = light;
            lv_obj_add_flag(light, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_pos(light, side == 0 ? left_x : right_x, y);
            lv_obj_set_size(light, 32, 32);
            lv_obj_set_style_bg_color(light, RALLY_LIGHT_OFF, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(light, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(light, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(light, lv_color_hex(0x42474b), LV_PART_MAIN);
            lv_obj_set_style_radius(light, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_shadow_width(light, 16, LV_PART_MAIN);
            lv_obj_set_style_shadow_spread(light, 1, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(light, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_clear_flag(light, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
    }

    lv_obj_t *rpm_caption = make_label(root, "ENGINE RPM", DASH_FONT_LABEL14, RALLY_TEXT_DIM);
    lv_obj_add_flag(rpm_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(rpm_caption, 440);
    lv_obj_set_style_text_align(rpm_caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(rpm_caption, 42, 180);
    s_tour_rpm_val = make_label(root, "0", DASH_FONT_SPEED, C_WHITE);
    lv_obj_add_flag(s_tour_rpm_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_tour_rpm_val, 440);
    lv_obj_set_style_text_align(s_tour_rpm_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_tour_rpm_val, 42, 224);

    lv_obj_t *divider = lv_obj_create(root);
    lv_obj_add_flag(divider, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(divider, 511, 182);
    lv_obj_set_size(divider, 2, 132);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x34383b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(divider, 0, LV_PART_MAIN);
    lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *speed_caption = make_label(root, "ROAD SPEED", DASH_FONT_LABEL14, RALLY_TEXT_DIM);
    lv_obj_add_flag(speed_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(speed_caption, 440);
    lv_obj_set_style_text_align(speed_caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(speed_caption, 542, 180);
    s_tour_speed_val = make_label(root, "0", DASH_FONT_SPEED, C_WHITE);
    lv_obj_add_flag(s_tour_speed_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_tour_speed_val, 440);
    lv_obj_set_style_text_align(s_tour_speed_val, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_tour_speed_val, 542, 224);
    s_tour_speed_unit = make_label(root, "MPH", DASH_FONT_LABEL14, lv_color_hex(0xb8bdc2));
    lv_obj_add_flag(s_tour_speed_unit, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(s_tour_speed_unit, 440);
    lv_obj_set_style_text_align(s_tour_speed_unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_tour_speed_unit, 542, 282);

    touring_make_field(root, 0, 30, 352, LV_TEXT_ALIGN_LEFT);
    touring_make_field(root, 1, 276, 352, LV_TEXT_ALIGN_LEFT);
    touring_make_field(root, 2, 522, 352, LV_TEXT_ALIGN_RIGHT);
    touring_make_field(root, 3, 768, 352, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t *odo_caption = make_label(root, "ODO", DASH_FONT_LABEL, lv_color_hex(0x81909c));
    lv_obj_add_flag(odo_caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(odo_caption, 30, 536);
    s_tour_odo_val = make_label(root, "0 MI", DASH_FONT_LABEL14, C_WHITE);
    lv_obj_add_flag(s_tour_odo_val, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(s_tour_odo_val, 74, 532);

    build_settings_button(root, true);
    lv_obj_t *controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_add_flag(controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(controls, DASH_CONTROL_ROW_WIDTH, 40);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_RIGHT, -24, -32);
    return root;
}

static void update_theme_touring(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    (void)limiter_hit;
    static int last_rpm = -1;
    static int last_speed = -1;
    static int last_metric = -1;
    static long last_odometer = -1;
    static int last_odometer_metric = -1;
    int metric = (dash_config_get_temperature_celsius() ? 1 : 0) |
                 (dash_config_get_pressure_kpa() ? 2 : 0) |
                 (dash_config_get_speed_kph() ? 4 : 0) |
                 (dash_config_get_distance_km() ? 8 : 0);
    int speed = (int)(data->speed_mph + 0.5f);
    if (rpm != last_rpm) {
        last_rpm = rpm;
        lv_label_set_text_fmt(s_tour_rpm_val, "%d", rpm);
    }
    if (speed != last_speed) {
        last_speed = speed;
        lv_label_set_text_fmt(s_tour_speed_val, "%d", speed);
    }
    if (metric != last_metric) {
        last_metric = metric;
        lv_label_set_text(s_tour_speed_unit, dash_config_get_speed_kph() ? "KPH" : "MPH");
        for (int slot = 0; slot < DASH_CONFIG_TOURING_FIELD_COUNT; ++slot) {
            system_field_refresh_identity(SYSTEM_FIELD_TOURING, slot);
        }
    }
    double distance = data->odo_miles;
    if (dash_config_get_distance_km()) distance *= 1.60934;
    long odometer = (long)(distance + 0.5);
    if (odometer != last_odometer || metric != last_odometer_metric) {
        last_odometer = odometer;
        last_odometer_metric = metric;
        char odometer_text[24];
        snprintf(odometer_text, sizeof(odometer_text), "%ld %s", odometer,
             dash_config_get_distance_km() ? "KM" : "MI");
        lv_label_set_text(s_tour_odo_val, odometer_text);
    }

    float values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg, fuel,
    };
    bool available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        true, true, true, true, true, true, true, data->oil_valid,
        data->duty_valid, data->knock_valid, true,
    };
    for (int slot = 0; slot < DASH_CONFIG_TOURING_FIELD_COUNT; ++slot) {
        int channel = dash_config_get_touring_field_channel(slot);
        if (channel < 0 || channel >= DASH_CONFIG_HAL_CHANNEL_COUNT) channel = 0;
        float displayed_value = system_field_display_value(channel, values[channel]);
        system_field_refresh_live_unit(SYSTEM_FIELD_TOURING, slot, channel, values[channel]);
        char text[16];
        if (!available[channel]) snprintf(text, sizeof(text), "N/A");
        else if (channel == TILE_AFR || channel == TILE_MAP || channel == TILE_BATT || channel == TILE_KNOCK) {
            snprintf(text, sizeof(text), "%.1f", displayed_value);
        } else {
            float value = displayed_value;
            snprintf(text, sizeof(text), "%d", (int)(value + (value >= 0 ? 0.5f : -0.5f)));
        }
        if (strcmp(text, s_tour_last_field_text[slot]) != 0) {
            snprintf(s_tour_last_field_text[slot], sizeof(s_tour_last_field_text[slot]), "%s", text);
            lv_label_set_text(s_tour_field_val[slot], text);
        }
        lv_obj_set_style_text_color(s_tour_field_val[slot],
            system_field_value_color(channel, values[channel], available[channel]), LV_PART_MAIN);
    }
}

lv_obj_t *honda_dash_ui_create(lv_obj_t *parent)
{
    initialize_i18n_fonts();

    /* pull the user's saved redline/VTEC settings in before any theme
       (which reads these same variables while building its tick marks
       and color zones) gets constructed */
    VTEC_RPM     = dash_config_get_vtec_rpm();
    REDLINE      = dash_config_get_redline_rpm();
    FULL_RED_RPM = REDLINE - 600;
    MAXRPM       = REDLINE + 600;

    lv_obj_t *cluster = lv_obj_create(parent);
    s_cluster = cluster;
    lv_obj_set_size(cluster, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(cluster, C_VOID, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cluster, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cluster, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cluster, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cluster, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cluster, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Modern theme (existing UI), wrapped so it can be hidden when
       another theme is selected ---- */
    s_theme_modern = make_plain_container(cluster);
    lv_obj_set_size(s_theme_modern, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_theme_modern, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_theme_modern, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    build_rpm_wrap(s_theme_modern);

    lv_obj_t *content = make_plain_container(s_theme_modern);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_right(content, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(content, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(content, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 8, LV_PART_MAIN);

    build_top_readout(content);
    build_tiles_grid(content);
    for (int slot = 0; slot < DASH_CONFIG_MODERN_FIELD_COUNT; ++slot) {
        system_field_refresh_identity(SYSTEM_FIELD_MODERN, slot);
    }

    lv_obj_t *telltale_strip = build_telltales(s_theme_modern);

    /* plain spacer -- lifts the telltale strip up off the true bottom edge
       so it clears the bezel. Must come AFTER the strip in the flow:
       `content` has flex_grow:1 and will absorb this spacer's height no
       matter where it sits, but only a spacer placed after the strip
       actually shifts the strip's own final position upward -- one placed
       before it just gets silently absorbed with no visible effect (which
       is exactly what happened last time). Deliberately not using style
       margin here since that API caused a build error in this project's
       LVGL setup; a spacer object needs nothing beyond basic
       lv_obj_create/set_size. */
    lv_obj_t *bottom_spacer = lv_obj_create(s_theme_modern);
    lv_obj_set_size(bottom_spacer, LV_PCT(100), 21);
    lv_obj_set_style_bg_opa(bottom_spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom_spacer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bottom_spacer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bottom_spacer, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Racepak-inspired cyan LCD, hidden until selected ---- */
    s_theme_race_lcd = build_theme_race_lcd(cluster);
    lv_obj_add_flag(s_theme_race_lcd, LV_OBJ_FLAG_HIDDEN);

    s_theme_haldash = build_theme_haldash(cluster);
    lv_obj_add_flag(s_theme_haldash, LV_OBJ_FLAG_HIDDEN);

    s_theme_endurance = build_theme_endurance(cluster);
    lv_obj_add_flag(s_theme_endurance, LV_OBJ_FLAG_HIDDEN);

    s_theme_touring = build_theme_touring(cluster);
    lv_obj_add_flag(s_theme_touring, LV_OBJ_FLAG_HIDDEN);

    build_critical_warning_banner(cluster);
    build_settings_button(telltale_strip, true);
    build_settings_overlay(cluster);

    /* restore whichever theme was active last time -- persist=false since
       we're loading what's already saved, not creating a new choice */
    activate_theme(theme_storage_load(), false);

    return cluster;
}

static uint32_t zone_color_for(tile_id_t id, float raw)
{
    if ((id == TILE_ECT || id == TILE_IAT) && dash_config_get_temperature_celsius()) {
        raw = raw * (9.0f / 5.0f) + 32.0f;
    } else if ((id == TILE_MAP || id == TILE_OIL) && dash_config_get_pressure_kpa()) {
        raw /= 6.89476f;
    }
    switch (id) {
        case TILE_ECT: {
            const float yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_ECT_YELLOW) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_ECT_HIGH) / 10.0f;
            return raw >= red ? 0xe4002b : (raw >= yellow ? 0xffb020 : (raw < 150.0f ? 0x4d8fff : 0x39ff8c));
        }
        case TILE_IAT: {
            const float yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_IAT_YELLOW) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_IAT_HIGH) / 10.0f;
            return raw >= red ? 0xe4002b : (raw >= yellow ? 0xffb020 : (raw < 60.0f ? 0x4d8fff : 0x39ff8c));
        }
        case TILE_AFR: {
            const float rich_yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_AFR_RICH_YELLOW) / 10.0f;
            const float rich_red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_AFR_RICH) / 10.0f;
            const float lean_yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_AFR_LEAN_YELLOW) / 10.0f;
            const float lean_red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_AFR_LEAN) / 10.0f;
            if (raw < rich_red || raw > lean_red) return 0xe4002b;
            if (raw < rich_yellow || raw > lean_yellow) return 0xffb020;
            return 0x39ff8c;
        }
        case TILE_MAP: {
            const float yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_MAP_YELLOW) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_MAP_HIGH) / 10.0f;
            return raw >= red ? 0xe4002b : (raw >= yellow ? 0xffb020 : 0x39ff8c);
        }
        case TILE_BATT: {
            const float yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_BATT_YELLOW) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_BATT_LOW) / 10.0f;
            return raw < red ? 0xe4002b : (raw < yellow ? 0xffb020 : 0x39ff8c);
        }
        case TILE_TPS: {
            const float yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_TPS_YELLOW) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_TPS_HIGH) / 10.0f;
            return raw >= red ? 0xe4002b : (raw >= yellow ? 0xffb020 : 0x39ff8c);
        }
        case TILE_OIL: {
            const float yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_OIL_YELLOW) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_OIL_LOW) / 10.0f;
            return raw < red ? 0xe4002b : (raw < yellow ? 0xffb020 : 0x39ff8c);
        }
        case TILE_DUTY: {
            const float yellow = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_DUTY_YELLOW) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_DUTY_HIGH) / 10.0f;
            return raw >= red ? 0xe4002b : (raw >= yellow ? 0xffb020 : 0x39ff8c);
        }
        case TILE_KNOCK: {
            const float amber = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_KNOCK_AMBER) / 10.0f;
            const float red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_KNOCK_RED) / 10.0f;
            return raw >= red ? 0xe4002b : (raw >= amber ? 0xffb020 : 0x4c4e54);
        }
        default: break;
    }

    const tile_def_t *def = &TILE_DEFS[id];
    uint32_t zone_hex = def->zones[0].color_hex;
    for (uint8_t i = 0; i < def->zone_count; i++) {
        if (raw >= def->zones[i].lo && raw <= def->zones[i].hi) {
            zone_hex = def->zones[i].color_hex;
            break;
        }
        if (i == def->zone_count - 1) zone_hex = def->zones[i].color_hex;
    }
    return zone_hex;
}

static lv_color_t system_field_value_color(int channel, float value, bool available)
{
    if (!available) return C_LABEL_DIM;
    if (channel < 0 || channel >= TILE_COUNT) return C_WHITE;
    uint32_t zone = zone_color_for((tile_id_t)channel, value);
    if (zone == 0xe4002b) return C_RED;
    if (zone == 0xffb020) return C_AMBER;
    return C_WHITE;
}

static void set_tile_value(int slot, int channel, float raw, bool available)
{
    const tile_def_t *def = channel < TILE_COUNT ? &TILE_DEFS[channel] : NULL;
    tile_widgets_t *w = &s_tiles[slot];
    float displayed_value = system_field_display_value(channel, raw);
    system_field_refresh_live_unit(SYSTEM_FIELD_MODERN, slot, channel, raw);

    char buf[24];
    if (!available) {
        snprintf(buf, sizeof(buf), "N/A");
    } else if (!def || def->dp == 0) {
        snprintf(buf, sizeof(buf), "%d", (int)(displayed_value +
                 (displayed_value >= 0 ? 0.5f : -0.5f)));
    } else {
        snprintf(buf, sizeof(buf), "%.*f", def->dp, displayed_value);
    }

    uint32_t zone_hex = available ? (def ? zone_color_for((tile_id_t)channel, raw) : 0x39ff8c) : 0;

    /* this is the fix for the low frame rate: skip the whole update
       (label re-align on 4 outline copies, bar redraw, color changes --
       all of it alpha-blended, all of it expensive) when neither the
       displayed number nor the status zone actually changed since last
       time. Previously this ran unconditionally on every single call,
       even when a sensor's value was rock steady. */
    if (strcmp(buf, w->last_text) == 0 && zone_hex == w->last_zone_hex) {
        return;
    }
    snprintf(w->last_text, sizeof(w->last_text), "%s", buf);
    w->last_zone_hex = zone_hex;

    lv_label_set_text(w->value, buf);
    {
        const int off = 1;
        const int dx[4] = { -off, off, 0, 0 };
        const int dy[4] = { 0, 0, -off, off };
        for (int i = 0; i < 4; i++) {
            if (!w->value_outline[i]) continue;
            lv_label_set_text(w->value_outline[i], buf);
            lv_obj_align_to(w->value_outline[i], w->value, LV_ALIGN_CENTER, dx[i], dy[i]);
        }
    }

    if (!available) {
        lv_bar_set_value(w->bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_text_color(w->value, C_LABEL_DIM, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(w->bar, LV_OPA_TRANSP, LV_PART_INDICATOR);
        return;
    }

    /* fill width tracks the live value's position within min/max --
       zone color/opacity signals status, width signals where in the
       range the value actually sits */
    float minimum = def ? def->min : 0.0f;
    float maximum = def ? def->max : 100.0f;
    float pct = (raw - minimum) / (maximum - minimum);
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
    lv_bar_set_value(w->bar, (int32_t)(pct * 1000), LV_ANIM_OFF);

    lv_color_t bar_color = lv_color_hex(zone_hex);
    lv_opa_t bar_opa = LV_OPA_20;
    if (zone_hex == 0xe4002b) bar_opa = LV_OPA_40;         /* red zone -- bad */
    else if (zone_hex == 0xffb020) bar_opa = LV_OPA_30;    /* amber zone -- caution (knock only) */
    else if (zone_hex == 0x39ff8c) bar_opa = LV_OPA_40;    /* bright green needs more opacity to read true over the dark panel */
    else if (zone_hex == 0x4d8fff) bar_opa = LV_OPA_30;    /* blue zone -- cold */

    /* value text stays plain white regardless of zone -- the fill communicates status */
    lv_obj_set_style_text_color(w->value, C_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w->bar, bar_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(w->bar, bar_opa, LV_PART_INDICATOR);
}

static void apply_value_smoothing(honda_dash_data_t *data)
{
    enum { SMOOTHING_MAX_WINDOW = 10 };
    typedef struct {
        uint16_t rpm;
        float speed_mph;
        float tps_pct;
        float map_psi;
    } smoothing_sample_t;
    static smoothing_sample_t samples[SMOOTHING_MAX_WINDOW];
    static size_t next_sample = 0;
    static size_t sample_count = 0;
    static dash_config_smoothing_strength_t last_strength = DASH_CONFIG_SMOOTHING_OFF;
    static const uint8_t windows[DASH_CONFIG_SMOOTHING_COUNT] = {0, 3, 6, 10};
    dash_config_smoothing_strength_t strength = dash_config_get_smoothing_strength();

    if (strength <= DASH_CONFIG_SMOOTHING_OFF || strength >= DASH_CONFIG_SMOOTHING_COUNT) {
        next_sample = 0;
        sample_count = 0;
        last_strength = DASH_CONFIG_SMOOTHING_OFF;
        return;
    }
    size_t window = windows[strength];
    if (strength != last_strength) {
        next_sample = 0;
        sample_count = 0;
        last_strength = strength;
    }

    if (sample_count > 0) {
        const smoothing_sample_t *previous = &samples[(next_sample + window - 1) % window];
        if (abs((int)data->rpm - (int)previous->rpm) >= 100 ||
                fabsf(data->tps_pct - previous->tps_pct) >= 8.0f ||
                fabsf(data->map_psi - previous->map_psi) >= 3.0f) {
            next_sample = 0;
            sample_count = 0;
        }
    }

    samples[next_sample] = (smoothing_sample_t){
        .rpm = data->rpm,
        .speed_mph = data->speed_mph,
        .tps_pct = data->tps_pct,
        .map_psi = data->map_psi,
    };
    next_sample = (next_sample + 1) % window;
    if (sample_count < window) sample_count++;

    float rpm = 0.0f;
    float speed = 0.0f;
    for (size_t i = 0; i < sample_count; ++i) {
        rpm += samples[i].rpm;
        speed += samples[i].speed_mph;
    }

    float divisor = (float)sample_count;
    data->rpm = (uint16_t)(lroundf((rpm / divisor) / 10.0f) * 10);
    data->speed_mph = speed / divisor;
}

void honda_dash_ui_update(const honda_dash_data_t *data)
{
    if (!data) return;

    if (s_active_theme < 100) s_shift_light_last_theme = -1;

    s_warning_data = *data;
    s_warning_have_data = true;
    record_buttons_refresh();

    /* apply the user's unit preference once here, rather than touching
       every theme's individual rendering code -- all five themes read
       from this same converted copy */
    honda_dash_data_t display_data = *data;
    if (dash_config_get_speed_kph()) {
        display_data.speed_mph = display_data.speed_mph * 1.60934f;
    }
    if (dash_config_get_temperature_celsius()) {
        display_data.ect_f     = (display_data.ect_f - 32.0f) * (5.0f / 9.0f);
        display_data.iat_f     = (display_data.iat_f - 32.0f) * (5.0f / 9.0f);
    }
    if (dash_config_get_pressure_kpa()) {
        display_data.map_psi *= 6.89476f;
        display_data.oil_psi *= 6.89476f;
    }
    if (s_odo_display_mode == 1) {
        display_data.odo_miles = odometer_get_trip_a_miles();
    } else if (s_odo_display_mode == 2) {
        display_data.odo_miles = odometer_get_trip_b_miles();
    }
    apply_value_smoothing(&display_data);
    data = &display_data;

    s_last_data = *data;
    s_have_last_data = true;

    int rpm = data->rpm;
    if (rpm < 0) rpm = 0;
    if (rpm > MAXRPM) rpm = MAXRPM;
    bool vtec_on = rpm >= VTEC_RPM;
    bool limiter_hit = rpm >= FULL_RED_RPM;
    float fuel = data->fuel_pct;
    if (fuel < 0) fuel = 0;
    if (fuel > 100) fuel = 100;

    /* only the currently-visible theme's widgets get touched */
    if (s_active_theme >= 100) runtime_theme_update(data);
    else if (s_active_theme == THEME_ID_RACE_LCD) update_theme_race_lcd(data, rpm, limiter_hit, fuel);
    else if (s_active_theme == THEME_ID_HALDASH) update_theme_haldash(data, rpm, limiter_hit, fuel);
    else if (s_active_theme == THEME_ID_ENDURANCE) update_theme_endurance(data, rpm, limiter_hit, fuel);
    else if (s_active_theme == THEME_ID_TOURING) update_theme_touring(data, rpm, limiter_hit, fuel);
    else update_theme_modern(data, rpm, limiter_hit, vtec_on, fuel);
    if (s_theme_stability_started_us != 0 &&
            esp_timer_get_time() - s_theme_stability_started_us >= 5000000) {
        s_theme_stability_started_us = 0;
        theme_recovery_set_pending(false);
        ESP_LOGI("THEME", "Theme startup confirmed stable");
    }
}

static void update_theme_modern(const honda_dash_data_t *data, int rpm, bool limiter_hit, bool vtec_on, float fuel)
{
    static int last_metric = -1;
    int metric = (dash_config_get_temperature_celsius() ? 1 : 0) |
                 (dash_config_get_pressure_kpa() ? 2 : 0) |
                 (dash_config_get_speed_kph() ? 4 : 0);
    if (metric != last_metric) {
        last_metric = metric;
        lv_label_set_text(s_speed_unit_label, dash_config_get_speed_kph() ? "KPH" : "MPH");
        for (int slot = 0; slot < DASH_CONFIG_MODERN_FIELD_COUNT; ++slot) {
            system_field_refresh_identity(SYSTEM_FIELD_MODERN, slot);
        }
    }
    /* everything in this block (label, tags, mini-bar) is a pure function
       of rpm alone, so one change check covers all of it */
    static int s_last_rpm = -1;
    if (rpm != s_last_rpm) {
        s_last_rpm = rpm;

        char rpm_buf[8];
        snprintf(rpm_buf, sizeof(rpm_buf), "%d", rpm);
        lv_label_set_text(s_rpm_val_label, rpm_buf);
        lv_obj_set_style_text_color(s_rpm_val_label, rpm >= 7000 ? C_RED : C_WHITE, LV_PART_MAIN);

        bool shift_on = dash_config_get_shift_light_enabled() &&
                rpm >= dash_config_get_shift_stage_rpm(2);

        lv_obj_set_style_bg_color(s_tag_vtec, vtec_on ? C_GREEN_DEEP : C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_tag_vtec, vtec_on ? C_GREEN : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_tag_vtec_label, vtec_on ? lv_color_hex(0xeafff3) : C_LABEL_DIM, LV_PART_MAIN);

        lv_obj_set_style_bg_color(s_tag_shift, shift_on ? C_RED : C_PANEL, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_tag_shift, shift_on ? C_RED : C_LINE, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_tag_shift_label, shift_on ? C_WHITE : C_LABEL_DIM, LV_PART_MAIN);

        /* mini gradient bar under the RPM number, mirrors the main bar's color */
        int32_t mini_val = (int32_t)(((float)rpm / (float)MAXRPM) * 1000.0f);
        if (mini_val > 1000) mini_val = 1000;
        lv_bar_set_value(s_rpm_mini_fill, mini_val, LV_ANIM_OFF);
        lv_color_t mini_c = (rpm >= FULL_RED_RPM) ? C_RED : seg_color_for_rpm(rpm);
        lv_obj_set_style_bg_color(s_rpm_mini_fill, mini_c, LV_PART_INDICATOR);
    }

    /* Segment appearance changes only when the visible bucket or limiter
       state changes, not for every raw RPM update within the same bucket. */
    int active_count = (rpm * SEG_COUNT) / MAXRPM;
    if (active_count != s_last_seg_bucket || limiter_hit != s_last_seg_limiter) {
        s_last_seg_bucket = active_count;
        s_last_seg_limiter = limiter_hit;
        for (int i = 0; i < SEG_COUNT; i++) {
            int seg_rpm = ((i + 1) * MAXRPM) / SEG_COUNT;
            bool active = i < active_count;
            lv_color_t c = !active ? C_SEG_OFF : (limiter_hit ? C_RED : seg_color_for_rpm(seg_rpm));
            lv_obj_set_style_bg_color(s_segs[i], c, LV_PART_MAIN);
        }
    }

    /* ---- speed ---- */
    static int s_last_speed = -1;
    int speed_i = (int)(data->speed_mph + 0.5f);
    if (speed_i != s_last_speed) {
        s_last_speed = speed_i;
        char speed_buf[8];
        snprintf(speed_buf, sizeof(speed_buf), "%d", speed_i);
        lv_label_set_text(s_speed_val_label, speed_buf);
    }

    /* ---- gear ---- */
    static int s_last_gear = -99;
    if (data->gear != s_last_gear) {
        s_last_gear = data->gear;
        if (data->gear <= 0) {
            lv_label_set_text(s_gear_num_label, "N");
        } else {
            char gear_buf[4];
            snprintf(gear_buf, sizeof(gear_buf), "%d", data->gear);
            lv_label_set_text(s_gear_num_label, gear_buf);
        }
    }

    /* ---- customizable data tiles (each does its own change detection) ---- */
    float channel_values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg, fuel,
    };
    bool channel_available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        true, true, true, true, true, true, true, data->oil_valid,
        data->duty_valid, data->knock_valid, true,
    };
    for (int slot = 0; slot < DASH_CONFIG_MODERN_FIELD_COUNT; ++slot) {
        int channel = dash_config_get_modern_field_channel(slot);
        set_tile_value(slot, channel, channel_values[channel], channel_available[channel]);
    }

    /* ---- telltales ---- */
    static int s_last_tell_mask = -1;
    bool cel_lit      = data->cel;
    int tell_mask = cel_lit ? 1 : 0;
    if (tell_mask != s_last_tell_mask) {
        s_last_tell_mask = tell_mask;
        lv_obj_set_style_line_color(s_tell_cel.dot,
                                    cel_lit ? s_tell_cel.on_color : C_LABEL_DIM,
                                    LV_PART_MAIN);
    }

    /* ---- odometer ---- */
    static long s_last_odo = -1;
    long odo_i = (long)(data->odo_miles + 0.5);
    if (odo_i != s_last_odo) {
        s_last_odo = odo_i;
        char odo_buf[20];
        snprintf(odo_buf, sizeof(odo_buf), "%ld MI", odo_i);
        lv_label_set_text(s_odo_val_label, odo_buf);
    }

    /* ---- fuel level ---- */
    static int s_last_fuel = -1;
    int fuel_i = (int)(fuel + 0.5f);
    if (fuel_i != s_last_fuel) {
        s_last_fuel = fuel_i;
        lv_bar_set_value(s_fuel_bar, fuel_i, LV_ANIM_OFF);
        lv_color_t fuel_color = (fuel <= 15) ? C_RED : (fuel <= 30) ? C_AMBER : lv_color_hex(0x2dd4bf);
        lv_obj_set_style_bg_color(s_fuel_bar, fuel_color, LV_PART_INDICATOR);
        char fuel_buf[8];
        snprintf(fuel_buf, sizeof(fuel_buf), "%d", fuel_i);
        lv_label_set_text(s_fuel_val_label, fuel_buf);
    }
}

#if 0
static void update_theme_track(const honda_dash_data_t *data, int rpm, bool limiter_hit, bool vtec_on, float fuel)
{
    char buf[16];

    static int s_trk_last_rpm = -1;
    if (rpm != s_trk_last_rpm) {
        s_trk_last_rpm = rpm;
        snprintf(buf, sizeof(buf), "%d", rpm);
        lv_label_set_text(s_trk_rpm_val, buf);
        lv_obj_set_style_text_color(s_trk_rpm_val, rpm >= 7000 ? C_RED : C_WHITE, LV_PART_MAIN);

        /* segments: white normally, red past the limiter -- reuses the same
           dome-taper heights baked in at build time, only color changes here */
        int active_count = (rpm * SEG_COUNT) / MAXRPM;
        for (int i = 0; i < SEG_COUNT; i++) {
            bool active = i < active_count;
            lv_color_t c = !active ? lv_color_hex(0x1c1c1c) : (limiter_hit ? C_RED : C_WHITE);
            lv_obj_set_style_bg_color(s_trk_segs[i], c, LV_PART_MAIN);
        }
    }

    static int s_trk_last_speed = -1;
    int speed_i = (int)(data->speed_mph + 0.5f);
    if (speed_i != s_trk_last_speed) {
        s_trk_last_speed = speed_i;
        snprintf(buf, sizeof(buf), "%d", speed_i);
        lv_label_set_text(s_trk_speed_val, buf);
    }

    static int s_trk_last_gear = -99;
    if (data->gear != s_trk_last_gear) {
        s_trk_last_gear = data->gear;
        if (data->gear <= 0) {
            lv_label_set_text(s_trk_gear_val, "N");
        } else {
            snprintf(buf, sizeof(buf), "%d", data->gear);
            lv_label_set_text(s_trk_gear_val, buf);
        }
    }

    /* tiles: plain white, red only if the channel is in its critical zone */
    float vals[TILE_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg
    };
    static char s_trk_last_tile_text[TILE_COUNT][24];
    static uint32_t s_trk_last_tile_zone[TILE_COUNT];
    for (int id = 0; id < TILE_COUNT; id++) {
        const tile_def_t *def = &TILE_DEFS[id];
        float v = vals[id];
        char tbuf[24];
        bool available = tile_value_is_available(data, (tile_id_t)id);
        if (!available) snprintf(tbuf, sizeof(tbuf), "N/A");
        else if (def->dp == 0) snprintf(tbuf, sizeof(tbuf), "%d", (int)(v + (v >= 0 ? 0.5f : -0.5f)));
        else snprintf(tbuf, sizeof(tbuf), "%.*f", def->dp, v);
        uint32_t zone_hex = available ? zone_color_for((tile_id_t)id, v) : 0;
        if (strcmp(tbuf, s_trk_last_tile_text[id]) == 0 && zone_hex == s_trk_last_tile_zone[id]) continue;
        snprintf(s_trk_last_tile_text[id], sizeof(s_trk_last_tile_text[id]), "%s", tbuf);
        s_trk_last_tile_zone[id] = zone_hex;
        lv_label_set_text(s_trk_tile_val[id], tbuf);
        bool crit = (zone_hex == 0xe4002b);
        lv_obj_set_style_text_color(s_trk_tile_val[id], crit ? C_RED : C_WHITE, LV_PART_MAIN);
    }

    static int s_trk_last_tell_mask = -1;
    bool knock_lit   = data->knock_valid && data->knock_deg > 1.5f;
    bool oil_lit     = data->oil_valid && data->oil_psi < 15.0f;
    bool coolant_lit = data->ect_f > 225.0f;
    bool cel_lit     = data->cel;
    int tell_mask = (knock_lit<<0) | (oil_lit<<1) | (coolant_lit<<2) | (cel_lit<<3) | (vtec_on<<4);
    if (tell_mask != s_trk_last_tell_mask) {
        s_trk_last_tell_mask = tell_mask;
        lv_obj_set_style_bg_color(s_trk_tell_knock.dot,   knock_lit   ? s_trk_tell_knock.on_color   : lv_color_hex(0x3a3a3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_trk_tell_oil.dot,     oil_lit     ? s_trk_tell_oil.on_color     : lv_color_hex(0x3a3a3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_trk_tell_coolant.dot, coolant_lit ? s_trk_tell_coolant.on_color : lv_color_hex(0x3a3a3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_trk_tell_cel.dot,     cel_lit     ? s_trk_tell_cel.on_color     : lv_color_hex(0x3a3a3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_trk_tell_vtec.dot,    vtec_on     ? s_trk_tell_vtec.on_color    : lv_color_hex(0x3a3a3a), LV_PART_MAIN);
    }

    static long s_trk_last_odo = -1;
    long odo_i = (long)(data->odo_miles + 0.5);
    if (odo_i != s_trk_last_odo) {
        s_trk_last_odo = odo_i;
        snprintf(buf, sizeof(buf), "%ld MI", odo_i);
        lv_label_set_text(s_trk_odo_val, buf);
    }

    static int s_trk_last_fuel = -1;
    int fuel_i = (int)(fuel + 0.5f);
    if (fuel_i != s_trk_last_fuel) {
        s_trk_last_fuel = fuel_i;
        lv_bar_set_value(s_trk_fuel_bar, fuel_i, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d", fuel_i);
        lv_label_set_text(s_trk_fuel_val, buf);
    }
}

static void update_theme_retro(const honda_dash_data_t *data, int rpm, bool limiter_hit, bool vtec_on, float fuel)
{
    const lv_color_t GREEN = lv_color_hex(0x39ff8c);
    const lv_color_t GOFF  = lv_color_hex(0x0a2412);
    char buf[16];

    static int s_ret_last_rpm = -1;
    if (rpm != s_ret_last_rpm) {
        s_ret_last_rpm = rpm;
        snprintf(buf, sizeof(buf), "%d", rpm);
        lv_label_set_text(s_ret_rpm_val, buf);
        lv_obj_set_style_text_color(s_ret_rpm_val, rpm >= 7000 ? C_RED : GREEN, LV_PART_MAIN);

        int active_count = (rpm * SEG_COUNT) / MAXRPM;
        for (int i = 0; i < SEG_COUNT; i++) {
            bool active = i < active_count;
            lv_color_t c = !active ? GOFF : (limiter_hit ? C_RED : GREEN);
            lv_obj_set_style_bg_color(s_ret_segs[i], c, LV_PART_MAIN);
        }
    }

    static int s_ret_last_speed = -1;
    int speed_i = (int)(data->speed_mph + 0.5f);
    if (speed_i != s_ret_last_speed) {
        s_ret_last_speed = speed_i;
        snprintf(buf, sizeof(buf), "%d", speed_i);
        lv_label_set_text(s_ret_speed_val, buf);
    }

    static int s_ret_last_gear = -99;
    if (data->gear != s_ret_last_gear) {
        s_ret_last_gear = data->gear;
        if (data->gear <= 0) {
            lv_label_set_text(s_ret_gear_val, "N");
        } else {
            snprintf(buf, sizeof(buf), "%d", data->gear);
            lv_label_set_text(s_ret_gear_val, buf);
        }
    }

    float vals[TILE_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg
    };
    static char s_ret_last_tile_text[TILE_COUNT][24];
    static uint32_t s_ret_last_tile_zone[TILE_COUNT];
    for (int id = 0; id < TILE_COUNT; id++) {
        const tile_def_t *def = &TILE_DEFS[id];
        float v = vals[id];
        char tbuf[24];
        bool available = tile_value_is_available(data, (tile_id_t)id);
        if (!available) snprintf(tbuf, sizeof(tbuf), "N/A");
        else if (def->dp == 0) snprintf(tbuf, sizeof(tbuf), "%d", (int)(v + (v >= 0 ? 0.5f : -0.5f)));
        else snprintf(tbuf, sizeof(tbuf), "%.*f", def->dp, v);
        uint32_t zone_hex = available ? zone_color_for((tile_id_t)id, v) : 0;
        if (strcmp(tbuf, s_ret_last_tile_text[id]) == 0 && zone_hex == s_ret_last_tile_zone[id]) continue;
        snprintf(s_ret_last_tile_text[id], sizeof(s_ret_last_tile_text[id]), "%s", tbuf);
        s_ret_last_tile_zone[id] = zone_hex;
        lv_label_set_text(s_ret_tile_val[id], tbuf);
        bool crit = (zone_hex == 0xe4002b);
        lv_obj_set_style_text_color(s_ret_tile_val[id], crit ? C_RED : GREEN, LV_PART_MAIN);
    }

    static int s_ret_last_tell_mask = -1;
    bool knock_lit   = data->knock_valid && data->knock_deg > 1.5f;
    bool oil_lit     = data->oil_valid && data->oil_psi < 15.0f;
    bool coolant_lit = data->ect_f > 225.0f;
    bool cel_lit     = data->cel;
    int tell_mask = (knock_lit<<0) | (oil_lit<<1) | (coolant_lit<<2) | (cel_lit<<3) | (vtec_on<<4);
    if (tell_mask != s_ret_last_tell_mask) {
        s_ret_last_tell_mask = tell_mask;
        lv_obj_set_style_bg_color(s_ret_tell_knock.dot,   knock_lit   ? s_ret_tell_knock.on_color   : lv_color_hex(0x1f6b3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ret_tell_oil.dot,     oil_lit     ? s_ret_tell_oil.on_color     : lv_color_hex(0x1f6b3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ret_tell_coolant.dot, coolant_lit ? s_ret_tell_coolant.on_color : lv_color_hex(0x1f6b3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ret_tell_cel.dot,     cel_lit     ? s_ret_tell_cel.on_color     : lv_color_hex(0x1f6b3a), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ret_tell_vtec.dot,    vtec_on     ? s_ret_tell_vtec.on_color    : lv_color_hex(0x1f6b3a), LV_PART_MAIN);
    }

    static long s_ret_last_odo = -1;
    long odo_i = (long)(data->odo_miles + 0.5);
    if (odo_i != s_ret_last_odo) {
        s_ret_last_odo = odo_i;
        snprintf(buf, sizeof(buf), "%ld MI", odo_i);
        lv_label_set_text(s_ret_odo_val, buf);
    }

    static int s_ret_last_fuel = -1;
    int fuel_i = (int)(fuel + 0.5f);
    if (fuel_i != s_ret_last_fuel) {
        s_ret_last_fuel = fuel_i;
        lv_bar_set_value(s_ret_fuel_bar, fuel_i, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d", fuel_i);
        lv_label_set_text(s_ret_fuel_val, buf);
    }
}

static void update_theme_minimal(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    (void)fuel; /* no longer shown in this stripped-down layout */

    static int s_min_last_speed = -1;
    int speed_i = (int)(data->speed_mph + 0.5f);
    if (speed_i != s_min_last_speed) {
        s_min_last_speed = speed_i;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", speed_i);
        lv_label_set_text(s_min_speed_val, buf);
    }

    static int s_min_last_rpm = -1;
    if (rpm != s_min_last_rpm) {
        s_min_last_rpm = rpm;

        char buf[8];
        snprintf(buf, sizeof(buf), "%d", rpm);
        lv_label_set_text(s_min_sub_label, buf);

        /* same dome-taper segments as build time, only color changes here --
           white normally, red past the limiter, matching every other theme */
        int active_count = (rpm * SEG_COUNT) / MAXRPM;
        for (int i = 0; i < SEG_COUNT; i++) {
            bool active = i < active_count;
            lv_color_t c = !active ? C_SEG_OFF : (limiter_hit ? C_RED : seg_color_for_rpm((i * MAXRPM) / SEG_COUNT));
            lv_obj_set_style_bg_color(s_min_segs[i], c, LV_PART_MAIN);
        }
    }
}

static void update_theme_classic(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    (void)limiter_hit;
    update_si_cluster_theme(data, rpm, fuel, &s_si_yellow_state);
    return;

    {
        (void)limiter_hit;

        static int last_rpm = -1;
        if (rpm != last_rpm) {
            last_rpm = rpm;
            float value = rpm / 10000.0f;
            if (value < 0) value = 0;
            if (value > 1) value = 1;
            lv_obj_set_style_transform_angle(s_cla_rpm_needle, (int16_t)((-130.0f + value * 260.0f) * 10.0f), LV_PART_MAIN);
        }

        static int last_speed = -1;
        int speed_kph = (int)(data->speed_mph * 1.60934f + 0.5f);
        if (speed_kph != last_speed) {
            last_speed = speed_kph;
            float value = speed_kph / 180.0f;
            if (value < 0) value = 0;
            if (value > 1) value = 1;
            lv_obj_set_style_transform_angle(s_cla_speed_needle, (int16_t)((-130.0f + value * 260.0f) * 10.0f), LV_PART_MAIN);
        }

        static int last_fuel = -1;
        int fuel_i = (int)(fuel + 0.5f);
        if (fuel_i != last_fuel) {
            last_fuel = fuel_i;
            float value = fuel / 100.0f;
            if (value < 0) value = 0;
            if (value > 1) value = 1;
            lv_obj_set_style_transform_angle(s_ek9_fuel_needle, (int16_t)((-168.0f + value * 156.0f) * 10.0f), LV_PART_MAIN);
        }

        static int last_ect = -1;
        int ect_i = (int)(data->ect_f + 0.5f);
        if (ect_i != last_ect) {
            last_ect = ect_i;
            float value = (data->ect_f - 150.0f) / 90.0f;
            if (value < 0) value = 0;
            if (value > 1) value = 1;
            lv_obj_set_style_transform_angle(s_ek9_temp_needle, (int16_t)((168.0f - value * 156.0f) * 10.0f), LV_PART_MAIN);
        }

        static long last_odo_tenths = -1;
        long odo_tenths = (long)(data->odo_miles * 1.60934 * 10.0 + 0.5);
        if (odo_tenths != last_odo_tenths) {
            last_odo_tenths = odo_tenths;
            char digits[7];
            snprintf(digits, sizeof(digits), "%06ld", (odo_tenths / 10) % 1000000);
            for (int i = 0; i < 6; i++) {
                char digit[2] = {digits[i], '\0'};
                lv_label_set_text(s_cla_odo_digits[i], digit);
            }
        }

        static long last_trip_tenths = -1;
        long trip_tenths = (long)(odometer_get_trip_a_miles() * 1.60934 * 10.0 + 0.5);
        if (trip_tenths != last_trip_tenths) {
            last_trip_tenths = trip_tenths;
            char digits[5];
            snprintf(digits, sizeof(digits), "%04ld", trip_tenths % 10000);
            for (int i = 0; i < 4; i++) {
                char digit[2] = {digits[i], '\0'};
                lv_label_set_text(s_cla_trip_digits[i], digit);
            }
        }

        return;
    }

    (void)limiter_hit; /* the live indicator arc is suppressed for authenticity, so its color no longer matters */

    static int s_cla_last_rpm = -1;
    if (rpm != s_cla_last_rpm) {
        s_cla_last_rpm = rpm;
        float rpm_frac = (float)rpm / 10000.0f;
        if (rpm_frac > 1.0f) rpm_frac = 1.0f;
        lv_arc_set_value(s_cla_rpm_arc, rpm);
        cla_set_needle_angle(s_cla_rpm_needle, rpm_frac);
    }

    /* speedo face is printed 0-180 km/h (matches the real JDM cluster this
       theme replicates), so the needle is driven off a km/h conversion
       here regardless of the user's overall MPH/KPH display setting --
       the numbers baked into this specific gauge face don't change */
    static int s_cla_last_speed = -1;
    int spd_i = (int)(data->speed_mph + 0.5f);
    if (spd_i != s_cla_last_speed) {
        s_cla_last_speed = spd_i;
        float spd_kph = data->speed_mph * 1.60934f;
        if (spd_kph < 0) spd_kph = 0;
        if (spd_kph > 180) spd_kph = 180;
        lv_arc_set_value(s_cla_speed_arc, (int)(spd_kph + 0.5f));
        cla_set_needle_angle(s_cla_speed_needle, spd_kph / 180.0f);
    }

    /* fuel/temp sub-needles on the combo pod */
    static int s_ek9_last_fuel = -1;
    int fuel_i = (int)(fuel + 0.5f);
    if (fuel_i != s_ek9_last_fuel) {
        s_ek9_last_fuel = fuel_i;
        float f = fuel; if (f < 0) f = 0; if (f > 100) f = 100;
        float fuel_ang = -12.0f - (1.0f - f / 100.0f) * 156.0f;
        lv_obj_set_style_transform_angle(s_ek9_fuel_needle, (int16_t)(fuel_ang * 10.0f), LV_PART_MAIN);
    }
    static int s_ek9_last_ect = -1;
    int ect_i = (int)(data->ect_f + 0.5f);
    if (ect_i != s_ek9_last_ect) {
        s_ek9_last_ect = ect_i;
        float tf = (data->ect_f - 150.0f) / 90.0f;
        if (tf < 0) tf = 0;
        if (tf > 1) tf = 1;
        float temp_ang = 12.0f + (1.0f - tf) * 156.0f;
        lv_obj_set_style_transform_angle(s_ek9_temp_needle, (int16_t)(temp_ang * 10.0f), LV_PART_MAIN);
    }

    /* odometer window embedded in the speedo face */
    static long s_ek9_last_odo = -1;
    long odo_i = (long)(data->odo_miles + 0.5);
    if (odo_i != s_ek9_last_odo) {
        s_ek9_last_odo = odo_i;
        char odo_buf[8];
        snprintf(odo_buf, sizeof(odo_buf), "%06ld", odo_i % 1000000);
        lv_label_set_text(s_cla_odo_val, odo_buf);
    }

    char oil_buf[16];
    char duty_buf[16];
    char knock_buf[16];
    if (data->oil_valid) snprintf(oil_buf, sizeof(oil_buf), "%d", (int)(data->oil_psi + 0.5f));
    else snprintf(oil_buf, sizeof(oil_buf), "N/A");
    if (data->duty_valid) snprintf(duty_buf, sizeof(duty_buf), "%d%%", (int)(data->duty_pct + 0.5f));
    else snprintf(duty_buf, sizeof(duty_buf), "N/A");
    if (data->knock_valid) snprintf(knock_buf, sizeof(knock_buf), "%.1f\xC2\xB0", data->knock_deg);
    else snprintf(knock_buf, sizeof(knock_buf), "N/A");

    char footer_buf[256];
    snprintf(footer_buf, sizeof(footer_buf),
        "ODO %ld  \xC2\xB7  FUEL %d%%  \xC2\xB7  CLNT %d\xC2\xB0" "F  \xC2\xB7  IAT %d\xC2\xB0" "F  \xC2\xB7  AFR %.1f  \xC2\xB7  "
        "TMG %d\xC2\xB0  \xC2\xB7  MAP %.1f  \xC2\xB7  BATT %.1fV  \xC2\xB7  TPS %d%%  \xC2\xB7  OIL %s  \xC2\xB7  INJ %s  \xC2\xB7  KNK %s",
        (long)(data->odo_miles + 0.5), (int)(fuel + 0.5f),
        (int)(data->ect_f + 0.5f), (int)(data->iat_f + 0.5f), data->afr,
        (int)(data->timing_deg + 0.5f), data->map_psi, data->batt_v,
        (int)(data->tps_pct + 0.5f), oil_buf, duty_buf, knock_buf);

    static char s_cla_last_footer[256];
    if (strcmp(footer_buf, s_cla_last_footer) != 0) {
        strncpy(s_cla_last_footer, footer_buf, sizeof(s_cla_last_footer) - 1);
        s_cla_last_footer[sizeof(s_cla_last_footer) - 1] = '\0';
        lv_label_set_text(s_cla_footer_label, footer_buf);
    }
}
#endif

