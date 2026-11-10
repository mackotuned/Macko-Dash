/*
 * Settings > System > Wi-Fi and Firmware Update pages.
 *
 * #included by honda_dash_ui.c after ui_mackotuned.inc.c (shares the V2
 * fonts/colours and the settings shell helpers). All radio work happens in
 * wifi_update.c; these pages only post requests and poll its status. The
 * radio is switched on when either page opens and off when you leave them.
 */

#include "wifi_update.h"
#include "rotary_controller_uart.h"
#include "gps.h"

#define FW_POLL_MS 250

static lv_timer_t *s_fw_timer;
static bool s_fw_auto_check;
static bool s_fw_ota_mode;

/* Wi-Fi page */
static lv_obj_t *s_wifi_dot;
static lv_obj_t *s_wifi_title;
static lv_obj_t *s_wifi_detail;
static lv_obj_t *s_wifi_forget_btn;
static lv_obj_t *s_wifi_searching;
static lv_obj_t *s_wifi_list;
static wu_ap_t s_wifi_rows[WU_MAX_APS];
static uint32_t s_wifi_list_gen = UINT32_MAX;
static char s_wifi_list_key[48];

/* password entry */
static lv_obj_t *s_wifi_pw_modal;
static lv_obj_t *s_wifi_pw_ta;
static lv_obj_t *s_wifi_pw_show_label;
static char s_wifi_pw_ssid[33];

/* Firmware Update page */
static lv_obj_t *s_fw_latest;
static lv_obj_t *s_fw_status;
static lv_obj_t *s_fw_bar;
static lv_obj_t *s_fw_btn;
static lv_obj_t *s_fw_btn_label;
static lv_obj_t *s_fw_wifi_line;

static void fw_refresh(void);

/* ------------------------------ helpers ----------------------------- */

static lv_obj_t *fw_card(lv_obj_t *parent, lv_coord_t h)
{
    lv_obj_t *card = v2_box(parent, 0, 0, LV_PCT(100), h, V2_PANEL, 6);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    ui_set_border_color(card, V2_PANEL_LINE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);
    return card;
}

static lv_obj_t *fw_caption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = v2_label(parent, text, &v2_font_label13, V2_UNIT);
    lv_obj_set_style_text_letter_space(label, 2, LV_PART_MAIN);
    return label;
}

static lv_obj_t *fw_button(lv_obj_t *parent, const char *text, bool primary, lv_coord_t w, lv_coord_t h,
                           lv_event_cb_t cb, void *user_data, lv_obj_t **label_out)
{
    lv_obj_t *button = v2_box(parent, 0, 0, w, h, primary ? V2_RED : V2_CHIP_OFF, 4);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    ui_set_border_color(button, primary ? V2_RED : V2_PILL_LINE, LV_PART_MAIN);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(button, 6);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    add_press_feedback(button);
    lv_obj_t *label = v2_label(button, text, &v2_font_label16, lv_color_white());
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    lv_obj_center(label);
    if (label_out) *label_out = label;
    return button;
}

static void fw_set_enabled(lv_obj_t *button, bool enabled)
{
    if (enabled) {
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    } else {
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(button, LV_OPA_40, LV_PART_MAIN);
    }
}

/* four signal bars, drawn with boxes so no special font glyphs are needed */
static void fw_signal_bars(lv_obj_t *parent, int rssi)
{
    int level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    lv_obj_t *holder = make_plain_container(parent);
    lv_obj_set_size(holder, 30, 22);
    for (int i = 0; i < 4; ++i) {
        lv_coord_t h = 7 + i * 5;
        v2_box(holder, i * 8, 22 - h, 5, h, i < level ? V2_TEXT : V2_TRACK, 1);
    }
}

/* ------------------------------ password ---------------------------- */

static void wifi_pw_close(void)
{
    if (!s_wifi_pw_modal) return;
    lv_obj_del(s_wifi_pw_modal);
    s_wifi_pw_modal = NULL;
    s_wifi_pw_ta = NULL;
    s_wifi_pw_show_label = NULL;
}

static void wifi_pw_connect(void)
{
    if (!s_wifi_pw_ta) return;
    const char *pass = lv_textarea_get_text(s_wifi_pw_ta);
    wifi_update_connect(s_wifi_pw_ssid, pass);
    wifi_pw_close();
}

static void wifi_pw_connect_cb(lv_event_t *e) { (void)e; wifi_pw_connect(); }
static void wifi_pw_cancel_cb(lv_event_t *e) { (void)e; wifi_pw_close(); }

static void wifi_pw_keyboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) wifi_pw_connect();
    else if (code == LV_EVENT_CANCEL) wifi_pw_close();
}

static void wifi_pw_show_cb(lv_event_t *e)
{
    (void)e;
    if (!s_wifi_pw_ta) return;
    bool hidden = lv_textarea_get_password_mode(s_wifi_pw_ta);
    lv_textarea_set_password_mode(s_wifi_pw_ta, !hidden);
    lv_label_set_text(s_wifi_pw_show_label, hidden ? "HIDE" : "SHOW");
}

static void wifi_pw_open(const char *ssid)
{
    wifi_pw_close();
    snprintf(s_wifi_pw_ssid, sizeof(s_wifi_pw_ssid), "%s", ssid);

    lv_obj_t *modal = v2_box(s_settings_overlay, 0, 0, SCR_W, SCR_H, V2_BG, 0);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_CLICKABLE);
    s_wifi_pw_modal = modal;

    lv_obj_t *head = make_plain_container(modal);
    lv_obj_set_size(head, 700, 60);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_t *cap = fw_caption(head, "WI-FI PASSWORD FOR");
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *name = v2_label(head, ssid, &lv_font_montserrat_24, V2_TEXT);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 700);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t *row = make_plain_container(modal);
    lv_obj_set_size(row, 700, 64);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 124);
    s_wifi_pw_ta = lv_textarea_create(row);
    lv_obj_set_size(s_wifi_pw_ta, 560, 64);
    lv_obj_align(s_wifi_pw_ta, LV_ALIGN_LEFT_MID, 0, 0);
    lv_textarea_set_one_line(s_wifi_pw_ta, true);
    lv_textarea_set_password_mode(s_wifi_pw_ta, true);
    lv_textarea_set_max_length(s_wifi_pw_ta, 64);
    lv_textarea_set_placeholder_text(s_wifi_pw_ta, "Password");
    lv_obj_set_style_text_font(s_wifi_pw_ta, &lv_font_montserrat_28, LV_PART_MAIN);
    ui_set_bg_color(s_wifi_pw_ta, V2_PANEL, LV_PART_MAIN);
    ui_set_text_color(s_wifi_pw_ta, V2_TEXT, LV_PART_MAIN);
    ui_set_border_color(s_wifi_pw_ta, V2_RED, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_wifi_pw_ta, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_wifi_pw_ta, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_wifi_pw_ta, 14, LV_PART_MAIN);
    lv_obj_t *show = fw_button(row, "SHOW", false, 124, 64, wifi_pw_show_cb, NULL, &s_wifi_pw_show_label);
    lv_obj_align(show, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *buttons = make_plain_container(modal);
    lv_obj_set_size(buttons, 700, 54);
    lv_obj_align(buttons, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_t *cancel = fw_button(buttons, "CANCEL", false, 200, 54, wifi_pw_cancel_cb, NULL, NULL);
    lv_obj_align(cancel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *connect = fw_button(buttons, "CONNECT", true, 280, 54, wifi_pw_connect_cb, NULL, NULL);
    lv_obj_align(connect, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *kb = lv_keyboard_create(modal);
    lv_obj_set_size(kb, 1000, 318);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_keyboard_set_textarea(kb, s_wifi_pw_ta);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_24, LV_PART_ITEMS);
    ui_set_bg_color(kb, V2_STRIP, LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
    ui_set_bg_color(kb, lv_color_hex(0x24262c), LV_PART_ITEMS);
    ui_set_text_color(kb, V2_TEXT, LV_PART_ITEMS);
    ui_set_bg_color(kb, lv_color_hex(0x3a0d16), LV_PART_ITEMS | LV_STATE_CHECKED);
    ui_set_bg_color(kb, V2_RED, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(kb, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, wifi_pw_keyboard_cb, LV_EVENT_ALL, NULL);
}

/* ------------------------------ Wi-Fi page -------------------------- */

static void wifi_scan_cb(lv_event_t *e) { (void)e; wifi_update_scan(); }
static void wifi_forget_cb(lv_event_t *e) { (void)e; wifi_update_forget(); s_wifi_list_key[0] = '\0'; }

static void wifi_row_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= WU_MAX_APS) return;
    wu_status_t st;
    wifi_update_get_status(&st);
    if (st.wifi == WU_WIFI_CONNECTED && strcmp(st.ssid, s_wifi_rows[index].ssid) == 0) return;
    if (s_wifi_rows[index].secure) wifi_pw_open(s_wifi_rows[index].ssid);
    else wifi_update_connect(s_wifi_rows[index].ssid, "");
}

static void wifi_rebuild_list(const wu_status_t *st)
{
    lv_obj_clean(s_wifi_list);
    int n = wifi_update_get_aps(s_wifi_rows, WU_MAX_APS);
    if (n == 0) {
        lv_obj_t *empty = v2_label(s_wifi_list, st->scanning ? "Searching for networks..." : "No networks found. Tap SCAN.",
                                   &v2_font_label16, V2_DIM);
        lv_obj_set_style_pad_top(empty, 12, LV_PART_MAIN);
        return;
    }
    for (int i = 0; i < n; ++i) {
        const wu_ap_t *ap = &s_wifi_rows[i];
        bool current = strcmp(st->ssid, ap->ssid) == 0 &&
                       (st->wifi == WU_WIFI_CONNECTED || st->wifi == WU_WIFI_CONNECTING);
        lv_obj_t *row = v2_box(s_wifi_list, 0, 0, LV_PCT(100), 58, current ? V2_CHIP_ON : V2_PANEL, 4);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        ui_set_border_color(row, current ? V2_RED : V2_PANEL_LINE, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, 16, LV_PART_MAIN);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, wifi_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        add_press_feedback(row);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, LV_PART_MAIN);

        lv_obj_t *name = v2_label(row, ap->ssid, &v2_font_label16, V2_TEXT);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);
        if (current) {
            lv_obj_t *tag = v2_label(row, st->wifi == WU_WIFI_CONNECTED ? "CONNECTED" : "CONNECTING",
                                     &v2_font_label13, st->wifi == WU_WIFI_CONNECTED ? V2_GREEN : V2_AMBER);
            lv_obj_set_style_text_letter_space(tag, 1, LV_PART_MAIN);
        }
        fw_caption(row, ap->secure ? "SECURED" : "OPEN");
        fw_signal_bars(row, ap->rssi);
    }
}

static void wifi_refresh(const wu_status_t *st)
{
    char title[64];
    lv_color_t dot = V2_DIM;
    char saved[33];
    bool has_saved = wifi_update_saved_ssid(saved, sizeof(saved));
    switch (st->wifi) {
    case WU_WIFI_CONNECTED:
        snprintf(title, sizeof(title), "Connected to %s", st->ssid);
        dot = V2_GREEN;
        break;
    case WU_WIFI_CONNECTING:
        snprintf(title, sizeof(title), "Connecting to %s", st->ssid);
        dot = V2_AMBER;
        break;
    case WU_WIFI_FAILED:
        snprintf(title, sizeof(title), "Could not join %s", st->ssid);
        dot = V2_RED;
        break;
    case WU_WIFI_ERROR:
        snprintf(title, sizeof(title), "Wi-Fi is not working");
        dot = V2_RED;
        break;
    case WU_WIFI_STARTING:
    case WU_WIFI_OFF:
        snprintf(title, sizeof(title), "Starting Wi-Fi");
        break;
    default:
        snprintf(title, sizeof(title), "Not connected");
        break;
    }
    set_label_text_if_changed(s_wifi_title, title);
    set_label_text_if_changed(s_wifi_detail, st->wifi == WU_WIFI_IDLE
                                                 ? "Choose your network below."
                                                 : st->wifi_detail);
    ui_set_bg_color(s_wifi_dot, dot, LV_PART_MAIN);
    if (has_saved) lv_obj_clear_flag(s_wifi_forget_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_wifi_forget_btn, LV_OBJ_FLAG_HIDDEN);
    if (st->scanning) lv_obj_clear_flag(s_wifi_searching, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_wifi_searching, LV_OBJ_FLAG_HIDDEN);

    char key[48];
    snprintf(key, sizeof(key), "%d|%d|%s", (int)st->wifi, (int)st->scanning, st->ssid);
    if (st->scan_gen != s_wifi_list_gen || strcmp(key, s_wifi_list_key) != 0) {
        s_wifi_list_gen = st->scan_gen;
        snprintf(s_wifi_list_key, sizeof(s_wifi_list_key), "%s", key);
        wifi_rebuild_list(st);
    }
}

static void fw_build_wifi_page(lv_obj_t *panel)
{
    lv_obj_t *page = make_plain_container(panel);
    s_page_wifi = page;
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 10, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = fw_card(page, 84);
    s_wifi_dot = v2_box(card, 0, 8, 12, 12, V2_DIM, 6);
    s_wifi_title = v2_label(card, "Starting Wi-Fi", &v2_font_label16, V2_TEXT);
    lv_obj_set_pos(s_wifi_title, 24, 2);
    s_wifi_detail = v2_label(card, "", &v2_font_label13, V2_LABEL);
    lv_obj_set_pos(s_wifi_detail, 24, 28);
    lv_obj_t *scan = fw_button(card, "SCAN", false, 104, 46, wifi_scan_cb, NULL, NULL);
    lv_obj_align(scan, LV_ALIGN_RIGHT_MID, 0, 0);
    s_wifi_forget_btn = fw_button(card, "FORGET", false, 116, 46, wifi_forget_cb, NULL, NULL);
    lv_obj_align(s_wifi_forget_btn, LV_ALIGN_RIGHT_MID, -114, 0);

    lv_obj_t *head = make_plain_container(page);
    lv_obj_set_size(head, LV_PCT(100), 20);
    lv_obj_t *cap = fw_caption(head, "NETWORKS NEARBY");
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 0, 0);
    s_wifi_searching = fw_caption(head, "SEARCHING...");
    ui_set_text_color(s_wifi_searching, V2_AMBER, LV_PART_MAIN);
    lv_obj_align(s_wifi_searching, LV_ALIGN_RIGHT_MID, 0, 0);

    s_wifi_list = make_plain_container(page);
    lv_obj_set_width(s_wifi_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_wifi_list, 1);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_wifi_list, 6, LV_PART_MAIN);
    lv_obj_add_flag(s_wifi_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *note = v2_label(page, "Wi-Fi is only on while this page or Firmware Update is open.",
                              &v2_font_label13, V2_DIM);
    lv_obj_set_width(note, LV_PCT(100));
}

/* ------------------------------ update page ------------------------- */

static void fw_open_wifi_cb(lv_event_t *e) { (void)e; settings_show_page(s_page_wifi); }

static void fw_main_btn_cb(lv_event_t *e)
{
    (void)e;
    wu_status_t st;
    wifi_update_get_status(&st);
    if (st.upd == WU_UPD_AVAILABLE) {
        /* pause gauges, CAN and logging while the new firmware is written */
        s_fw_ota_mode = true;
        dashboard_runtime_set_ota_mode(true);
        wifi_update_install();
    } else if (st.wifi == WU_WIFI_CONNECTED) {
        wifi_update_check();
    } else {
        settings_show_page(s_page_wifi);
    }
}

static void fw_update_refresh(const wu_status_t *st)
{
    const char *status = st->upd_detail;
    char button[40] = "CHECK FOR UPDATE";
    bool busy = st->upd == WU_UPD_CHECKING || st->upd == WU_UPD_DOWNLOADING || st->upd == WU_UPD_REBOOTING;

    if (st->upd == WU_UPD_IDLE || (st->upd == WU_UPD_ERROR && st->wifi != WU_WIFI_CONNECTED)) {
        char saved[33];
        if (st->wifi == WU_WIFI_CONNECTED) status = "Ready to check for updates.";
        else if (st->wifi == WU_WIFI_CONNECTING || st->wifi == WU_WIFI_STARTING) status = "Connecting to Wi-Fi...";
        else if (!wifi_update_saved_ssid(saved, sizeof(saved))) status = "Set up Wi-Fi to check for updates.";
        else status = "Wi-Fi is not connected.";
    }
    if (st->upd == WU_UPD_AVAILABLE) snprintf(button, sizeof(button), "INSTALL %s", st->latest);
    else if (st->upd == WU_UPD_CHECKING) snprintf(button, sizeof(button), "CHECKING...");
    else if (st->upd == WU_UPD_DOWNLOADING) snprintf(button, sizeof(button), "INSTALLING %d%%", st->progress);
    else if (st->upd == WU_UPD_REBOOTING) snprintf(button, sizeof(button), "RESTARTING...");
    else if (st->wifi != WU_WIFI_CONNECTED) snprintf(button, sizeof(button), "SET UP WI-FI");

    set_label_text_if_changed(s_fw_status, status);
    set_label_text_if_changed(s_fw_btn_label, button);
    fw_set_enabled(s_fw_btn, !busy && !(st->wifi == WU_WIFI_CONNECTING || st->wifi == WU_WIFI_STARTING));

    set_label_text_if_changed(s_fw_latest, st->latest[0] ? st->latest : "--");
    ui_set_text_color(s_fw_latest, st->upd == WU_UPD_AVAILABLE || st->upd == WU_UPD_USB_ONLY ? V2_GREEN : V2_TEXT,
                      LV_PART_MAIN);

    if (st->upd == WU_UPD_DOWNLOADING || st->upd == WU_UPD_REBOOTING) {
        lv_obj_clear_flag(s_fw_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_fw_bar, st->progress, LV_ANIM_OFF);
    } else {
        lv_obj_add_flag(s_fw_bar, LV_OBJ_FLAG_HIDDEN);
    }

    char wifi_line[72];
    if (st->wifi == WU_WIFI_CONNECTED) snprintf(wifi_line, sizeof(wifi_line), "Wi-Fi: %s", st->ssid);
    else if (st->wifi == WU_WIFI_CONNECTING) snprintf(wifi_line, sizeof(wifi_line), "Wi-Fi: connecting to %s", st->ssid);
    else snprintf(wifi_line, sizeof(wifi_line), "Wi-Fi: not connected");
    set_label_text_if_changed(s_fw_wifi_line, wifi_line);

    if (s_fw_auto_check && st->wifi == WU_WIFI_CONNECTED && !busy) {
        s_fw_auto_check = false;
        wifi_update_check();
    }
}

static void fw_build_update_page(lv_obj_t *panel)
{
    lv_obj_t *page = make_plain_container(panel);
    s_page_update = page;
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 12, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *versions = make_plain_container(page);
    lv_obj_set_size(versions, LV_PCT(100), 104);
    lv_obj_set_flex_flow(versions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(versions, 12, LV_PART_MAIN);
    const esp_app_desc_t *app = esp_app_get_description();
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *card = fw_card(versions, 104);
        lv_obj_set_width(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(card, 1);
        fw_caption(card, i == 0 ? "INSTALLED" : "LATEST AVAILABLE");
        lv_obj_t *value = v2_label(card, i == 0 ? app->version : "--", &v2_font_value44, V2_TEXT);
        lv_obj_set_pos(value, 0, 22);
        if (i == 1) s_fw_latest = value;
    }

    lv_obj_t *card = fw_card(page, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, LV_PART_MAIN);
    s_fw_status = v2_label(card, "", &v2_font_label16, V2_TEXT);
    lv_obj_set_width(s_fw_status, LV_PCT(100));
    lv_label_set_long_mode(s_fw_status, LV_LABEL_LONG_WRAP);
    s_fw_bar = lv_bar_create(card);
    lv_obj_set_size(s_fw_bar, LV_PCT(100), 12);
    lv_bar_set_range(s_fw_bar, 0, 100);
    ui_set_bg_color(s_fw_bar, V2_TRACK, LV_PART_MAIN);
    ui_set_bg_color(s_fw_bar, V2_RED, LV_PART_INDICATOR);
    lv_obj_add_flag(s_fw_bar, LV_OBJ_FLAG_HIDDEN);
    s_fw_btn = fw_button(card, "CHECK FOR UPDATE", true, LV_PCT(100), 62, fw_main_btn_cb, NULL, &s_fw_btn_label);

    lv_obj_t *wifi_row = make_plain_container(page);
    lv_obj_set_size(wifi_row, LV_PCT(100), 46);
    s_fw_wifi_line = v2_label(wifi_row, "Wi-Fi: not connected", &v2_font_label16, V2_LABEL);
    lv_obj_align(s_fw_wifi_line, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *wifi_btn = fw_button(wifi_row, "WI-FI SETTINGS", false, 190, 46, fw_open_wifi_cb, NULL, NULL);
    lv_obj_align(wifi_btn, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *note = v2_label(page,
                              "Keep the dash powered (ignition on) until it restarts. Settings, odometer and "
                              "themes are kept. If the new version does not start properly, the dash goes "
                              "back to this version by itself.",
                              &v2_font_label13, V2_DIM);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
}

/* ------------------------------ knob page -------------------------- */

static lv_obj_t *s_knob_switch;
static lv_obj_t *s_knob_dot;
static lv_obj_t *s_knob_title;
static lv_obj_t *s_knob_detail;
static lv_obj_t *s_knob_upd_status;
static lv_obj_t *s_knob_upd_bar;
static lv_obj_t *s_knob_upd_btn;
static lv_obj_t *s_knob_upd_label;
static bool s_knob_auto_check;

static void knob_update_btn_cb(lv_event_t *e)
{
    (void)e;
    wu_status_t st;
    wifi_update_get_status(&st);
    if (st.knob_upd == WU_UPD_AVAILABLE) {
        wifi_update_knob_install();
    } else {
        /* the check needs home Wi-Fi: connect, then check once connected */
        s_knob_auto_check = true;
        wifi_update_radio_on();
    }
}

static void knob_update_refresh(const wu_status_t *st, bool knob_connected)
{
    bool busy = st->knob_upd == WU_UPD_CHECKING || st->knob_upd == WU_UPD_DOWNLOADING;
    const char *status = st->knob_upd_detail;
    char button[40] = "CHECK FOR KNOB UPDATE";
    if (st->knob_upd == WU_UPD_IDLE) status = "Uses the dash's home Wi-Fi to look for a newer knob version.";
    if (s_knob_auto_check && !busy) {
        if (st->wifi == WU_WIFI_CONNECTED) {
            s_knob_auto_check = false;
            wifi_update_knob_check();
        } else if (st->wifi == WU_WIFI_FAILED || st->wifi == WU_WIFI_ERROR ||
                   (st->wifi == WU_WIFI_IDLE && !st->ssid[0])) {
            s_knob_auto_check = false;
        } else {
            status = "Connecting to Wi-Fi...";
            busy = true;
        }
    }
    if (st->knob_upd == WU_UPD_AVAILABLE) snprintf(button, sizeof(button), "UPDATE KNOB TO %s", st->knob_latest);
    else if (st->knob_upd == WU_UPD_CHECKING) snprintf(button, sizeof(button), "CHECKING...");
    else if (st->knob_upd == WU_UPD_DOWNLOADING) snprintf(button, sizeof(button), "UPDATING %d%%", st->knob_progress);
    else if (busy) snprintf(button, sizeof(button), "CONNECTING...");
    set_label_text_if_changed(s_knob_upd_status, status);
    set_label_text_if_changed(s_knob_upd_label, button);
    fw_set_enabled(s_knob_upd_btn, !busy && (knob_connected || st->knob_upd == WU_UPD_AVAILABLE));
    if (st->knob_upd == WU_UPD_DOWNLOADING) {
        lv_obj_clear_flag(s_knob_upd_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_knob_upd_bar, st->knob_progress, LV_ANIM_OFF);
    } else {
        lv_obj_add_flag(s_knob_upd_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

static void knob_switch_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    dash_config_set_wireless_knob(on);
    wifi_update_knob_network(on);
}

static void knob_refresh(const wu_status_t *st)
{
    char version[24] = "";
    bool connected = rotary_controller_wireless_status(version, sizeof(version));
    bool enabled = dash_config_get_wireless_knob();
    char title[48], detail[96];
    lv_color_t dot = V2_DIM;
    if (!enabled) {
        snprintf(title, sizeof(title), "Wireless knob is off");
        snprintf(detail, sizeof(detail), "Turn it on to use a knob without a data cable.");
    } else if (connected) {
        snprintf(title, sizeof(title), "Knob connected");
        int n = snprintf(detail, sizeof(detail), "Knob firmware %s", version[0] ? version : "1.1 or older");
        if (st->knob_rssi) snprintf(detail + n, sizeof(detail) - n, "  |  signal %d dBm", st->knob_rssi);
        dot = V2_GREEN;
    } else if (st->knob_ap) {
        snprintf(title, sizeof(title), "Waiting for the knob");
        snprintf(detail, sizeof(detail), "Dash network %s is on. Power the knob and it joins by itself.",
                 st->knob_ssid);
        dot = V2_AMBER;
    } else {
        snprintf(title, sizeof(title), "Starting knob network...");
        detail[0] = '\0';
        dot = V2_AMBER;
    }
    set_label_text_if_changed(s_knob_title, title);
    set_label_text_if_changed(s_knob_detail, detail);
    ui_set_bg_color(s_knob_dot, dot, LV_PART_MAIN);
    knob_update_refresh(st, connected);
    if (enabled != lv_obj_has_state(s_knob_switch, LV_STATE_CHECKED)) {
        if (enabled) lv_obj_add_state(s_knob_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_knob_switch, LV_STATE_CHECKED);
    }
}

static void fw_build_knob_page(lv_obj_t *panel)
{
    lv_obj_t *page = make_plain_container(panel);
    s_page_knob = page;
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 12, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = fw_card(page, 92);
    lv_obj_t *name = v2_label(card, "Wireless knob", &v2_font_label16, V2_TEXT);
    lv_obj_set_pos(name, 0, 4);
    lv_obj_t *hint = v2_label(card, "Connects the MackoDash knob over Wi-Fi. Only power is wired.",
                              &v2_font_label13, V2_LABEL);
    lv_obj_set_pos(hint, 0, 32);
    s_knob_switch = lv_switch_create(card);
    lv_obj_set_size(s_knob_switch, 72, 38);
    lv_obj_align(s_knob_switch, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_set_bg_color(s_knob_switch, V2_RED, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (dash_config_get_wireless_knob()) lv_obj_add_state(s_knob_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_knob_switch, knob_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *status = fw_card(page, 92);
    s_knob_dot = v2_box(status, 0, 8, 12, 12, V2_DIM, 6);
    s_knob_title = v2_label(status, "", &v2_font_label16, V2_TEXT);
    lv_obj_set_pos(s_knob_title, 24, 2);
    s_knob_detail = v2_label(status, "", &v2_font_label13, V2_LABEL);
    lv_obj_set_pos(s_knob_detail, 24, 30);
    lv_obj_set_width(s_knob_detail, LV_PCT(95));
    lv_label_set_long_mode(s_knob_detail, LV_LABEL_LONG_WRAP);

    lv_obj_t *upd = fw_card(page, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(upd, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(upd, 10, LV_PART_MAIN);
    s_knob_upd_status = v2_label(upd, "", &v2_font_label13, V2_LABEL);
    lv_obj_set_width(s_knob_upd_status, LV_PCT(100));
    lv_label_set_long_mode(s_knob_upd_status, LV_LABEL_LONG_WRAP);
    s_knob_upd_bar = lv_bar_create(upd);
    lv_obj_set_size(s_knob_upd_bar, LV_PCT(100), 10);
    lv_bar_set_range(s_knob_upd_bar, 0, 100);
    ui_set_bg_color(s_knob_upd_bar, V2_TRACK, LV_PART_MAIN);
    ui_set_bg_color(s_knob_upd_bar, V2_RED, LV_PART_INDICATOR);
    lv_obj_add_flag(s_knob_upd_bar, LV_OBJ_FLAG_HIDDEN);
    s_knob_upd_btn = fw_button(upd, "CHECK FOR KNOB UPDATE", true, LV_PCT(100), 54, knob_update_btn_cb, NULL,
                               &s_knob_upd_label);

    lv_obj_t *note = v2_label(page,
                              "The knob joins the nearest MackoDash by itself. Knobs need firmware 1.2.0 or "
                              "newer for wireless; install it once with the MackoDash USB updater.",
                              &v2_font_label13, V2_DIM);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
}

/* ------------------------------ GPS page --------------------------- */

static lv_obj_t *s_gps_dot;
static lv_obj_t *s_gps_title;
static lv_obj_t *s_gps_detail;
static lv_obj_t *s_gps_src_btn[3];
static lv_obj_t *s_gps_src_hint;
static lv_obj_t *s_gps_bars[5];
static lv_obj_t *s_gps_signal;

static const char *const GPS_SOURCE_NAMES[3] = {"ECU", "AUTO", "GPS ONLY"};
static const char *const GPS_SOURCE_HINTS[3] = {
    "Speed and odometer come from the ECU over CAN.",
    "Uses the ECU's speed when it sends one, otherwise GPS.",
    "Ignores the ECU's speed. Speed and odometer come from GPS.",
};

static void gps_source_style(void)
{
    int selected = (int)dash_config_get_speed_source();
    for (int i = 0; i < 3; ++i) {
        bool on = i == selected;
        ui_set_bg_color(s_gps_src_btn[i], on ? V2_RED : V2_CHIP_OFF, LV_PART_MAIN);
        ui_set_border_color(s_gps_src_btn[i], on ? V2_RED : V2_PILL_LINE, LV_PART_MAIN);
    }
    set_label_text_if_changed(s_gps_src_hint, GPS_SOURCE_HINTS[selected]);
}

static void gps_source_cb(lv_event_t *e)
{
    dash_config_set_speed_source((dash_speed_source_t)(intptr_t)lv_event_get_user_data(e));
    gps_source_style();
}

static void gps_swap_cb(lv_event_t *e)
{
    dash_config_set_port_swapped(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    rotary_controller_port_swap_changed();
}

static void gps_refresh(void)
{
    gps_status_t st;
    gps_get_status(&st);
    rotary_port_mode_t port = rotary_controller_port_mode();

    /* live signal: average strength of the 4 best satellites, in dB-Hz */
    int db = st.signal_db;
    int level = db >= 42 ? 5 : db >= 35 ? 4 : db >= 28 ? 3 : db >= 20 ? 2 : db > 0 ? 1 : 0;
    static const char *const RATING[6] = {"No signal", "Very weak", "Weak", "Fair", "Good", "Excellent"};
    lv_color_t bar = level >= 4 ? V2_GREEN : level == 3 ? V2_AMBER : V2_RED;
    for (int i = 0; i < 5; ++i) ui_set_bg_color(s_gps_bars[i], i < level ? bar : V2_TRACK, LV_PART_MAIN);
    char signal[64];
    if (db > 0) snprintf(signal, sizeof(signal), "%s  |  %d dB  |  %d in view", RATING[level], db, st.sats_in_view);
    else if (st.detected) snprintf(signal, sizeof(signal), "No signal  |  %d in view", st.sats_in_view);
    else snprintf(signal, sizeof(signal), "--");
    set_label_text_if_changed(s_gps_signal, signal);

    char title[48], detail[112];
    lv_color_t dot = V2_DIM;
    if (port == ROTARY_PORT_KNOB) {
        snprintf(title, sizeof(title), "Wired knob on the accessory port");
        snprintf(detail, sizeof(detail), "Unplug the knob cable and connect the GPS module there.");
    } else if (!st.detected) {
        snprintf(title, sizeof(title), "No GPS detected");
        snprintf(detail, sizeof(detail), "Searching the port. Check the wiring, or try Swap TX / RX below.");
    } else if (!st.fix) {
        snprintf(title, sizeof(title), "GPS connected, waiting for satellites");
        snprintf(detail, sizeof(detail), "The first fix can take a few minutes with a clear view of the sky.");
        dot = V2_AMBER;
    } else {
        bool kph = dash_config_get_speed_kph();
        snprintf(title, sizeof(title), "GPS fix: %.0f %s", kph ? st.speed_kph : st.speed_kph * 0.621371f,
                 kph ? "km/h" : "mph");
        snprintf(detail, sizeof(detail), "%d satellites used  |  %.0f updates per second  |  %d baud",
                 st.satellites, st.rate_hz, st.baud);
        dot = V2_GREEN;
    }
    set_label_text_if_changed(s_gps_title, title);
    set_label_text_if_changed(s_gps_detail, detail);
    ui_set_bg_color(s_gps_dot, dot, LV_PART_MAIN);
}

static void fw_build_gps_page(lv_obj_t *panel)
{
    lv_obj_t *page = make_plain_container(panel);
    s_page_gps = page;
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 12, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status = fw_card(page, 124);
    s_gps_dot = v2_box(status, 0, 8, 12, 12, V2_DIM, 6);
    s_gps_title = v2_label(status, "", &v2_font_label16, V2_TEXT);
    lv_obj_set_pos(s_gps_title, 24, 2);
    s_gps_detail = v2_label(status, "", &v2_font_label13, V2_LABEL);
    lv_obj_set_pos(s_gps_detail, 24, 30);
    lv_obj_set_width(s_gps_detail, LV_PCT(95));
    lv_label_set_long_mode(s_gps_detail, LV_LABEL_LONG_WRAP);
    for (int i = 0; i < 5; ++i) {
        lv_coord_t h = 8 + i * 4;
        s_gps_bars[i] = v2_box(status, 24 + i * 10, 88 - h, 7, h, V2_TRACK, 1);
    }
    s_gps_signal = v2_label(status, "--", &v2_font_label13, V2_TEXT);
    lv_obj_set_pos(s_gps_signal, 84, 72);

    lv_obj_t *src = fw_card(page, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(src, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(src, 10, LV_PART_MAIN);
    fw_caption(src, "SPEED SOURCE");
    lv_obj_t *row = make_plain_container(src);
    lv_obj_set_size(row, LV_PCT(100), 54);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);
    for (int i = 0; i < 3; ++i) {
        s_gps_src_btn[i] = fw_button(row, GPS_SOURCE_NAMES[i], false, 100, 54, gps_source_cb,
                                     (void *)(intptr_t)i, NULL);
        lv_obj_set_flex_grow(s_gps_src_btn[i], 1);
    }
    s_gps_src_hint = v2_label(src, "", &v2_font_label13, V2_LABEL);
    lv_obj_set_width(s_gps_src_hint, LV_PCT(100));
    gps_source_style();

    lv_obj_t *swap = fw_card(page, 76);
    lv_obj_t *swap_name = v2_label(swap, "Swap TX / RX", &v2_font_label16, V2_TEXT);
    lv_obj_set_pos(swap_name, 0, 0);
    lv_obj_t *swap_hint = v2_label(swap, "Turn on if the GPS is not detected and the data wires may be crossed.",
                                   &v2_font_label13, V2_LABEL);
    lv_obj_set_pos(swap_hint, 0, 26);
    lv_obj_t *swap_sw = lv_switch_create(swap);
    lv_obj_set_size(swap_sw, 72, 38);
    lv_obj_align(swap_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_set_bg_color(swap_sw, V2_RED, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (dash_config_get_port_swapped()) lv_obj_add_state(swap_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(swap_sw, gps_swap_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *note = v2_label(page,
                              "Wiring (old knob plug): GPS VCC to the plug's power, GND to GND, "
                              "GPS TX to GPIO 50, GPS RX to GPIO 49. Works with u-blox NEO-6M style "
                              "modules; they are set to 5 updates per second automatically.",
                              &v2_font_label13, V2_DIM);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
}

/* ------------------------------ lifecycle --------------------------- */

static void fw_refresh(void)
{
    wu_status_t st;
    wifi_update_get_status(&st);
    /* a failed install hands the gauges, CAN and logging back */
    if (s_fw_ota_mode && st.upd == WU_UPD_ERROR) {
        s_fw_ota_mode = false;
        dashboard_runtime_set_ota_mode(false);
    }
    if (s_page_wifi && !lv_obj_has_flag(s_page_wifi, LV_OBJ_FLAG_HIDDEN)) wifi_refresh(&st);
    if (s_page_update && !lv_obj_has_flag(s_page_update, LV_OBJ_FLAG_HIDDEN)) fw_update_refresh(&st);
    if (s_page_knob && !lv_obj_has_flag(s_page_knob, LV_OBJ_FLAG_HIDDEN)) knob_refresh(&st);
    if (s_page_gps && !lv_obj_has_flag(s_page_gps, LV_OBJ_FLAG_HIDDEN)) gps_refresh();
}

static void fw_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    fw_refresh();
}

/* Called for every settings page change; NULL when settings close. */
static void fw_on_page_shown(lv_obj_t *page)
{
    bool wifi_page = page && page == s_page_wifi;
    bool update_page = page && page == s_page_update;
    if (wifi_page || update_page) {
        wifi_update_radio_on();
        if (wifi_page) {
            wifi_update_scan();
            s_wifi_list_key[0] = '\0';
        }
        if (update_page) s_fw_auto_check = true;
        if (!s_fw_timer) s_fw_timer = lv_timer_create(fw_timer_cb, FW_POLL_MS, NULL);
        fw_refresh();
        return;
    }
    if (page && (page == s_page_knob || page == s_page_gps)) {
        /* the knob network is separate from home Wi-Fi: leave the station off */
        wifi_update_radio_off();
        if (!s_fw_timer) s_fw_timer = lv_timer_create(fw_timer_cb, FW_POLL_MS, NULL);
        fw_refresh();
        return;
    }
    wifi_pw_close();
    s_fw_auto_check = false;
    s_knob_auto_check = false;
    if (s_fw_timer && !s_fw_ota_mode) {   /* keep watching an install that is still running */
        lv_timer_del(s_fw_timer);
        s_fw_timer = NULL;
    }
    wifi_update_radio_off();
}

static void fw_build_pages(lv_obj_t *panel)
{
    fw_build_wifi_page(panel);
    fw_build_update_page(panel);
    fw_build_knob_page(panel);
    fw_build_gps_page(panel);
}
