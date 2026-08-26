/*
 * honda_dash_ui.h
 * ------------------------------------------------------------------
 * LVGL v8.3 port of the Honda 92-00 digital cluster UI.
 * Target: 1024x600 panel (ESP32-P4), driven by live CANbus/Hondata data.
 *
 * USAGE
 * -----
 *   #include "honda_dash_ui.h"
 *
 *   // once, after your display + LVGL init (e.g. after lv_disp_drv_register):
 *   lv_obj_t *root = honda_dash_ui_create(lv_scr_act());
 *
 *   // every time you have fresh CAN data (throttle to ~20-30Hz, see note
 *   // in honda_dash_ui_update()):
 *   honda_dash_data_t d = {0};
 *   d.rpm        = hondata_get_rpm();
 *   d.speed_mph  = hondata_get_speed();
 *   d.gear       = hondata_get_gear();      // 0 = Neutral
 *   d.ect_f      = hondata_get_ect_f();
 *   d.iat_f      = hondata_get_iat_f();
 *   d.afr        = hondata_get_afr();
 *   d.timing_deg = hondata_get_timing();
 *   d.map_psi    = hondata_get_map_psi();
 *   d.batt_v     = hondata_get_battery();
 *   d.tps_pct    = hondata_get_tps();
 *   d.oil_psi    = hondata_get_oil_psi();
 *   d.duty_pct   = hondata_get_inj_duty();
 *   d.knock_deg  = hondata_get_knock_retard();
 *   d.cel        = hondata_get_cel_flag();
 *   d.odo_miles  = odometer_get_miles();
 *   d.fuel_pct   = hondata_get_fuel_level();
 *   honda_dash_ui_update(&d);
 *
 * REQUIRED lv_conf.h SETTINGS
 * ----------------------------
 *   #define LV_USE_FLEX   1
 *   #define LV_USE_GRID   1
 *   #define LV_FONT_MONTSERRAT_12  1
 *   #define LV_FONT_MONTSERRAT_14  1
 *   #define LV_FONT_MONTSERRAT_28  1
 *   #define LV_FONT_MONTSERRAT_48  1
 *
 * FONT NOTES
 * ----------
 * The web mockup used Barlow Condensed at very large sizes (speed number
 * ~118px, RPM readout ~58px, gear ~56px). LVGL's built-in Montserrat tops
 * out at 48px, so this file uses lv_font_montserrat_48 for all "big number"
 * text out of the box -- it will look smaller/rounder than the mockup but
 * works immediately with zero extra setup.
 *
 * To get the exact mockup look: run Barlow Condensed (700/800 weight)
 * through the LVGL font converter (https://lvgl.io/tools/fontconverter)
 * at the sizes below, add the generated .c files to your build, and swap
 * the DASH_FONT_* macros at the top of honda_dash_ui.c to point at them:
 *   DASH_FONT_SPEED   -> 118px  (speed readout)
 *   DASH_FONT_RPM     -> 58px   (RPM readout)
 *   DASH_FONT_GEAR    -> 56px   (gear number)
 *   DASH_FONT_TILEVAL -> 44px   (the 10 data tile values)
 *   DASH_FONT_LABEL   -> 12px   (tile labels / units / telltales)
 */

#ifndef HONDA_DASH_UI_H
#define HONDA_DASH_UI_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One snapshot of everything the cluster displays. Populate this from your
 * existing (working) CANbus/Hondata read code and pass it to
 * honda_dash_ui_update(). Any field you don't have yet can be left at 0 --
 * nothing will crash, it'll just show 0 / N / OFF until you wire it up. */
typedef struct {
    uint16_t rpm;          /* 0-9000                                   */
    float    speed_mph;    /* 0-200                                    */
    int8_t   gear;         /* 0 = Neutral, 1-6 = gear                  */
    float    ect_f;        /* coolant temp, deg F                      */
    float    iat_f;        /* intake air temp, deg F                   */
    float    afr;          /* air/fuel ratio, e.g. 14.7                */
    float    timing_deg;   /* ignition timing, deg BTDC                */
    float    map_psi;      /* manifold absolute pressure, PSI          */
    float    batt_v;       /* battery voltage                          */
    float    tps_pct;      /* throttle position, 0-100                 */
    float    oil_psi;      /* oil pressure, PSI                        */
    float    duty_pct;     /* injector duty cycle, 0-100               */
    float    knock_deg;    /* knock retard, degrees                    */
    bool     oil_valid;
    bool     duty_valid;
    bool     knock_valid;
    bool     cel;          /* check engine / fault flag                */
    double   odo_miles;    /* total odometer reading, miles             */
    float    fuel_pct;     /* fuel level, 0-100                        */
} honda_dash_data_t;

/* Builds the full cluster UI as a child of `parent` (usually lv_scr_act()).
 * Call once, after LVGL + your display driver are initialized.
 * Returns the root container object (1024x600). */
lv_obj_t *honda_dash_ui_create(lv_obj_t *parent);

/* Pushes a fresh data snapshot into the UI. Cheap, but not free -- every
 * call touches ~90 widgets (10 tiles + RPM bar + speed/gear + telltales).
 * Call this from a periodic task/timer at roughly 20-30Hz rather than on
 * every single incoming CAN frame; that's plenty smooth for a gauge
 * cluster and keeps LVGL's redraw load sane. */
void honda_dash_ui_update(const honda_dash_data_t *data);

/* Applies staged shift colors to built-in RPM indicators from raw drivetrain
 * data. Imported themes retain their authored RPM colors. */
void honda_dash_ui_update_shift_lights(uint16_t rpm);

#ifdef __cplusplus
}
#endif

#endif /* HONDA_DASH_UI_H */
