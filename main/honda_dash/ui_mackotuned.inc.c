/*
 * MackoTuned settings shell + gauge channel picker.
 *
 * #included by honda_dash_ui.c after theme_v2.inc.c (shares its fonts and
 * colours). The shell wraps the existing settings pages: a left section
 * menu, a header with the page title / BACK / DONE, and the page content to
 * the right -- all kept clear of the dash bezel (top corners, bottom edge).
 * Every page, setting and callback is the original one; only the frame and
 * navigation changed.
 */

#define SHELL_NAV_W     252
#define SHELL_TOP       58      /* below the bezel arc at the side menu / header corners */
#define SHELL_HEADER_H  60
#define SHELL_BOTTOM    V2_BOTTOM_SAFE

typedef struct {
    const char *label;
    lv_obj_t **root;            /* the section's first page */
    lv_event_cb_t open_cb;
} shell_section_t;

static const shell_section_t SHELL_SECTIONS[] = {
    {"Appearance", &s_page_theme, settings_open_theme_cb},
    {"Display", &s_page_display, settings_open_display_cb},
    {"Drive Logs", &s_page_logs_menu, settings_open_logs_menu_cb},
    {"ECU Connection", &s_page_ecu, settings_open_ecu_cb},
    {"Engine Limits", &s_page_engine_limits, settings_open_engine_limits_cb},
    {"System", &s_page_config, settings_open_config_cb},
};
#define SHELL_SECTION_COUNT ((int)(sizeof(SHELL_SECTIONS) / sizeof(SHELL_SECTIONS[0])))

static lv_obj_t *s_shell_nav_btn[SHELL_SECTION_COUNT];
static lv_obj_t *s_shell_nav_label[SHELL_SECTION_COUNT];
static lv_obj_t *s_shell_nav_mark[SHELL_SECTION_COUNT];
static lv_obj_t *s_shell_title;
static lv_obj_t *s_shell_back_btn;
static int s_shell_section = 0;

static const char *shell_page_title(lv_obj_t *page)
{
    struct { lv_obj_t **page; const char *title; } titles[] = {
        {&s_page_theme, "APPEARANCE"}, {&s_page_display, "DISPLAY"}, {&s_page_units, "UNITS"},
        {&s_page_odometer, "ODOMETER"}, {&s_page_logs_menu, "DRIVE LOGS"}, {&s_page_logs, "LOG FILES"},
        {&s_page_peaks, "SESSION PEAKS"}, {&s_page_achievements, "ACHIEVEMENTS"},
        {&s_page_ecu, "ECU CONNECTION"}, {&s_page_engine_limits, "ENGINE LIMITS"},
        {&s_page_config, "SYSTEM"}, {&s_page_fuel, "FUEL GAUGE"},
        {&s_page_info, "ABOUT"}, {&s_page_shortcuts, "SHORTCUTS"}, {&s_page_readme, "GUIDE"},
        {&s_page_theme_resets, "THEME RESETS"}, {&s_page_contact, "SUPPORT"}, {&s_page_update, "FIRMWARE UPDATE"},
        {&s_page_wifi, "WI-FI"}, {&s_page_knob, "KNOB"},
    };
    for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); ++i) {
        if (*titles[i].page == page) return titles[i].title;
    }
    return "SETTINGS";
}

/* Called by settings_show_page(): keep the menu, title and BACK in sync. */
static void shell_page_shown(lv_obj_t *page)
{
    if (!s_shell_title) return;
    for (int i = 0; i < SHELL_SECTION_COUNT; ++i) {
        if (*SHELL_SECTIONS[i].root == page) s_shell_section = i;
    }
    /* sub-pages light up their parent section (also when opened by the knob) */
    struct { lv_obj_t **page; lv_obj_t **parent; } children[] = {
        {&s_page_peaks, &s_page_logs_menu}, {&s_page_logs, &s_page_logs_menu},
        {&s_page_achievements, &s_page_logs_menu}, {&s_page_units, &s_page_display},
        {&s_page_odometer, &s_page_display}, {&s_page_fuel, &s_page_config}, {&s_page_info, &s_page_config},
        {&s_page_shortcuts, &s_page_config}, {&s_page_readme, &s_page_config},
        {&s_page_theme_resets, &s_page_config}, {&s_page_contact, &s_page_config},
        {&s_page_update, &s_page_config}, {&s_page_wifi, &s_page_config},
        {&s_page_knob, &s_page_config},
    };
    for (size_t c = 0; c < sizeof(children) / sizeof(children[0]); ++c) {
        if (*children[c].page != page) continue;
        for (int i = 0; i < SHELL_SECTION_COUNT; ++i) {
            if (*SHELL_SECTIONS[i].root == *children[c].parent) s_shell_section = i;
        }
    }
    for (int i = 0; i < SHELL_SECTION_COUNT; ++i) {
        bool on = i == s_shell_section;
        ui_set_bg_color(s_shell_nav_btn[i], on ? V2_RED : V2_STRIP, LV_PART_MAIN);
        ui_set_text_color(s_shell_nav_label[i], on ? lv_color_white() : lv_color_hex(0xc9ccd1), LV_PART_MAIN);
        ui_set_bg_color(s_shell_nav_mark[i], on ? lv_color_white() : lv_color_hex(0x3a3d44), LV_PART_MAIN);
    }
    set_label_text_if_changed(s_shell_title, shell_page_title(page));
    bool root = *SHELL_SECTIONS[s_shell_section].root == page;
    if (root) lv_obj_add_flag(s_shell_back_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(s_shell_back_btn, LV_OBJ_FLAG_HIDDEN);
}

/* The old tile menu page is replaced by the side menu. */
static lv_obj_t *shell_redirect_page(lv_obj_t *page)
{
    if (page != s_page_main) return page;
    return *SHELL_SECTIONS[s_shell_section].root;
}

static void shell_nav_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= SHELL_SECTION_COUNT) return;
    s_shell_section = index;
    SHELL_SECTIONS[index].open_cb(e);
}

static lv_obj_t *shell_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t border,
                              lv_coord_t w, lv_coord_t h, lv_event_cb_t cb)
{
    lv_obj_t *button = v2_box(parent, 0, 0, w, h, bg, 4);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    ui_set_border_color(button, border, LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    add_press_feedback(button);
    lv_obj_t *label = v2_label(button, text, &v2_font_label16, lv_color_white());
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    lv_obj_center(label);
    return button;
}

/* Builds the frame around the existing pages. `panel` holds the pages. */
static void shell_build(lv_obj_t *overlay, lv_obj_t *panel)
{
    ui_set_bg_color(overlay, V2_BG, LV_PART_MAIN);

    lv_obj_set_size(panel, SCR_W - SHELL_NAV_W, SCR_H - SHELL_TOP - SHELL_HEADER_H - SHELL_BOTTOM);
    lv_obj_align(panel, LV_ALIGN_TOP_LEFT, SHELL_NAV_W, SHELL_TOP + SHELL_HEADER_H);
    ui_set_bg_color(panel, V2_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(panel, 24, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel, 6, LV_PART_MAIN);

    /* wordmark in the visible centre of the top edge */
    lv_obj_t *brand = make_plain_container(overlay);
    lv_obj_add_flag(brand, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brand, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    v2_label(brand, "MACKO", &v2_font_brand18, V2_TEXT);
    v2_label(brand, "DASH", &v2_font_brand18, V2_RED);
    lv_obj_t *caption = v2_label(brand, "SETTINGS", &v2_font_label13, V2_UNIT);
    lv_obj_set_style_text_letter_space(caption, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_left(caption, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(caption, 2, LV_PART_MAIN);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 16);

    /* side menu */
    lv_obj_t *nav = v2_box(overlay, 0, SHELL_TOP, SHELL_NAV_W, SCR_H - SHELL_TOP - SHELL_BOTTOM, V2_STRIP, 0);
    lv_obj_add_flag(nav, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_border_width(nav, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    ui_set_border_color(nav, lv_color_hex(0x1f2126), LV_PART_MAIN);
    lv_obj_set_style_pad_all(nav, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(nav, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(nav, 4, LV_PART_MAIN);
    for (int i = 0; i < SHELL_SECTION_COUNT; ++i) {
        lv_obj_t *button = v2_box(nav, 0, 0, LV_PCT(100), 50, V2_STRIP, 4);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, shell_nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        add_press_feedback(button);
        s_shell_nav_mark[i] = v2_box(button, 14, 21, 8, 8, lv_color_hex(0x3a3d44), 0);
        s_shell_nav_label[i] = v2_label(button, SHELL_SECTIONS[i].label, &v2_font_label16,
                                        lv_color_hex(0xc9ccd1));
        lv_obj_align(s_shell_nav_label[i], LV_ALIGN_LEFT_MID, 34, 0);
        s_shell_nav_btn[i] = button;
    }
    lv_obj_t *spacer = make_plain_container(nav);
    lv_obj_set_size(spacer, LV_PCT(100), 1);
    lv_obj_set_flex_grow(spacer, 1);
    const esp_app_desc_t *app = esp_app_get_description();
    char version[48];
    snprintf(version, sizeof(version), "Firmware %s", app ? app->version : "");
    v2_label(nav, version, &v2_font_label13, V2_UNIT);

    /* header: page title + red underline, BACK, DONE */
    lv_obj_t *header = make_plain_container(overlay);
    lv_obj_add_flag(header, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(header, SHELL_NAV_W + 24, SHELL_TOP + 6);
    lv_obj_set_size(header, SCR_W - SHELL_NAV_W - 24 - 18, SHELL_HEADER_H - 8);
    s_shell_title = v2_label(header, "SETTINGS", &v2_font_value34, V2_TEXT);
    lv_obj_align(s_shell_title, LV_ALIGN_TOP_LEFT, 0, -2);
    lv_obj_t *underline = v2_box(header, 2, 42, 120, 3, V2_RED, 0);
    (void)underline;
    lv_obj_t *done = shell_button(header, "DONE", V2_RED, V2_RED, 116, 46, settings_close_cb);
    lv_obj_align(done, LV_ALIGN_TOP_RIGHT, 0, 0);
    s_shell_back_btn = shell_button(header, "BACK", V2_CHIP_OFF, V2_PILL_LINE, 104, 46, settings_nav_cb);
    lv_obj_align_to(s_shell_back_btn, done, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_add_flag(s_shell_back_btn, LV_OBJ_FLAG_HIDDEN);

    /* the old floating round back/close button is replaced by BACK / DONE */
    if (s_settings_nav_button) lv_obj_add_flag(s_settings_nav_button, LV_OBJ_FLAG_HIDDEN);
}

/* ====================== gauge channel picker ============================== */

enum {
    PICK_ENGINE = 0, PICK_TEMPS, PICK_PRESSURES, PICK_FUEL, PICK_BOOST, PICK_CHASSIS, PICK_SWITCHES,
    PICK_TORQUE, PICK_GROUP_COUNT
};
static const char *const PICK_GROUP_NAMES[PICK_GROUP_COUNT] = {
    "Engine", "Temperatures", "Pressures", "Fuel & AFR", "Boost & Turbo", "Chassis", "Switches",
    "Torque & Launch",
};
#define PICK_MAX_BUTTONS 64

static lv_obj_t *s_pick;
static lv_obj_t *s_pick_group_btn[PICK_GROUP_COUNT];
static lv_obj_t *s_pick_group_label[PICK_GROUP_COUNT];
static lv_obj_t *s_pick_group_count[PICK_GROUP_COUNT];
static lv_obj_t *s_pick_grid;
static lv_obj_t *s_pick_limits;
static lv_obj_t *s_pick_limits_btn;
static lv_obj_t *s_pick_limits_label;
static lv_obj_t *s_pick_use_label;
static lv_obj_t *s_pick_btn[PICK_MAX_BUTTONS];
static lv_obj_t *s_pick_value[PICK_MAX_BUTTONS];
static lv_obj_t *s_pick_unit[PICK_MAX_BUTTONS];
static int s_pick_btn_channel[PICK_MAX_BUTTONS];
static int s_pick_btn_count;
static int s_pick_group;
static lv_timer_t *s_pick_timer;

static int pick_group_of(int channel)
{
    if (!system_field_is_ecu(channel)) {
        switch (channel) {
            case TILE_ECT: case TILE_IAT: return PICK_TEMPS;
            case TILE_AFR: case TILE_DUTY: case TILE_COUNT: return PICK_FUEL;   /* TILE_COUNT = fuel % */
            case TILE_MAP: return PICK_BOOST;
            case TILE_OIL: return PICK_PRESSURES;
            default: return PICK_ENGINE;
        }
    }
    int ecu = channel - DASH_CONFIG_HAL_CHANNEL_COUNT;
    const char *sig = ecu_channel_signal(ecu);
    ecu_unit_t unit = ecu_channel_unit_kind(ecu);
    if (strstr(sig, "turbo") || strstr(sig, "boost") || strstr(sig, "wastegate")) return PICK_BOOST;
    if (strstr(sig, "launch") || strncmp(sig, "tq_", 3) == 0 || strstr(sig, "torque") ||
            strncmp(sig, "tc_", 3) == 0 || strstr(sig, "nitrous") || strstr(sig, "race_timer") ||
            strstr(sig, "aux_limiter") || strstr(sig, "flat_shift") || strstr(sig, "engine_limiting")) {
        return PICK_TORQUE;
    }
    if (unit == ECU_UNIT_BOOL) return PICK_SWITCHES;
    if (unit == ECU_UNIT_TEMP) return PICK_TEMPS;
    if (unit == ECU_UNIT_PRESSURE) return PICK_PRESSURES;
    if (strstr(sig, "fuel") || strncmp(sig, "inj_", 4) == 0 || strstr(sig, "wideband") || strstr(sig, "lambda") ||
            strncmp(sig, "stft", 4) == 0 || strncmp(sig, "ltft", 4) == 0) {
        return PICK_FUEL;
    }
    if (unit == ECU_UNIT_SPEED || unit == ECU_UNIT_G || strstr(sig, "steering") || strstr(sig, "driveshaft") ||
            strstr(sig, "cruise") || strstr(sig, "gear_selector")) {
        return PICK_CHASSIS;
    }
    return PICK_ENGINE;
}

static const char *pick_theme_name(system_field_theme_t theme)
{
    switch (theme) {
        case SYSTEM_FIELD_MODERN: return "MACKODASH V1";
        case SYSTEM_FIELD_V2: return s_active_theme == THEME_ID_TACH ? "MACKODASH TACH" : "MACKODASH V2";
        case SYSTEM_FIELD_RACE: return "RACE LCD";
        case SYSTEM_FIELD_ENDURANCE: return "HUNTERDASH";
        case SYSTEM_FIELD_TOURING: return "RALLY STAGE";
        default: return "HALDASH";
    }
}

static void pick_upper(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src);
    for (char *c = dst; *c; ++c) *c = (char)toupper((unsigned char)*c);
}

static void pick_sample(int channel, float *value, bool *ok)
{
    const honda_dash_data_t *d = &s_last_data;
    float fuel = d->fuel_pct < 0 ? 0 : (d->fuel_pct > 100 ? 100 : d->fuel_pct);
    float values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        d->ect_f, d->iat_f, d->afr, d->timing_deg, d->map_psi,
        d->batt_v, d->tps_pct, d->oil_psi, d->duty_pct, d->knock_deg, fuel,
    };
    bool available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        s_have_last_data, s_have_last_data, s_have_last_data, s_have_last_data, s_have_last_data,
        s_have_last_data, s_have_last_data, d->oil_valid, d->duty_valid, d->knock_valid, s_have_last_data,
    };
    system_field_sample(channel, values, available, value, ok);
}

static void pick_refresh_values(void)
{
    for (int i = 0; i < s_pick_btn_count; ++i) {
        float value;
        bool ok;
        pick_sample(s_pick_btn_channel[i], &value, &ok);
        char text[24];
        if (ok) system_field_format_text(s_pick_btn_channel[i], value, true, text, sizeof(text));
        else snprintf(text, sizeof(text), "--");
        set_label_text_if_changed(s_pick_value[i], text);
        ui_set_text_color(s_pick_value[i], ok ? V2_TEXT : V2_DIM, LV_PART_MAIN);
        set_label_text_if_changed(s_pick_unit[i], ok ? v2_unit_text(hal_channel_unit(s_pick_btn_channel[i]))
                                                     : "not received");
    }
}

static void pick_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_pick && s_pick_grid && !lv_obj_has_flag(s_pick_grid, LV_OBJ_FLAG_HIDDEN)) pick_refresh_values();
}

static void pick_style_selection(void)
{
    for (int i = 0; i < s_pick_btn_count; ++i) {
        bool on = s_pick_btn_channel[i] == s_hal_edit_channel;
        ui_set_bg_color(s_pick_btn[i], on ? V2_CHIP_ON : V2_PANEL, LV_PART_MAIN);
        ui_set_border_color(s_pick_btn[i], on ? V2_RED : V2_PANEL_LINE, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_pick_btn[i], on ? 2 : 1, LV_PART_MAIN);
    }
    char use[40];
    char name[32];
    pick_upper(name, sizeof(name), system_field_name(s_hal_edit_channel));
    snprintf(use, sizeof(use), "USE %s", name);
    lv_label_set_text(s_pick_use_label, use);
    dash_config_threshold_t thresholds[4];
    if (system_field_channel_thresholds(s_hal_edit_channel, thresholds) > 0) {
        lv_obj_clear_flag(s_pick_limits_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pick_limits_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void pick_channel_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= s_pick_btn_count) return;
    s_hal_edit_channel = s_pick_btn_channel[index];
    pick_style_selection();
}

static void pick_show_group(int group)
{
    s_pick_group = group;
    for (int g = 0; g < PICK_GROUP_COUNT; ++g) {
        bool on = g == group;
        ui_set_bg_color(s_pick_group_btn[g], on ? V2_RED : V2_PANEL, LV_PART_MAIN);
        ui_set_border_color(s_pick_group_btn[g], on ? V2_RED : V2_PANEL_LINE, LV_PART_MAIN);
        ui_set_text_color(s_pick_group_label[g], on ? lv_color_white() : lv_color_hex(0xc9ccd1), LV_PART_MAIN);
        ui_set_text_color(s_pick_group_count[g], on ? lv_color_hex(0xffd9df) : V2_UNIT, LV_PART_MAIN);
    }
    lv_obj_clean(s_pick_grid);
    s_pick_btn_count = 0;
    for (int channel = 0; channel < DASH_CONFIG_FIELD_CHANNEL_COUNT && s_pick_btn_count < PICK_MAX_BUTTONS; ++channel) {
        if (pick_group_of(channel) != group) continue;
        int i = s_pick_btn_count++;
        lv_obj_t *button = v2_box(s_pick_grid, 0, 0, 240, 80, V2_PANEL, 4);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        ui_set_border_color(button, V2_PANEL_LINE, LV_PART_MAIN);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, pick_channel_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = v2_label(button, system_field_name(channel), &v2_font_label16, V2_TEXT);
        lv_obj_set_pos(name, 12, 7);
        s_pick_value[i] = v2_label(button, "--", &v2_font_value34, V2_DIM);
        lv_obj_set_pos(s_pick_value[i], 11, 31);
        s_pick_unit[i] = v2_label(button, "", &v2_font_label13, V2_UNIT);
        lv_obj_align(s_pick_unit[i], LV_ALIGN_BOTTOM_RIGHT, -10, -9);
        s_pick_btn[i] = button;
        s_pick_btn_channel[i] = channel;
    }
    lv_obj_scroll_to_y(s_pick_grid, 0, LV_ANIM_OFF);
    pick_refresh_values();
    pick_style_selection();
}

static void pick_group_cb(lv_event_t *e)
{
    int group = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_pick_limits && !lv_obj_has_flag(s_pick_limits, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_pick_limits, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pick_grid, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_pick_limits_label, "LIMITS");
    }
    if (group >= 0 && group < PICK_GROUP_COUNT) pick_show_group(group);
}

static void pick_close(void)
{
    if (s_pick_timer) {
        lv_timer_del(s_pick_timer);
        s_pick_timer = NULL;
    }
    lv_obj_t *picker = s_pick;
    s_pick = NULL;
    s_pick_grid = NULL;
    s_pick_limits = NULL;
    s_hal_threshold_panel = NULL;
    s_pick_btn_count = 0;
    s_hal_edit_slot = -1;
    memset(s_cfg_threshold_sliders, 0, sizeof(s_cfg_threshold_sliders));
    memset(s_cfg_threshold_labels, 0, sizeof(s_cfg_threshold_labels));
    if (picker) lv_obj_del_async(picker);
}

static void pick_cancel_cb(lv_event_t *e)
{
    (void)e;
    pick_close();
}

static void pick_use_cb(lv_event_t *e)
{
    (void)e;
    if (s_hal_edit_slot >= 0) {
        system_field_set_channel(s_system_edit_theme, s_hal_edit_slot, s_hal_edit_channel);
        system_field_refresh_identity(s_system_edit_theme, s_hal_edit_slot);
    }
    pick_close();
}

static void pick_limits_cb(lv_event_t *e)
{
    (void)e;
    bool show_limits = lv_obj_has_flag(s_pick_limits, LV_OBJ_FLAG_HIDDEN);
    if (show_limits) {
        lv_obj_add_flag(s_pick_grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pick_limits, LV_OBJ_FLAG_HIDDEN);
        s_hal_threshold_panel = s_pick_limits;
        hal_channel_thresholds_rebuild();
        lv_label_set_text(s_pick_limits_label, "GAUGES");
    } else {
        lv_obj_add_flag(s_pick_limits, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pick_grid, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_pick_limits_label, "LIMITS");
    }
}

static void pick_open(system_field_theme_t theme, int slot)
{
    if (s_pick) return;
    s_system_edit_theme = theme;
    s_hal_edit_slot = slot;
    s_hal_edit_channel = system_field_get_channel(theme, slot);
    if (s_hal_edit_channel < 0 || s_hal_edit_channel >= DASH_CONFIG_FIELD_CHANNEL_COUNT) s_hal_edit_channel = 0;

    s_pick = v2_box(s_cluster, 0, 0, SCR_W, SCR_H, V2_BG, 0);
    lv_obj_add_flag(s_pick, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_pick);

    lv_obj_t *brand = make_plain_container(s_pick);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    v2_label(brand, "MACKO", &v2_font_brand18, V2_TEXT);
    v2_label(brand, "DASH", &v2_font_brand18, V2_RED);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 14);

    char caption[48];
    snprintf(caption, sizeof(caption), "TILE %d  \xC2\xB7  %s", slot + 1, pick_theme_name(theme));
    lv_obj_t *caption_label = v2_label(s_pick, caption, &v2_font_label13, V2_LABEL);
    lv_obj_set_style_text_letter_space(caption_label, 2, LV_PART_MAIN);
    lv_obj_set_pos(caption_label, 24, 60);
    lv_obj_t *title = v2_label(s_pick, "CHOOSE A GAUGE", &v2_font_value34, V2_TEXT);
    lv_obj_set_pos(title, 24, 76);
    v2_box(s_pick, 26, 118, 110, 3, V2_RED, 0);

    lv_obj_t *use = shell_button(s_pick, "USE", V2_RED, V2_RED, 240, 48, pick_use_cb);
    lv_obj_set_pos(use, SCR_W - 24 - 240, 66);
    s_pick_use_label = lv_obj_get_child(use, 0);
    lv_label_set_long_mode(s_pick_use_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_pick_use_label, 220);
    lv_obj_set_style_text_align(s_pick_use_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(s_pick_use_label);
    lv_obj_t *cancel = shell_button(s_pick, "CANCEL", V2_CHIP_OFF, V2_PILL_LINE, 120, 48, pick_cancel_cb);
    lv_obj_set_pos(cancel, SCR_W - 24 - 240 - 10 - 120, 66);
    s_pick_limits_btn = shell_button(s_pick, "LIMITS", V2_CHIP_OFF, V2_PILL_LINE, 120, 48, pick_limits_cb);
    lv_obj_set_pos(s_pick_limits_btn, SCR_W - 24 - 240 - 10 - 120 - 10 - 120, 66);
    s_pick_limits_label = lv_obj_get_child(s_pick_limits_btn, 0);

    int counts[PICK_GROUP_COUNT] = {0};
    for (int channel = 0; channel < DASH_CONFIG_FIELD_CHANNEL_COUNT; ++channel) counts[pick_group_of(channel)]++;
    for (int g = 0; g < PICK_GROUP_COUNT; ++g) {
        lv_obj_t *button = v2_box(s_pick, 24, 134 + g * 50, 196, 46, V2_PANEL, 4);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        ui_set_border_color(button, V2_PANEL_LINE, LV_PART_MAIN);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, pick_group_cb, LV_EVENT_CLICKED, (void *)(intptr_t)g);
        s_pick_group_label[g] = v2_label(button, PICK_GROUP_NAMES[g], &v2_font_label16, lv_color_hex(0xc9ccd1));
        lv_obj_align(s_pick_group_label[g], LV_ALIGN_LEFT_MID, 12, 0);
        char count[8];
        snprintf(count, sizeof(count), "%d", counts[g]);
        s_pick_group_count[g] = v2_label(button, count, &v2_font_label13, V2_UNIT);
        lv_obj_align(s_pick_group_count[g], LV_ALIGN_RIGHT_MID, -12, 0);
        s_pick_group_btn[g] = button;
    }

    lv_coord_t area_x = 24 + 196 + 14;
    lv_coord_t area_y = 134;
    lv_coord_t area_w = SCR_W - area_x - 24;
    lv_coord_t area_h = SCR_H - area_y - SHELL_BOTTOM - 30;
    s_pick_grid = v2_box(s_pick, area_x, area_y, area_w, area_h, V2_BG, 0);
    lv_obj_add_flag(s_pick_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_pick_grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_pick_grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_pick_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(s_pick_grid, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_pick_grid, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_pick_grid, 4, LV_PART_MAIN);

    s_pick_limits = v2_box(s_pick, area_x, area_y, area_w, area_h, V2_BG, 0);
    lv_obj_set_flex_flow(s_pick_limits, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(s_pick_limits, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_pick_limits, 12, LV_PART_MAIN);
    lv_obj_add_flag(s_pick_limits, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *note = v2_label(s_pick, "Live values come from the ECU. Channels your ECU isn't sending show \"not received\".",
                              &v2_font_label13, V2_UNIT);
    lv_obj_set_pos(note, 24, SCR_H - SHELL_BOTTOM - 22);

    pick_show_group(pick_group_of(s_hal_edit_channel));
    s_pick_timer = lv_timer_create(pick_timer_cb, 300, NULL);
}

/* ====================== ECU Connection page ================================ */

static lv_obj_t *s_ecu_status_value;
static lv_obj_t *s_ecu_status_dot;
static lv_obj_t *s_ecu_protocol_value;
static lv_obj_t *s_ecu_bitrate_value;
static lv_obj_t *s_ecu_channels_value;
static lv_obj_t *s_ecu_last_frame_value;
static lv_obj_t *s_ecu_errors_value;
static lv_obj_t *s_ecu_missed_value;

static lv_obj_t *ecu_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    ui_set_bg_color(card, V2_PANEL, LV_PART_MAIN);
    ui_set_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    ui_set_border_color(card, V2_PANEL_LINE, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *ecu_stat(lv_obj_t *parent, const char *caption, lv_obj_t **row_out)
{
    lv_obj_t *column = make_plain_container(parent);
    lv_obj_set_size(column, LV_PCT(25), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(column, 4, LV_PART_MAIN);
    lv_obj_t *label = v2_label(column, caption, &v2_font_label13, V2_LABEL);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    lv_obj_t *row = make_plain_container(column);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
    if (row_out) *row_out = row;
    return row;
}

static lv_obj_t *ecu_diag_row(lv_obj_t *card, const char *name, bool divider)
{
    lv_obj_t *row = make_plain_container(card);
    lv_obj_set_size(row, LV_PCT(100), 44);
    lv_obj_set_style_pad_hor(row, 16, LV_PART_MAIN);
    if (divider) {
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        ui_set_border_color(row, lv_color_hex(0x1f2126), LV_PART_MAIN);
    }
    lv_obj_t *label = v2_label(row, name, &v2_font_label16, lv_color_hex(0xc9ccd1));
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *value = v2_label(row, "--", &v2_font_label16, V2_TEXT);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
    return value;
}

static const char *ecu_protocol_label(const char *name)
{
    for (size_t i = 0; i < CFG_PROTOCOL_COUNT; ++i) {
        if (CFG_PROTOCOL_NAMES[i][0] && strcmp(CFG_PROTOCOL_NAMES[i], name) == 0) return CFG_PROTOCOL_LABELS[i];
    }
    return name && name[0] ? name : "Detecting";
}

static void ecu_page_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_page_ecu || lv_obj_has_flag(s_page_ecu, LV_OBJ_FLAG_HIDDEN) ||
            lv_obj_has_flag(s_settings_overlay, LV_OBJ_FLAG_HIDDEN)) return;
    canbus_diagnostics_t diag;
    canbus_get_diagnostics(&diag);
    bool live = diag.live_data;
    bool running = diag.controller_state == CANBUS_CONTROLLER_RUNNING;
    lv_color_t color = live ? V2_GREEN : (running ? V2_AMBER : V2_RED_TEXT);
    set_label_text_if_changed(s_ecu_status_value, live ? "Live data" : (running ? "No data" : "Offline"));
    ui_set_text_color(s_ecu_status_value, color, LV_PART_MAIN);
    ui_set_bg_color(s_ecu_status_dot, color, LV_PART_MAIN);
    set_label_text_if_changed(s_ecu_protocol_value, ecu_protocol_label(diag.protocol));
    char text[32];
    if (diag.bitrate >= 1000000) snprintf(text, sizeof(text), "%d Mbit/s", diag.bitrate / 1000000);
    else if (diag.bitrate > 0) snprintf(text, sizeof(text), "%d kbit/s", diag.bitrate / 1000);
    else snprintf(text, sizeof(text), "--");
    set_label_text_if_changed(s_ecu_bitrate_value, text);
    int channels = 0;
    for (int i = 0; i < ECU_CHANNEL_COUNT; ++i) {
        if (ecu_channel_get(i, NULL)) ++channels;
    }
    snprintf(text, sizeof(text), "%d of %d", channels, ECU_CHANNEL_COUNT);
    set_label_text_if_changed(s_ecu_channels_value, text);
    if (diag.last_frame_age_ms || live) snprintf(text, sizeof(text), "%lu ms ago", (unsigned long)diag.last_frame_age_ms);
    else snprintf(text, sizeof(text), "Never");
    set_label_text_if_changed(s_ecu_last_frame_value, text);
    snprintf(text, sizeof(text), "%lu", (unsigned long)diag.bus_error_count);
    set_label_text_if_changed(s_ecu_errors_value, text);
    ui_set_text_color(s_ecu_errors_value, diag.bus_error_count ? V2_AMBER : V2_GREEN, LV_PART_MAIN);
    snprintf(text, sizeof(text), "%lu", (unsigned long)diag.receive_missed_count);
    set_label_text_if_changed(s_ecu_missed_value, text);
}

/* Builds the ECU Connection page body into `page` (a column flex container). */
static void shell_build_ecu_page(lv_obj_t *page)
{
    lv_obj_set_style_pad_row(page, 12, LV_PART_MAIN);

    lv_obj_t *status = ecu_card(page);
    lv_obj_set_style_pad_all(status, 14, LV_PART_MAIN);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_t *row = ecu_stat(status, "STATUS", NULL);
    s_ecu_status_dot = v2_box(row, 0, 0, 10, 10, V2_DIM, 5);
    s_ecu_status_value = v2_label(row, "--", &v2_font_label16, V2_TEXT);
    s_ecu_protocol_value = v2_label(ecu_stat(status, "PROTOCOL", NULL), "--", &v2_font_label16, V2_TEXT);
    s_ecu_bitrate_value = v2_label(ecu_stat(status, "BITRATE", NULL), "--", &v2_font_label16, V2_TEXT);
    s_ecu_channels_value = v2_label(ecu_stat(status, "CHANNELS LIVE", NULL), "--", &v2_font_label16, V2_TEXT);

    lv_obj_t *heading = v2_label(page, "ECU PROTOCOL", &v2_font_label13, V2_LABEL);
    lv_obj_set_style_text_letter_space(heading, 2, LV_PART_MAIN);

    lv_obj_t *grid = make_plain_container(page);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 10, LV_PART_MAIN);
    for (size_t index = 0; index < CFG_PROTOCOL_COUNT; ++index) {
        lv_obj_t *tile = v2_box(grid, 0, 0, 170, 52, V2_PANEL, 4);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        ui_set_border_color(tile, V2_PANEL_LINE, LV_PART_MAIN);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(tile, cfg_protocol_tile_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        add_press_feedback(tile);
        lv_obj_t *name = v2_label(tile, CFG_PROTOCOL_LABELS[index], &v2_font_label16, lv_color_hex(0xc9ccd1));
        lv_obj_center(name);
        s_cfg_protocol_tiles[index] = tile;
    }
    s_cfg_protocol_status_label = v2_label(page, "", &v2_font_label13, V2_UNIT);
    s_ecu_restart_note = v2_label(page, "Restart the dash to switch to this ECU protocol.",
                                  &v2_font_label13, V2_AMBER);
    lv_obj_add_flag(s_ecu_restart_note, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *diag = ecu_card(page);
    lv_obj_set_flex_flow(diag, LV_FLEX_FLOW_COLUMN);
    s_ecu_last_frame_value = ecu_diag_row(diag, "Last frame", true);
    s_ecu_errors_value = ecu_diag_row(diag, "Bus errors", true);
    s_ecu_missed_value = ecu_diag_row(diag, "Frames missed", false);

    cfg_protocol_tiles_refresh();
    lv_timer_create(ecu_page_refresh_timer_cb, 500, NULL);
}

/* ====================== Session Peaks grid ================================= */

static lv_obj_t *s_peak_tiles[PEAK_VALUE_COUNT];
static lv_obj_t *s_peak_units[PEAK_VALUE_COUNT];
static int s_peak_highlight = -1;

/* 3 x 4 tile grid (10 peaks) sized to fit the settings content area without scrolling */
static void shell_build_peaks_grid(lv_obj_t *grid)
{
    static const char *const captions[PEAK_VALUE_COUNT] = {
        "MAX RPM", "MAX SPEED", "MAX BOOST / MAP", "MAX COOLANT", "MAX INTAKE TEMP",
        "MAX INJECTOR DUTY", "MAX KNOCK", "MIN AFR", "MIN OIL PRESSURE", "MIN BATTERY",
    };
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(grid, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 10, LV_PART_MAIN);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < PEAK_VALUE_COUNT; ++i) {
        lv_obj_t *tile = v2_box(grid, 0, 0, 236, 76, V2_PANEL, 4);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        ui_set_border_color(tile, V2_PANEL_LINE, LV_PART_MAIN);
        lv_obj_t *caption = v2_label(tile, captions[i], &v2_font_label13, V2_LABEL);
        lv_obj_set_style_text_letter_space(caption, 1, LV_PART_MAIN);
        lv_obj_set_pos(caption, 13, 7);
        s_peak_values[i] = v2_label(tile, "--", &v2_font_value34, V2_DIM);
        lv_obj_set_pos(s_peak_values[i], 12, 25);
        s_peak_units[i] = v2_label(tile, "", &v2_font_label13, V2_UNIT);
        lv_obj_align(s_peak_units[i], LV_ALIGN_BOTTOM_RIGHT, -12, -9);
        s_peak_tiles[i] = tile;
    }
}

static void peaks_set(int index, bool valid, const char *format, double value, const char *unit)
{
    if (index < 0 || index >= PEAK_VALUE_COUNT || !s_peak_values[index]) return;
    char text[24];
    if (valid) snprintf(text, sizeof(text), format, value);
    else snprintf(text, sizeof(text), "--");
    set_label_text_if_changed(s_peak_values[index], text);
    ui_set_text_color(s_peak_values[index], valid ? V2_TEXT : V2_DIM, LV_PART_MAIN);
    set_label_text_if_changed(s_peak_units[index], valid ? unit : "");
}

/* Knob selection: outline the selected tile (-1 clears it). */
static void peaks_highlight(int index)
{
    if (index == s_peak_highlight) return;
    if (s_peak_highlight >= 0 && s_peak_tiles[s_peak_highlight]) {
        lv_obj_set_style_border_width(s_peak_tiles[s_peak_highlight], 1, LV_PART_MAIN);
        ui_set_border_color(s_peak_tiles[s_peak_highlight], V2_PANEL_LINE, LV_PART_MAIN);
    }
    s_peak_highlight = (index >= 0 && index < PEAK_VALUE_COUNT) ? index : -1;
    if (s_peak_highlight >= 0 && s_peak_tiles[s_peak_highlight]) {
        lv_obj_set_style_border_width(s_peak_tiles[s_peak_highlight], 2, LV_PART_MAIN);
        ui_set_border_color(s_peak_tiles[s_peak_highlight], V2_RED, LV_PART_MAIN);
    }
}
