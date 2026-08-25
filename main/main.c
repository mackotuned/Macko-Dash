#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "honda_dash_ui.h"
#include <stdint.h>
#include <math.h>
#include "esp_timer.h"
#include "odometer/odometer.h"
#include "canbus.h"
#include "dashboard_runtime.h"
#include "uart_file_transfer.h"
#include "ota_c6_hosted_bridge.h"
#include "dash_sim.h"
#include "dash_config.h"
#include "data_logger.h"
#include "session_peaks.h"
#include "theme_storage.h"

#define MIN_VALID_SPEED_MPH 3.0f
#define BOOT_LOGO_DURATION_MS 1500
#define ENABLE_STARTUP_SWEEP 1
#define STARTUP_SWEEP_DURATION_MS 2800
#define GAUGE_TIMER_PERIOD_MS 20

#define RPM_MIN 0
#define RPM_MAX 9000

LV_IMG_DECLARE(boot_logo);

static honda_dash_data_t g_last_ui_data = {0};
static bool g_last_ui_valid = false;
static TaskHandle_t s_can_startup_task = NULL;
static TaskHandle_t s_can_rx_task = NULL;
static TaskHandle_t s_odometer_tracking_task = NULL;
static lv_timer_t *s_gauge_timer = NULL;
static bool s_ota_mode_active = false;
static bool s_render_paused_active = false;

static void odometer_tracking_task(void *arg);
static void start_can_background_tasks(void);
static void stop_can_background_tasks(void);
static void start_gauge_timer(void);
static void stop_gauge_timer(void);

static bool ui_data_changed_significantly(const honda_dash_data_t *prev, const honda_dash_data_t *cur)
{
    if (abs((int)prev->rpm - (int)cur->rpm) >= 25) return true;
    if (prev->gear != cur->gear) return true;
    if (prev->cel != cur->cel) return true;
    if (prev->oil_valid != cur->oil_valid) return true;
    if (prev->duty_valid != cur->duty_valid) return true;
    if (prev->knock_valid != cur->knock_valid) return true;

    if (fabsf(prev->speed_mph - cur->speed_mph) >= 0.8f) return true;
    if (fabsf(prev->ect_f - cur->ect_f) >= 1.0f) return true;
    if (fabsf(prev->iat_f - cur->iat_f) >= 1.0f) return true;
    if (fabsf(prev->afr - cur->afr) >= 0.1f) return true;
    if (fabsf(prev->timing_deg - cur->timing_deg) >= 1.0f) return true;
    if (fabsf(prev->map_psi - cur->map_psi) >= 0.2f) return true;
    if (fabsf(prev->batt_v - cur->batt_v) >= 0.1f) return true;
    if (fabsf(prev->tps_pct - cur->tps_pct) >= 1.0f) return true;
    if (fabsf(prev->oil_psi - cur->oil_psi) >= 1.0f) return true;
    if (fabsf(prev->duty_pct - cur->duty_pct) >= 1.0f) return true;
    if (fabsf(prev->knock_deg - cur->knock_deg) >= 0.1f) return true;
    if (fabs(prev->odo_miles - cur->odo_miles) >= 0.1) return true;
    if (fabsf(prev->fuel_pct - cur->fuel_pct) >= 1.0f) return true;

    return false;
}

void gauge_timer(lv_timer_t * t) {

    (void)t;

    if (s_ota_mode_active) {
        return;
    }

    static bool startup_sweep_done = !ENABLE_STARTUP_SWEEP;
    static int64_t startup_sweep_start_us = 0;
    static bool startup_base_inited = false;
    static honda_dash_data_t startup_base = {0};

    honda_dash_data_t d = {0};

    if (!startup_sweep_done) {
        int64_t now_us = esp_timer_get_time();

        if (startup_sweep_start_us == 0)
            startup_sweep_start_us = now_us;

        float elapsed_ms = (now_us - startup_sweep_start_us) / 1000.0f;
        float phase = elapsed_ms / (float)STARTUP_SWEEP_DURATION_MS;

        if (phase > 1.0f)
            phase = 1.0f;

        // Triangle sweep with easing so the needle starts/ends softer.
        float tri = (phase <= 0.5f) ? (phase * 2.0f) : ((1.0f - phase) * 2.0f);
        float p = tri * tri * (3.0f - 2.0f * tri); // smoothstep(tri)

        if (!startup_base_inited) {
            startup_base = (honda_dash_data_t){0};
            startup_base.afr = 14.7f;
            startup_base.oil_valid = true;
            startup_base.duty_valid = true;
            startup_base.knock_valid = true;
            startup_base.odo_miles = odometer_get_miles();
            startup_base.fuel_pct = can_data.fuel_level;
            startup_base_inited = true;
        }

        // Startup sweep affects RPM only; all other widgets stay static.
        d = startup_base;
        d.rpm = (uint16_t)(RPM_MIN + p * (float)(RPM_MAX - RPM_MIN));

        honda_dash_ui_update(&d);

        if (elapsed_ms >= STARTUP_SWEEP_DURATION_MS)
            startup_sweep_done = true;

        return;
    }

    bool drivetrain_live = dash_sim_is_enabled();
    if (dash_sim_is_enabled()) {
        dash_sim_step(&d);
        d.oil_valid = true;
        d.duty_valid = true;
        d.knock_valid = true;
    } else {
    static float displayRPM = 0.0f;
    displayRPM += 0.20f * (can_data.rpm - displayRPM);

    int gear = 0;
    bool can_live = canbus_has_live_data();
    drivetrain_live = canbus_has_live_drivetrain();
    bool can_gear_live = canbus_has_live_gear();

    if (can_live && can_gear_live && can_data.gear > 0.0f && can_data.gear < 10.0f) {
        gear = (int)can_data.gear;
    }

    float speed_mph = can_data.speed * 0.621371f;

    if (speed_mph < MIN_VALID_SPEED_MPH)
        speed_mph = 0.0f;

    float display_tps = can_data.tps;

    if (display_tps < 1.0f)
        display_tps = 0.0f;
    else if (display_tps > 100.0f)
        display_tps = 100.0f;

    d.rpm        = (uint16_t)(displayRPM < 0 ? 0 : displayRPM);
    d.speed_mph  = speed_mph;
    d.gear       = (int8_t)gear;
    d.ect_f      = can_data.coolant_temp;
    d.iat_f      = can_data.air_temp;
    d.afr        = can_data.air_fuel_ratio;
    d.timing_deg = can_data.ign_angle;
    d.map_psi    = can_data.boost;
    d.batt_v     = can_data.battery_voltage;
    d.tps_pct    = display_tps;
    d.oil_psi    = can_data.oil_pressure;
    /* Not decoded by canbus.c yet -- hondata.json already carries
       inj_duration / knock_count signals on the wire (frames 0x663/0x665),
       they're just not wired into can_dash_data_t or protocol_loader's
       signal_name_to_ptr() yet. Wire those up if you want live duty/knock. */
    d.duty_pct   = 0.0f;
    d.knock_deg  = 0.0f;
    bool optional_grace = esp_timer_get_time() < 30000000;
    d.oil_valid   = optional_grace || canbus_has_recent_oil_pressure();
    d.duty_valid  = true;
    d.knock_valid = optional_grace;
    d.cel        = false;
    d.odo_miles  = odometer_get_miles();
    d.fuel_pct   = can_data.fuel_level;
    }

    data_logger_submit(&d);
    session_peaks_update(&d, drivetrain_live);

    bool changed = dash_config_get_value_smoothing() ||
                   (!g_last_ui_valid) || ui_data_changed_significantly(&g_last_ui_data, &d);
    if (changed) {
        honda_dash_ui_update(&d);
        g_last_ui_data = d;
        g_last_ui_valid = true;
    }
}

//------------------------------------------------------------------------//



void save_miles_task(void *arg){
    while (1){
        odometer_periodic_save();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void can_startup_task(void *arg)
{
    canbus_init();

    xTaskCreatePinnedToCore(canbus_task, "can_rx", 4096, NULL, 8, &s_can_rx_task, 1);
    xTaskCreatePinnedToCore(odometer_tracking_task, "odometer_tracking", 3072, NULL, 7, &s_odometer_tracking_task, 1);

    s_can_startup_task = NULL;

    vTaskDelete(NULL);
}

static void odometer_tracking_task(void *arg){
    int64_t last_odo_ms = 0;

    while (1){
        int64_t now_ms = esp_timer_get_time() / 1000;

        // ---------- Odometer ----------
        if (now_ms - last_odo_ms >= 1000){
            last_odo_ms = now_ms;

            float speed_kph = can_data.speed;

            // meters per second
            float meters_per_sec = speed_kph / 3.6f;

            uint32_t whole = (uint32_t)meters_per_sec;

            if (whole > 0)
                odometer_add_meters(whole);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void start_can_background_tasks(void)
{
    if (s_can_startup_task || s_can_rx_task || s_odometer_tracking_task) {
        return;
    }

    xTaskCreatePinnedToCore(can_startup_task, "can_startup", 6144, NULL, 7, &s_can_startup_task, 1);
}

static void stop_can_background_tasks(void)
{
    if (s_can_startup_task) {
        vTaskDelete(s_can_startup_task);
        s_can_startup_task = NULL;
    }

    if (s_can_rx_task) {
        vTaskDelete(s_can_rx_task);
        s_can_rx_task = NULL;
    }

    if (s_odometer_tracking_task) {
        vTaskDelete(s_odometer_tracking_task);
        s_odometer_tracking_task = NULL;
    }

    canbus_shutdown();
}

static void start_gauge_timer(void)
{
    if (!s_gauge_timer) {
        s_gauge_timer = lv_timer_create(gauge_timer, GAUGE_TIMER_PERIOD_MS, NULL);
    }
}

static void stop_gauge_timer(void)
{
    if (s_gauge_timer) {
        lv_timer_del(s_gauge_timer);
        s_gauge_timer = NULL;
    }
}

void dashboard_runtime_set_ota_mode(bool enabled)
{
    if (s_ota_mode_active == enabled) {
        return;
    }

    s_ota_mode_active = enabled;

    if (enabled) {
        stop_gauge_timer();
        stop_can_background_tasks();
    } else {
        start_can_background_tasks();
        start_gauge_timer();
    }
}

void dashboard_runtime_set_render_paused(bool paused)
{
    if (s_render_paused_active == paused) {
        return;
    }

    s_render_paused_active = paused;

    /* deliberately does NOT touch CAN background tasks -- those involve
       tearing down and re-initializing the TWAI driver, which is only
       safe to do rarely (the Update page's own OTA-mode pause), not on
       every settings-menu open/close */
    if (paused) {
        stop_gauge_timer();
    } else {
        start_gauge_timer();
    }
}

void app_main(void) {
    dash_config_init();
    session_peaks_init();

    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        }
    };

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) {
        ESP_LOGE("main", "display init failed");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    } else {
        ESP_LOGI("main", "display init complete");
    }

    esp_err_t theme_storage_err = theme_storage_init();
    ESP_LOGI("main", "Theme storage initialization -> %s", esp_err_to_name(theme_storage_err));
    data_logger_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(uart_file_transfer_start());

#if CONFIG_HONDA_DASH_ENABLE_WIFI_OTA
    esp_err_t bridge_err = ota_c6_hosted_bridge_register();
    ESP_LOGI("main", "C6 hosted OTA bridge register -> %s", esp_err_to_name(bridge_err));
#else
    ESP_LOGI("main", "WiFi/OTA feature disabled at build time");
#endif

    esp_err_t ui_lock_err = bsp_display_lock(1000);
    if (ui_lock_err != ESP_OK) {
        ESP_LOGE("main", "bsp_display_lock failed: %s", esp_err_to_name(ui_lock_err));
    } else {
        lv_obj_t *screen = lv_scr_act();
        lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_t *logo = lv_img_create(screen);
        lv_img_set_src(logo, &boot_logo);
        lv_obj_center(logo);

        lv_refr_now(disp);
        bsp_display_brightness_set(dash_config_get_brightness());
        vTaskDelay(pdMS_TO_TICKS(BOOT_LOGO_DURATION_MS));

        lv_obj_clean(screen);
        honda_dash_ui_create(screen);
#if !ENABLE_STARTUP_SWEEP
        honda_dash_data_t boot_frame = {0};
        honda_dash_ui_update(&boot_frame);
#endif
        lv_refr_now(disp);

        start_gauge_timer();
        bsp_display_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(1));

    odometer_init();
    vTaskDelay(pdMS_TO_TICKS(1));

    start_can_background_tasks();

    xTaskCreatePinnedToCore(save_miles_task, "save_miles_task", 4096, NULL, 4, NULL, 0);

}
