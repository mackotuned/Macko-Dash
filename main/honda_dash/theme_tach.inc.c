/*
 * MackoDash Tach theme -- round night-face tachometer with four data tiles
 * on each side (yellow fill bars), gear and speed boxes under the hub.
 *
 * #included by honda_dash_ui.c after theme_v2.inc.c (uses its fonts and
 * v2_box / v2_label helpers). The eight tiles share MackoDash V1/V2's gauge
 * slots 0-7, so a long-press change carries across those themes.
 *
 * Speed: the dial (face, band, ticks, numbers) is rendered once into a PSRAM
 * canvas; a needle move only invalidates the needle's old and new bounding
 * boxes, which LVGL repaints with an image blit plus one line.
 */

#define TACH_BG        lv_color_hex(0x0a0a0b)
#define TACH_FACE      lv_color_hex(0x101012)
#define TACH_RIM       lv_color_hex(0x28292c)
#define TACH_YELLOW    lv_color_hex(0xf7d60c)
#define TACH_RED       lv_color_hex(0xe21e24)
#define TACH_ORANGE    lv_color_hex(0xff7a1a)
#define TACH_TILE      lv_color_hex(0x1e1f22)
#define TACH_TILE_LINE lv_color_hex(0x3a3c40)
#define TACH_TRACK     lv_color_hex(0x121214)
#define TACH_TEXT      lv_color_hex(0xf5f5f0)
#define TACH_GREY      lv_color_hex(0x96989c)
#define TACH_DIM       lv_color_hex(0x55575c)

#define TACH_CX 512
#define TACH_CY 300
#define TACH_R 262          /* face radius; rim adds 8 px */
#define TACH_CANVAS 540     /* (TACH_R + 8) * 2 */
#define TACH_A0 96.0f       /* angle of 0 rpm, degrees clockwise from 3 o'clock */
#define TACH_SWEEP 284.0f
#define TACH_TILES 8
#define TACH_TILE_W 226
#define TACH_TILE_H 122
#define TACH_TILE_TOP 66
#define TACH_TILE_GAP 10
#define TACH_BAR_W 22

typedef struct {
    lv_obj_t *box;
    lv_obj_t *fill;
    lv_obj_t *value;
    lv_obj_t *unit;
    lv_obj_t *name;
    char last_text[24];
    lv_coord_t last_fill_h;
    uint32_t last_hex;
} tach_tile_t;

static lv_obj_t *s_tach_canvas;
static lv_color_t *s_tach_canvas_buf;
static int s_tach_scale_max = -1;
static int s_tach_scale_red = -1;
static lv_obj_t *s_tach_needle;
static lv_point_t s_tach_needle_pts[2];   /* absolute screen coords */
static float s_tach_needle_angle = -1000.0f;
static lv_obj_t *s_tach_hub;
static lv_obj_t *s_tach_rpm_val;
static lv_obj_t *s_tach_chip[3];
static lv_obj_t *s_tach_gear_val;
static lv_obj_t *s_tach_speed_val;
static lv_obj_t *s_tach_speed_unit;
static lv_obj_t *s_tach_odo_caption;
static lv_obj_t *s_tach_odo_val;
static long s_tach_last_odo = -1;
static tach_tile_t s_tach_tiles[TACH_TILES];

static float tach_angle(int rpm)
{
    int max = MAXRPM > 0 ? MAXRPM : 9000;
    if (rpm < 0) rpm = 0;
    if (rpm > max) rpm = max;
    return TACH_A0 + TACH_SWEEP * (float)rpm / (float)max;
}

static void tach_polar(float deg, float r, lv_coord_t *x, lv_coord_t *y)
{
    float a = deg * 3.14159265f / 180.0f;
    *x = (lv_coord_t)lroundf(cosf(a) * r);
    *y = (lv_coord_t)lroundf(sinf(a) * r);
}

/* ---- dial ------------------------------------------------------------------ */

static void tach_dial_render(void)
{
    if (!s_tach_canvas) return;
    const int c = TACH_CANVAS / 2;
    int max = MAXRPM > 0 ? MAXRPM : 9000;
    int red = REDLINE > 0 && REDLINE < max ? REDLINE : max;
    s_tach_scale_max = MAXRPM;
    s_tach_scale_red = REDLINE;

    lv_canvas_fill_bg(s_tach_canvas, TACH_BG, LV_OPA_COVER);

    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.radius = LV_RADIUS_CIRCLE;
    disc.bg_color = TACH_RIM;
    lv_canvas_draw_rect(s_tach_canvas, 0, 0, TACH_CANVAS, TACH_CANVAS, &disc);
    disc.bg_color = TACH_FACE;
    lv_canvas_draw_rect(s_tach_canvas, 8, 8, TACH_CANVAS - 16, TACH_CANVAS - 16, &disc);

    /* yellow / red band just inside the rim */
    lv_draw_arc_dsc_t band;
    lv_draw_arc_dsc_init(&band);
    band.width = 22;
    band.color = TACH_YELLOW;
    int a0 = (int)lroundf(tach_angle(0));
    int ar = (int)lroundf(tach_angle(red));
    int am = (int)lroundf(tach_angle(max));
    lv_canvas_draw_arc(s_tach_canvas, c, c, TACH_R - 8, a0, ar, &band);
    band.color = TACH_RED;
    if (am > ar) lv_canvas_draw_arc(s_tach_canvas, c, c, TACH_R - 8, ar, am, &band);

    /* ticks every 250 rpm, numbers every 1000 */
    lv_draw_line_dsc_t tick;
    lv_draw_line_dsc_init(&tick);
    lv_draw_label_dsc_t num;
    lv_draw_label_dsc_init(&num);
    num.font = &v2_font_value44;
    num.align = LV_TEXT_ALIGN_CENTER;
    for (int rpm = 0; rpm <= max; rpm += 250) {
        bool major = rpm % 1000 == 0, half = rpm % 500 == 0;
        float a = tach_angle(rpm);
        float r1 = TACH_R - 36, r0 = r1 - (major ? 30 : (half ? 20 : 12));
        lv_coord_t x0, y0, x1, y1;
        tach_polar(a, r0, &x0, &y0);
        tach_polar(a, r1, &x1, &y1);
        lv_point_t pts[2] = {{c + x0, c + y0}, {c + x1, c + y1}};
        tick.width = major ? 5 : 2;
        tick.color = rpm >= red ? TACH_RED : TACH_TEXT;
        lv_canvas_draw_line(s_tach_canvas, pts, 2, &tick);
        if (major) {
            char text[12];
            snprintf(text, sizeof(text), "%d", rpm / 1000);
            lv_coord_t nx, ny;
            tach_polar(a, TACH_R - 104, &nx, &ny);
            num.color = rpm >= red ? TACH_RED : TACH_TEXT;
            lv_canvas_draw_text(s_tach_canvas, c + nx - 40, c + ny - 27, 80, &num, text);
        }
    }
    lv_obj_invalidate(s_tach_canvas);
}

/* ---- needle ---------------------------------------------------------------- */

static void tach_needle_area(lv_area_t *area)
{
    area->x1 = LV_MIN(s_tach_needle_pts[0].x, s_tach_needle_pts[1].x) - 6;
    area->x2 = LV_MAX(s_tach_needle_pts[0].x, s_tach_needle_pts[1].x) + 6;
    area->y1 = LV_MIN(s_tach_needle_pts[0].y, s_tach_needle_pts[1].y) - 6;
    area->y2 = LV_MAX(s_tach_needle_pts[0].y, s_tach_needle_pts[1].y) + 6;
}

static void tach_needle_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    tach_needle_area(&area);
    if (!_lv_area_is_on(&area, ctx->clip_area)) return;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = TACH_RED;
    dsc.width = 7;
    dsc.round_start = 1;
    dsc.round_end = 1;
    lv_draw_line(ctx, &dsc, &s_tach_needle_pts[0], &s_tach_needle_pts[1]);
}

static void tach_needle_set(int rpm)
{
    float a = tach_angle(rpm);
    if (fabsf(a - s_tach_needle_angle) < 0.35f) return;
    s_tach_needle_angle = a;
    lv_area_t old_area;
    tach_needle_area(&old_area);
    lv_coord_t x0, y0, x1, y1;
    tach_polar(a, 96, &x0, &y0);
    tach_polar(a, TACH_R - 14, &x1, &y1);
    s_tach_needle_pts[0] = (lv_point_t){TACH_CX + x0, TACH_CY + y0};
    s_tach_needle_pts[1] = (lv_point_t){TACH_CX + x1, TACH_CY + y1};
    lv_area_t new_area;
    tach_needle_area(&new_area);
    lv_obj_invalidate_area(s_tach_needle, &old_area);
    lv_obj_invalidate_area(s_tach_needle, &new_area);
}

/* ---- tiles ----------------------------------------------------------------- */

static void tach_refresh_identity(int slot)
{
    if (slot < 0 || slot >= TACH_TILES || !s_tach_tiles[slot].name) return;
    int channel = dash_config_get_modern_field_channel(slot);
    if (channel < 0 || channel >= DASH_CONFIG_FIELD_CHANNEL_COUNT) channel = 0;
    char name[32];
    snprintf(name, sizeof(name), "%s", system_field_name(channel));
    for (char *ch = name; *ch; ++ch) *ch = (char)toupper((unsigned char)*ch);
    lv_label_set_text(s_tach_tiles[slot].name, name);
    lv_label_set_text(s_tach_tiles[slot].unit, v2_unit_text(hal_channel_unit(channel)));
    s_tach_tiles[slot].last_text[0] = '\0';
}

static void tach_refresh_live_unit(int slot, const char *unit)
{
    if (slot < 0 || slot >= TACH_TILES || !s_tach_tiles[slot].unit) return;
    set_label_text_if_changed(s_tach_tiles[slot].unit, v2_unit_text(unit));
}

static void tach_build_tile(lv_obj_t *root, int slot, lv_coord_t x, lv_coord_t y, bool bar_right)
{
    tach_tile_t *t = &s_tach_tiles[slot];
    t->box = v2_box(root, x, y, TACH_TILE_W, TACH_TILE_H, TACH_TILE, 6);
    lv_obj_set_style_border_width(t->box, 1, LV_PART_MAIN);
    ui_set_border_color(t->box, TACH_TILE_LINE, LV_PART_MAIN);
    lv_obj_add_flag(t->box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t->box, system_field_long_press_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)(SYSTEM_FIELD_V2 * 16 + slot));
    add_press_feedback(t->box);

    lv_coord_t inner_w = TACH_TILE_W - 2, inner_h = TACH_TILE_H - 2;
    lv_coord_t bar_x = bar_right ? inner_w - TACH_BAR_W - 8 : 8;
    lv_obj_t *track = v2_box(t->box, bar_x, 8, TACH_BAR_W, inner_h - 16, TACH_TRACK, 3);
    t->fill = v2_box(track, 0, inner_h - 16, TACH_BAR_W, 0, TACH_YELLOW, 3);

    lv_coord_t text_x = bar_right ? 14 : TACH_BAR_W + 22;
    lv_obj_t *row = make_plain_container(t->box);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_set_pos(row, text_x, 14);
    t->value = v2_label(row, "--", &v2_font_value44, TACH_TEXT);
    t->unit = v2_label(row, "", &v2_font_label16, TACH_GREY);
    lv_obj_set_style_pad_bottom(t->unit, 9, LV_PART_MAIN);
    t->name = v2_label(t->box, "", &v2_font_label13, TACH_GREY);
    lv_obj_set_style_text_letter_space(t->name, 1, LV_PART_MAIN);
    lv_obj_set_pos(t->name, text_x, inner_h - 30);
    t->last_fill_h = -1;
    t->last_hex = 0;
    tach_refresh_identity(slot);
}

static void tach_tile_set(int slot, int channel, float value, bool ok)
{
    tach_tile_t *t = &s_tach_tiles[slot];
    if (!t->box) return;
    char text[24];
    system_field_format_text(channel, value, ok, text, sizeof(text));
    lv_color_t base = system_field_value_color(channel, value, ok);
    bool critical = ok && base.full == C_RED.full;
    bool warn = ok && base.full == C_AMBER.full;

    float fraction = 0.0f;
    if (ok) {
        if (system_field_is_ecu(channel)) {
            fraction = ecu_channel_fraction(channel - DASH_CONFIG_HAL_CHANNEL_COUNT, value);
        } else if (channel < TILE_COUNT) {
            const tile_def_t *def = &TILE_DEFS[channel];
            fraction = (value - def->min) / (def->max - def->min);
        } else {
            fraction = value / 100.0f;
            critical = critical || value <= 10.0f;
        }
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
    }

    if (strcmp(text, t->last_text) != 0) {
        snprintf(t->last_text, sizeof(t->last_text), "%s", text);
        lv_label_set_text(t->value, text);
    }
    lv_color_t fill = critical ? TACH_RED : (warn ? TACH_ORANGE : TACH_YELLOW);
    lv_color_t value_color = !ok ? TACH_DIM : (critical ? TACH_RED : TACH_TEXT);
    uint32_t hex = (lv_color_to32(fill) << 1) ^ lv_color_to32(value_color);
    if (hex != t->last_hex) {
        t->last_hex = hex;
        ui_set_bg_color(t->fill, fill, LV_PART_MAIN);
        ui_set_text_color(t->value, value_color, LV_PART_MAIN);
        ui_set_border_color(t->box, critical ? TACH_RED : TACH_TILE_LINE, LV_PART_MAIN);
    }
    lv_coord_t track_h = TACH_TILE_H - 2 - 16;
    lv_coord_t fill_h = (lv_coord_t)(fraction * track_h + 0.5f);
    if (fill_h != t->last_fill_h) {
        t->last_fill_h = fill_h;
        lv_obj_set_pos(t->fill, 0, track_h - fill_h);
        lv_obj_set_height(t->fill, fill_h);
    }
}

/* ---- build ----------------------------------------------------------------- */

static lv_obj_t *tach_value_box(lv_obj_t *root, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                                const char *caption, lv_obj_t **value, lv_obj_t **caption_label)
{
    lv_obj_t *box = v2_box(root, x, y, w, 86, TACH_FACE, 4);
    lv_obj_set_style_border_width(box, 2, LV_PART_MAIN);
    ui_set_border_color(box, lv_color_hex(0xe6e6e6), LV_PART_MAIN);
    *value = v2_label(box, "0", &v2_font_value44, TACH_TEXT);
    lv_obj_align(*value, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *label = v2_label(box, caption, &v2_font_label13, TACH_GREY);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -6);
    if (caption_label) *caption_label = label;
    return box;
}

static lv_obj_t *build_theme_tach(lv_obj_t *cluster)
{
    lv_obj_t *root = lv_obj_create(cluster);
    lv_obj_add_flag(root, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, SCR_W, SCR_H);
    ui_set_bg_color(root, TACH_BG, LV_PART_MAIN);
    ui_set_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* pre-rendered dial */
    s_tach_canvas_buf = heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(TACH_CANVAS, TACH_CANVAS),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_tach_canvas_buf) {
        s_tach_canvas = lv_canvas_create(root);
        lv_canvas_set_buffer(s_tach_canvas, s_tach_canvas_buf, TACH_CANVAS, TACH_CANVAS,
                             LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(s_tach_canvas, TACH_CX - TACH_CANVAS / 2, TACH_CY - TACH_CANVAS / 2);
        lv_obj_clear_flag(s_tach_canvas, LV_OBJ_FLAG_CLICKABLE);
        tach_dial_render();
    }

    /* needle layer over the dial */
    s_tach_needle = lv_obj_create(root);
    lv_obj_set_pos(s_tach_needle, TACH_CX - TACH_CANVAS / 2, TACH_CY - TACH_CANVAS / 2);
    lv_obj_set_size(s_tach_needle, TACH_CANVAS, TACH_CANVAS);
    ui_set_bg_opa(s_tach_needle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tach_needle, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_tach_needle, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_tach_needle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_tach_needle, tach_needle_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    s_tach_needle_angle = -1000.0f;
    s_tach_needle_pts[0] = s_tach_needle_pts[1] = (lv_point_t){TACH_CX, TACH_CY};
    tach_needle_set(0);

    /* hub: brand, rpm, warning lights */
    s_tach_hub = v2_box(root, TACH_CX - 92, TACH_CY - 92, 184, 184, lv_color_hex(0x0e0e0f), LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(s_tach_hub, 3, LV_PART_MAIN);
    ui_set_border_color(s_tach_hub, lv_color_hex(0x323236), LV_PART_MAIN);
    lv_obj_t *brand = make_plain_container(s_tach_hub);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    v2_label(brand, "MACKO", &v2_font_brand18, TACH_TEXT);
    v2_label(brand, "TUNED", &v2_font_brand18, TACH_RED);
    lv_obj_align(brand, LV_ALIGN_TOP_MID, 0, 30);
    s_tach_rpm_val = v2_label(s_tach_hub, "0", &v2_font_value44, TACH_TEXT);
    lv_obj_align(s_tach_rpm_val, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_t *rpm_unit = v2_label(s_tach_hub, "RPM", &v2_font_label13, TACH_GREY);
    lv_obj_set_style_text_letter_space(rpm_unit, 3, LV_PART_MAIN);
    lv_obj_align(rpm_unit, LV_ALIGN_TOP_MID, 0, 108);
    static const char *const chips[3] = {"CEL", "OIL", "KNOCK"};
    static const lv_coord_t chip_x[3] = {-48, -6, 42};
    for (int i = 0; i < 3; ++i) {
        s_tach_chip[i] = v2_label(s_tach_hub, chips[i], &v2_font_label13, TACH_DIM);
        lv_obj_set_style_text_letter_space(s_tach_chip[i], 1, LV_PART_MAIN);
        lv_obj_align(s_tach_chip[i], LV_ALIGN_TOP_MID, chip_x[i], 132);
    }

    /* gear + speed boxes in the free lower-right of the dial */
    tach_value_box(root, TACH_CX - 4, TACH_CY + 98, 62, "GEAR", &s_tach_gear_val, NULL);
    lv_label_set_text(s_tach_gear_val, "N");
    tach_value_box(root, TACH_CX + 64, TACH_CY + 98, 110, dash_config_get_speed_kph() ? "SPEED KPH" : "SPEED MPH",
                   &s_tach_speed_val, &s_tach_speed_unit);

    /* tiles: slots 0-3 left, 4-7 right */
    for (int i = 0; i < 4; ++i) {
        lv_coord_t y = TACH_TILE_TOP + i * (TACH_TILE_H + TACH_TILE_GAP);
        tach_build_tile(root, i, 12, y, false);
        tach_build_tile(root, 4 + i, SCR_W - 12 - TACH_TILE_W, y, true);
    }

    /* odometer: top-right gap between the tiles and the dial */
    lv_obj_t *odo = make_plain_container(root);
    lv_obj_set_pos(odo, 646, 36);
    lv_obj_set_size(odo, 128, 46);
    lv_obj_set_ext_click_area(odo, 10);
    lv_obj_add_flag(odo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(odo, odo_cycle_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(odo, odo_reset_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    s_tach_odo_caption = v2_label(odo, "ODO", &v2_font_label13, TACH_GREY);
    lv_obj_set_style_text_letter_space(s_tach_odo_caption, 2, LV_PART_MAIN);
    lv_obj_align(s_tach_odo_caption, LV_ALIGN_TOP_RIGHT, 0, 0);
    s_tach_odo_val = v2_label(odo, "0 MI", &v2_font_label16, TACH_TEXT);
    lv_obj_align(s_tach_odo_val, LV_ALIGN_TOP_RIGHT, 0, 18);
    s_tach_last_odo = -1;

    /* Shared SIM / REC / settings buttons, spread over the free corners
       around the dial, clear of the bezel: settings bottom-left, REC
       bottom-right, SIM top-left. */
    build_settings_button(root, true);
    lv_obj_t *controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_t *sim_btn = lv_obj_get_child(controls, 0);
    lv_obj_t *rec_btn = lv_obj_get_child(controls, 1);
    lv_obj_t *set_btn = lv_obj_get_child(controls, 2);
    /* Small visible buttons that match the dial styling; the generous
       invisible click margin below keeps them easy to hit. */
    static const struct { lv_coord_t x, y, w, h; } spots[3] = {
        {262, 44, 60, 32},     /* SIM */
        {702, 530, 60, 32},    /* REC */
        {262, 530, 60, 32},    /* settings */
    };
    lv_obj_t *buttons[3] = {sim_btn, rec_btn, set_btn};
    for (int i = 0; i < 3; ++i) {
        if (!buttons[i]) continue;
        lv_obj_set_parent(buttons[i], root);
        lv_obj_add_flag(buttons[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_pos(buttons[i], spots[i].x, spots[i].y);
        lv_obj_set_size(buttons[i], spots[i].w, spots[i].h);
        lv_obj_set_style_radius(buttons[i], 6, LV_PART_MAIN);
        lv_obj_set_ext_click_area(buttons[i], 20);
        lv_obj_t *label = lv_obj_get_child(buttons[i], 0);
        if (label) {
            lv_obj_set_style_text_font(label, buttons[i] == set_btn ? &lv_font_montserrat_20 : &v2_font_label13,
                                       LV_PART_MAIN);
            if (buttons[i] != set_btn) lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN);
            lv_obj_center(label);
        }
    }
    lv_obj_del(controls);

    odo_tiles_refresh_display();
    return root;
}

/* ---- update ---------------------------------------------------------------- */

static void update_theme_tach(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    (void)limiter_hit;
    if (s_tach_scale_max != MAXRPM || s_tach_scale_red != REDLINE) tach_dial_render();

    static int last_metric = -1;
    int metric = (dash_config_get_temperature_celsius() ? 1 : 0) |
                 (dash_config_get_pressure_kpa() ? 2 : 0) |
                 (dash_config_get_speed_kph() ? 4 : 0) |
                 (dash_config_get_distance_km() ? 8 : 0);
    if (metric != last_metric) {
        last_metric = metric;
        set_label_text_if_changed(s_tach_speed_unit, dash_config_get_speed_kph() ? "SPEED KPH" : "SPEED MPH");
        for (int slot = 0; slot < TACH_TILES; ++slot) tach_refresh_identity(slot);
        s_tach_last_odo = -1;
    }

    tach_needle_set(rpm);
    static int last_rpm = -1;
    if (rpm != last_rpm) {
        last_rpm = rpm;
        lv_label_set_text_fmt(s_tach_rpm_val, "%d", rpm);
    }

    static int last_speed = -1;
    int speed = (int)(data->speed_mph + 0.5f);
    if (speed < 0) speed = 0;
    if (speed > 999) speed = 999;
    if (speed != last_speed) {
        last_speed = speed;
        lv_label_set_text_fmt(s_tach_speed_val, "%d", speed);
    }

    static int last_gear = -99;
    if (data->gear != last_gear) {
        last_gear = data->gear;
        if (data->gear <= 0) lv_label_set_text(s_tach_gear_val, "N");
        else lv_label_set_text_fmt(s_tach_gear_val, "%d", data->gear);
    }

    float values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg, fuel,
    };
    bool available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        true, true, true, true, true, true, true, data->oil_valid,
        data->duty_valid, data->knock_valid, true,
    };
    for (int slot = 0; slot < TACH_TILES; ++slot) {
        int channel = dash_config_get_modern_field_channel(slot);
        float value;
        bool ok;
        system_field_sample(channel, values, available, &value, &ok);
        system_field_refresh_live_unit(SYSTEM_FIELD_V2, slot, channel, value);
        tach_tile_set(slot, channel, value, ok);
    }

    double distance = data->odo_miles * (dash_config_get_distance_km() ? 1.60934 : 1.0);
    long odo = (long)(distance + 0.5);
    if (odo != s_tach_last_odo) {
        s_tach_last_odo = odo;
        char buf[24];
        long thousands = odo / 1000;
        const char *unit = dash_config_get_distance_km() ? "KM" : "MI";
        if (thousands > 0) snprintf(buf, sizeof(buf), "%ld,%03ld %s", thousands, odo % 1000, unit);
        else snprintf(buf, sizeof(buf), "%ld %s", odo, unit);
        lv_label_set_text(s_tach_odo_val, buf);
    }

    float oil_psi = dash_config_get_pressure_kpa() ? data->oil_psi / 6.89476f : data->oil_psi;
    float oil_low = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_OIL_LOW) / 10.0f;
    float knock_red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_KNOCK_RED) / 10.0f;
    bool lit[3] = {
        data->cel,
        data->oil_valid && oil_psi < oil_low,
        data->knock_valid && data->knock_deg >= knock_red,
    };
    for (int i = 0; i < 3; ++i) {
        ui_set_text_color(s_tach_chip[i], lit[i] ? TACH_RED : TACH_DIM, LV_PART_MAIN);
    }
}

/* Shift lights: the hub ring and RPM readout take the stage colour. */
static void tach_shift_apply(bool active, int flash_on, lv_color_t color, lv_opa_t active_opa)
{
    if (!s_tach_hub) return;
    bool on = active && flash_on;
    lv_color_t ring = on ? lv_color_mix(color, TACH_BG, active_opa) : lv_color_hex(0x323236);
    ui_set_border_color(s_tach_hub, ring, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tach_hub, on ? 6 : 3, LV_PART_MAIN);
    ui_set_text_color(s_tach_rpm_val, on ? ring : TACH_TEXT, LV_PART_MAIN);
}

/* ---- theme picker thumbnail ------------------------------------------------ */

static void tach_build_preview(lv_obj_t *pv)
{
    ui_set_bg_color(pv, TACH_BG, LV_PART_MAIN);
    lv_obj_t *dial = v2_box(pv, 0, 0, 92, 92, TACH_FACE, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(dial, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_border_width(dial, 6, LV_PART_MAIN);
    ui_set_border_color(dial, TACH_YELLOW, LV_PART_MAIN);
    lv_obj_set_style_border_side(dial, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_align(dial, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *hub = v2_box(dial, 0, 0, 30, 30, lv_color_hex(0x0e0e0f), LV_RADIUS_CIRCLE);
    lv_obj_center(hub);
    for (int side = 0; side < 2; ++side) {
        for (int row = 0; row < 4; ++row) {
            lv_obj_t *tile = v2_box(pv, 0, 0, 24, 20, TACH_TILE, 2);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_align(tile, side ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, side ? -8 : 8, 4 + row * 24);
            lv_obj_t *bar = v2_box(tile, side ? 18 : 2, 2 + row * 2, 4, 16 - row * 2, TACH_YELLOW, 1);
            (void)bar;
        }
    }
}
