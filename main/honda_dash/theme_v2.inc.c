/*
 * MackoDash V2 theme -- MackoTuned black / white / red home screen.
 *
 * #included by honda_dash_ui.c (it shares that file's static helpers:
 * field channels, warning colours, settings/REC buttons, odometer modes).
 *
 * Layout is built around the dash bezel, which covers the top corners
 * ~10 mm (V2_BEZEL_CORNER px) and arcs up to the top edge at the centre:
 * the RPM bar follows that curve and nothing else is placed under it.
 *
 * Rendering rules that keep it fast on the P4: every surface is opaque,
 * no shadows/gradients/opacity layers, slanted shapes are drawn once as
 * polygons and only the segments that change are invalidated.
 */

LV_FONT_DECLARE(v2_font_speed);
LV_FONT_DECLARE(v2_font_gear);
LV_FONT_DECLARE(v2_font_value44);
LV_FONT_DECLARE(v2_font_value34);
LV_FONT_DECLARE(v2_font_brand18);
LV_FONT_DECLARE(v2_font_label13);
LV_FONT_DECLARE(v2_font_label16);

#define V2_BG          lv_color_hex(0x050506)
#define V2_PANEL       lv_color_hex(0x111215)
#define V2_PANEL_LINE  lv_color_hex(0x24262b)
#define V2_STRIP       lv_color_hex(0x0b0c0e)
#define V2_STRIP_LINE  lv_color_hex(0x22242a)
#define V2_TRACK       lv_color_hex(0x222429)
#define V2_CHIP_OFF    lv_color_hex(0x16181c)
#define V2_CHIP_ON     lv_color_hex(0x2a0a10)
#define V2_PILL_LINE   lv_color_hex(0x2a2c31)
#define V2_TEXT        lv_color_hex(0xf2f3f5)
#define V2_LABEL       lv_color_hex(0xa7aab1)
#define V2_UNIT        lv_color_hex(0x8d9199)
#define V2_DIM         lv_color_hex(0x6b6f78)
#define V2_RED         lv_color_hex(0xe4002b)
#define V2_RED_TEXT    lv_color_hex(0xff4d6a)
#define V2_GREEN       lv_color_hex(0x39d98a)
#define V2_BLUE        lv_color_hex(0x4d8fff)
#define V2_AMBER       lv_color_hex(0xffb020)
#define V2_SEG_OFF     lv_color_hex(0x1c1d21)
#define V2_SEG_REDZONE lv_color_hex(0x3a0d16)

/* bezel: parabola, V2_BEZEL_CORNER px deep at both sides, V2_BEZEL_CENTER at the middle */
#define V2_BEZEL_CORNER 52
#define V2_BEZEL_CENTER 0
/* the lower bezel edge covers the last few px of the screen */
#define V2_BOTTOM_SAFE 12

#define V2_SEG_COUNT 44
#define V2_SEG_LEFT  26
#define V2_SEG_RIGHT 998
#define V2_SEG_GAP   4
#define V2_TICK_MAX  13
#define V2_SIDE_TILES 3
#define V2_BOTTOM_TILES 4

typedef struct {
    lv_color_t fill;
    lv_color_t border;
    uint8_t border_w;
    uint8_t skew;       /* horizontal lean of the top edge, px */
} v2_slant_t;

typedef struct {
    lv_obj_t *box;
    lv_obj_t *label;
    lv_obj_t *unit;
    lv_obj_t *value;
    lv_obj_t *fill;
    lv_coord_t track_w;
    bool bottom;
    char last_text[24];
    lv_coord_t last_fill_w;
    uint32_t last_fill_hex;
    uint32_t last_text_hex;
} v2_tile_t;

static lv_obj_t *s_v2_rpm_bar;
static lv_point_t s_v2_seg_pts[V2_SEG_COUNT][4];
static lv_area_t s_v2_seg_area[V2_SEG_COUNT];
static lv_color_t s_v2_seg_color[V2_SEG_COUNT];
static lv_obj_t *s_v2_ticks[V2_TICK_MAX];
static lv_obj_t *s_v2_rpm_val;
static lv_obj_t *s_v2_speed_val;
static lv_obj_t *s_v2_speed_unit;
static lv_obj_t *s_v2_gear_box;
static lv_obj_t *s_v2_gear_val;
static lv_obj_t *s_v2_pill_vtec;
static lv_obj_t *s_v2_pill_shift;
static lv_obj_t *s_v2_pill_launch;
static lv_obj_t *s_v2_pill_label[3];
static v2_slant_t s_v2_slant[4];          /* 0 gear, 1 vtec, 2 shift, 3 launch */
static v2_tile_t s_v2_tiles[DASH_CONFIG_MODERN_FIELD_COUNT];
static lv_obj_t *s_v2_chip[3];
static lv_obj_t *s_v2_chip_label[3];
static lv_obj_t *s_v2_fuel_fill;
static lv_obj_t *s_v2_fuel_label;
static int s_v2_tick_rpm_max = -1;

static int v2_bezel_edge(int x)
{
    float t = ((float)x - 512.0f) / 512.0f;
    return V2_BEZEL_CENTER + (int)((V2_BEZEL_CORNER - V2_BEZEL_CENTER) * t * t + 0.5f);
}

static lv_obj_t *v2_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    ui_set_text_color(label, color, LV_PART_MAIN);
    return label;
}

static lv_obj_t *v2_box(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                        lv_color_t bg, lv_coord_t radius)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    ui_set_bg_color(box, bg, LV_PART_MAIN);
    ui_set_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(box, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

/* ---- slanted shapes (gear box, indicator pills) -------------------------- */

static void v2_slant_points(const lv_area_t *a, int skew, int inset, lv_point_t pts[4])
{
    int k = skew;
    int lean = inset ? (inset * 4) / 3 : 0;   /* keep the border even on the slanted sides */
    pts[0] = (lv_point_t){a->x1 + k + lean, a->y1 + inset};
    pts[1] = (lv_point_t){a->x2 + 1 - inset, a->y1 + inset};
    pts[2] = (lv_point_t){a->x2 + 1 - k - lean, a->y2 + 1 - inset};
    pts[3] = (lv_point_t){a->x1 + inset, a->y2 + 1 - inset};
}

static void v2_slant_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    const v2_slant_t *s = (const v2_slant_t *)lv_obj_get_user_data(obj);
    if (!s) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    lv_point_t pts[4];
    if (s->border_w) {
        dsc.bg_color = s->border;
        v2_slant_points(&obj->coords, s->skew, 0, pts);
        lv_draw_polygon(ctx, &dsc, pts, 4);
        v2_slant_points(&obj->coords, s->skew, s->border_w, pts);
    } else {
        v2_slant_points(&obj->coords, s->skew, 0, pts);
    }
    dsc.bg_color = s->fill;
    lv_draw_polygon(ctx, &dsc, pts, 4);
}

static lv_obj_t *v2_slant_create(lv_obj_t *parent, v2_slant_t *style, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    ui_set_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(obj, style);
    lv_obj_add_event_cb(obj, v2_slant_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return obj;
}

static void v2_slant_set(lv_obj_t *obj, lv_color_t fill, lv_color_t border)
{
    v2_slant_t *s = (v2_slant_t *)lv_obj_get_user_data(obj);
    if (!s || (s->fill.full == fill.full && s->border.full == border.full)) return;
    s->fill = fill;
    s->border = border;
    lv_obj_invalidate(obj);
}

static lv_obj_t *v2_pill(lv_obj_t *parent, int index, const char *text, lv_coord_t w)
{
    s_v2_slant[1 + index] = (v2_slant_t){.fill = V2_BG, .border = V2_PILL_LINE, .border_w = 2, .skew = 6};
    lv_obj_t *pill = v2_slant_create(parent, &s_v2_slant[1 + index], w, 34);
    s_v2_pill_label[index] = v2_label(pill, text, &v2_font_label16, V2_DIM);
    lv_obj_set_style_text_letter_space(s_v2_pill_label[index], 2, LV_PART_MAIN);
    lv_obj_center(s_v2_pill_label[index]);
    return pill;
}

static void v2_pill_state(lv_obj_t *pill, lv_obj_t *label, int state)
{
    /* 0 = off, 1 = on (outline), 2 = on (filled) */
    lv_color_t accent = pill == s_v2_pill_vtec ? V2_GREEN : (pill == s_v2_pill_launch ? V2_AMBER : V2_RED);
    if (state == 2) {
        v2_slant_set(pill, accent, accent);
        ui_set_text_color(label, lv_color_white(), LV_PART_MAIN);
    } else if (state == 1) {
        v2_slant_set(pill, V2_BG, accent);
        ui_set_text_color(label, accent, LV_PART_MAIN);
    } else {
        v2_slant_set(pill, V2_BG, V2_PILL_LINE);
        ui_set_text_color(label, V2_DIM, LV_PART_MAIN);
    }
}

/* ---- RPM bar following the bezel ----------------------------------------- */

static void v2_rpm_draw_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    lv_coord_t ox = obj->coords.x1;
    lv_coord_t oy = obj->coords.y1;
    for (int i = 0; i < V2_SEG_COUNT; ++i) {
        lv_area_t area = s_v2_seg_area[i];
        lv_area_move(&area, ox, oy);
        if (!_lv_area_is_on(&area, ctx->clip_area)) continue;
        lv_point_t pts[4];
        for (int p = 0; p < 4; ++p) pts[p] = (lv_point_t){s_v2_seg_pts[i][p].x + ox, s_v2_seg_pts[i][p].y + oy};
        dsc.bg_color = s_v2_seg_color[i];
        lv_draw_polygon(ctx, &dsc, pts, 4);
    }
}

static int v2_segment_rpm(int index)
{
    return ((index + 1) * MAXRPM) / V2_SEG_COUNT;
}

static void v2_rpm_layout(lv_obj_t *bar)
{
    float width = (float)(V2_SEG_RIGHT - V2_SEG_LEFT - V2_SEG_GAP * (V2_SEG_COUNT - 1)) / V2_SEG_COUNT;
    for (int i = 0; i < V2_SEG_COUNT; ++i) {
        float x = V2_SEG_LEFT + i * (width + V2_SEG_GAP);
        int h = 20 + (i * 10) / (V2_SEG_COUNT - 1);
        int top = v2_bezel_edge((int)(x + width / 2)) + 8;
        int k = (h * 1625) / 10000;          /* tan(18 deg) / 2 * h */
        int x1 = (int)(x + 0.5f);
        int x2 = (int)(x + width + 0.5f);
        s_v2_seg_pts[i][0] = (lv_point_t){x1 + k, top};
        s_v2_seg_pts[i][1] = (lv_point_t){x2 + k, top};
        s_v2_seg_pts[i][2] = (lv_point_t){x2 - k, top + h};
        s_v2_seg_pts[i][3] = (lv_point_t){x1 - k, top + h};
        s_v2_seg_area[i] = (lv_area_t){x1 - k - 1, top - 1, x2 + k + 1, top + h + 1};
        s_v2_seg_color[i] = v2_segment_rpm(i) > REDLINE ? V2_SEG_REDZONE : V2_SEG_OFF;
    }
    (void)bar;
}

static void v2_rpm_ticks_refresh(void)
{
    if (s_v2_tick_rpm_max == MAXRPM) return;
    s_v2_tick_rpm_max = MAXRPM;
    for (int i = 0; i < V2_TICK_MAX; ++i) {
        lv_obj_t *tick = s_v2_ticks[i];
        if (!tick) continue;
        int rpm = i * 1000;
        if (rpm > MAXRPM) {
            lv_obj_add_flag(tick, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(tick, LV_OBJ_FLAG_HIDDEN);
        int x = V2_SEG_LEFT + ((V2_SEG_RIGHT - V2_SEG_LEFT) * rpm) / MAXRPM;
        /* just under the segment at this rpm (segments grow 20 -> 30 px) */
        int seg_h = 20 + (rpm * 10) / (MAXRPM > 0 ? MAXRPM : 1);
        lv_obj_set_pos(tick, x - 10, v2_bezel_edge(x) + 8 + seg_h + 3);
        ui_set_text_color(tick, rpm >= REDLINE ? V2_RED_TEXT : V2_UNIT, LV_PART_MAIN);
    }
}

/* Called from honda_dash_ui_update_shift_lights(), which already filters
   out calls where the bucket / flash / stage colours did not change. */
static void v2_rpm_apply(int bucket, bool flashing, int flash_on, lv_color_t stage_color, lv_opa_t active_opa)
{
    if (!s_v2_rpm_bar) return;
    v2_rpm_ticks_refresh();
    lv_coord_t ox = s_v2_rpm_bar->coords.x1;
    lv_coord_t oy = s_v2_rpm_bar->coords.y1;
    for (int i = 0; i < V2_SEG_COUNT; ++i) {
        bool active = flashing ? flash_on : i < bucket;
        bool red_zone = v2_segment_rpm(i) > REDLINE;
        lv_color_t color;
        if (active) {
            color = lv_color_mix(red_zone ? V2_RED : stage_color, V2_BG, active_opa);
        } else {
            color = red_zone ? V2_SEG_REDZONE : V2_SEG_OFF;
        }
        if (color.full == s_v2_seg_color[i].full) continue;
        s_v2_seg_color[i] = color;
        lv_area_t area = s_v2_seg_area[i];
        lv_area_move(&area, ox, oy);
        lv_obj_invalidate_area(s_v2_rpm_bar, &area);
    }
}

/* ---- data tiles ---------------------------------------------------------- */

static const char *v2_unit_text(const char *unit)
{
    if (!unit) return "";
    if (strcmp(unit, "DEG F") == 0) return "\xC2\xB0" "F";
    if (strcmp(unit, "DEG C") == 0) return "\xC2\xB0" "C";
    if (strcmp(unit, "DEG") == 0) return "\xC2\xB0";
    if (strcmp(unit, "RATIO") == 0) return ":1";
    return unit;
}

static void v2_refresh_identity(int slot)
{
    if (slot < 0 || slot >= DASH_CONFIG_MODERN_FIELD_COUNT || !s_v2_tiles[slot].label) return;
    int channel = dash_config_get_modern_field_channel(slot);
    if (channel < 0 || channel >= DASH_CONFIG_FIELD_CHANNEL_COUNT) channel = 0;
    char name[32];
    snprintf(name, sizeof(name), "%s", system_field_name(channel));
    for (char *c = name; *c; ++c) *c = (char)toupper((unsigned char)*c);
    lv_label_set_text(s_v2_tiles[slot].label, name);
    lv_label_set_text(s_v2_tiles[slot].unit, v2_unit_text(hal_channel_unit(channel)));
    s_v2_tiles[slot].last_text[0] = '\0';
}

static void v2_refresh_live_unit(int slot, const char *unit)
{
    if (slot < 0 || slot >= DASH_CONFIG_MODERN_FIELD_COUNT || !s_v2_tiles[slot].unit) return;
    set_label_text_if_changed(s_v2_tiles[slot].unit, v2_unit_text(unit));
}

static void v2_build_tile(lv_obj_t *root, int slot, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                          bool bottom)
{
    v2_tile_t *t = &s_v2_tiles[slot];
    t->bottom = bottom;
    t->box = v2_box(root, x, y, w, h, V2_PANEL, 4);
    lv_obj_set_style_border_width(t->box, 1, LV_PART_MAIN);
    ui_set_border_color(t->box, V2_PANEL_LINE, LV_PART_MAIN);
    lv_obj_add_flag(t->box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(t->box, system_field_long_press_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)(SYSTEM_FIELD_V2 * 16 + slot));
    add_press_feedback(t->box);

    t->label = v2_label(t->box, "", &v2_font_label13, V2_LABEL);
    lv_obj_set_style_text_letter_space(t->label, 1, LV_PART_MAIN);
    t->unit = v2_label(t->box, "", &v2_font_label13, V2_UNIT);
    lv_coord_t inner_w = w - 2;
    lv_coord_t inner_h = h - 2;
    if (!bottom) {
        lv_obj_set_pos(t->label, 13, 9);
        lv_obj_align(t->unit, LV_ALIGN_TOP_RIGHT, -13, 9);
        t->value = v2_label(t->box, "--", &v2_font_value44, V2_TEXT);
        lv_obj_set_pos(t->value, 12, 28);
        t->track_w = inner_w - 26;
        lv_obj_t *track = v2_box(t->box, 13, inner_h - 15, t->track_w, 5, V2_TRACK, 1);
        t->fill = v2_box(track, 0, 0, 0, 5, V2_GREEN, 1);
    } else {
        lv_obj_set_pos(t->label, 13, 11);
        lv_obj_set_pos(t->unit, 13, 31);
        t->value = v2_label(t->box, "--", &v2_font_value34, V2_TEXT);
        lv_obj_set_width(t->value, inner_w - 100);
        lv_obj_set_style_text_align(t->value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_align(t->value, LV_ALIGN_RIGHT_MID, -12, 0);
        t->track_w = inner_w;
        t->fill = v2_box(t->box, 0, inner_h - 3, 0, 3, V2_GREEN, 0);
    }
    t->last_fill_w = -1;
    t->last_fill_hex = 0;
    t->last_text_hex = 0;
    v2_refresh_identity(slot);
}

static lv_color_t v2_zone_color(uint32_t zone_hex)
{
    switch (zone_hex) {
        case 0xe4002b: return V2_RED_TEXT;
        case 0xffb020: return V2_AMBER;
        case 0x4d8fff: return V2_BLUE;
        case 0x4c4e54: return V2_DIM;
        default:       return V2_GREEN;
    }
}

static void v2_tile_set(int slot, int channel, float value, bool ok)
{
    v2_tile_t *t = &s_v2_tiles[slot];
    if (!t->box) return;

    char text[24];
    system_field_format_text(channel, value, ok, text, sizeof(text));
    lv_color_t base = system_field_value_color(channel, value, ok);
    lv_color_t text_color = !ok ? V2_DIM :
                            (base.full == C_RED.full ? V2_RED_TEXT :
                            (base.full == C_AMBER.full ? V2_AMBER : V2_TEXT));

    float fraction = 0.0f;
    uint32_t zone_hex = 0x39ff8c;
    if (ok) {
        if (system_field_is_ecu(channel)) {
            fraction = ecu_channel_fraction(channel - DASH_CONFIG_HAL_CHANNEL_COUNT, value);
        } else if (channel < TILE_COUNT) {
            const tile_def_t *def = &TILE_DEFS[channel];
            fraction = (value - def->min) / (def->max - def->min);
            zone_hex = zone_color_for((tile_id_t)channel, value);
        } else {
            fraction = value / 100.0f;   /* fuel % */
            zone_hex = value <= 15.0f ? 0xe4002b : (value <= 30.0f ? 0xffb020 : 0x39ff8c);
        }
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
        if (text_color.full == V2_RED_TEXT.full) zone_hex = 0xe4002b;
        else if (text_color.full == V2_AMBER.full) zone_hex = 0xffb020;
    }

    if (strcmp(text, t->last_text) != 0) {
        snprintf(t->last_text, sizeof(t->last_text), "%s", text);
        lv_label_set_text(t->value, text);
    }
    uint32_t text_hex = lv_color_to32(text_color);
    if (text_hex != t->last_text_hex) {
        t->last_text_hex = text_hex;
        ui_set_text_color(t->value, text_color, LV_PART_MAIN);
        ui_set_border_color(t->box, text_color.full == V2_RED_TEXT.full ? V2_RED_TEXT : V2_PANEL_LINE,
                            LV_PART_MAIN);
    }
    lv_coord_t fill_w = (lv_coord_t)(fraction * t->track_w + 0.5f);
    if (fill_w != t->last_fill_w) {
        t->last_fill_w = fill_w;
        lv_obj_set_width(t->fill, fill_w);
    }
    if (zone_hex != t->last_fill_hex) {
        t->last_fill_hex = zone_hex;
        ui_set_bg_color(t->fill, v2_zone_color(zone_hex), LV_PART_MAIN);
    }
}

/* ---- build ---------------------------------------------------------------- */

static lv_obj_t *build_theme_v2(lv_obj_t *cluster)
{
    lv_obj_t *root = lv_obj_create(cluster);
    lv_obj_add_flag(root, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, SCR_W, SCR_H);
    ui_set_bg_color(root, V2_BG, LV_PART_MAIN);
    ui_set_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* RPM bar + scale, following the bezel curve */
    s_v2_rpm_bar = lv_obj_create(root);
    lv_obj_set_pos(s_v2_rpm_bar, 0, 0);
    lv_obj_set_size(s_v2_rpm_bar, SCR_W, 120);
    ui_set_bg_opa(s_v2_rpm_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_v2_rpm_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_v2_rpm_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_v2_rpm_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    v2_rpm_layout(s_v2_rpm_bar);
    lv_obj_add_event_cb(s_v2_rpm_bar, v2_rpm_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    for (int i = 0; i < V2_TICK_MAX; ++i) {
        char text[4];
        snprintf(text, sizeof(text), "%d", i);
        s_v2_ticks[i] = v2_label(root, text, &v2_font_label13, V2_UNIT);
        lv_obj_set_width(s_v2_ticks[i], 20);
        lv_obj_set_style_text_align(s_v2_ticks[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    s_v2_tick_rpm_max = -1;
    v2_rpm_ticks_refresh();

    /* side tiles: slots 0-2 left, 3-5 right; bottom row: slots 6-9 */
    for (int i = 0; i < V2_SIDE_TILES; ++i) {
        v2_build_tile(root, i, 20, 122 + i * 116, 232, 106, false);
        v2_build_tile(root, V2_SIDE_TILES + i, SCR_W - 20 - 232, 122 + i * 116, 232, 106, false);
    }
    for (int i = 0; i < V2_BOTTOM_TILES; ++i) {
        lv_coord_t x = 20 + (lv_coord_t)((i * 2487) / 10);
        v2_build_tile(root, 2 * V2_SIDE_TILES + i, x, 470, 238, 64, true);
    }

    /* centre: RPM, speed, gear, indicator pills */
    lv_obj_t *centre = make_plain_container(root);
    lv_obj_set_pos(centre, 266, 92);
    lv_obj_set_size(centre, 492, 368);
    lv_obj_set_flex_flow(centre, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(centre, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(centre, 8, LV_PART_MAIN);

    lv_obj_t *rpm_row = make_plain_container(centre);
    lv_obj_set_size(rpm_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rpm_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(rpm_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(rpm_row, 10, LV_PART_MAIN);
    s_v2_rpm_val = v2_label(rpm_row, "0", &v2_font_value44, V2_TEXT);
    lv_obj_set_width(s_v2_rpm_val, 150);
    lv_obj_set_style_text_align(s_v2_rpm_val, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_t *rpm_unit = v2_label(rpm_row, "RPM", &v2_font_label13, V2_LABEL);
    lv_obj_set_style_text_letter_space(rpm_unit, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(rpm_unit, 10, LV_PART_MAIN);

    lv_obj_t *hero = make_plain_container(centre);
    lv_obj_set_size(hero, 470, 190);
    lv_obj_t *speed_col = make_plain_container(hero);
    lv_obj_set_size(speed_col, 320, 190);
    lv_obj_set_pos(speed_col, 0, 0);
    s_v2_speed_val = v2_label(speed_col, "0", &v2_font_speed, V2_TEXT);
    lv_obj_set_width(s_v2_speed_val, 320);
    lv_obj_set_style_text_align(s_v2_speed_val, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_v2_speed_val, -4, LV_PART_MAIN);
    lv_obj_align(s_v2_speed_val, LV_ALIGN_TOP_RIGHT, 0, -24);
    s_v2_speed_unit = v2_label(speed_col, dash_config_get_speed_kph() ? "KPH" : "MPH",
                               &v2_font_label16, V2_LABEL);
    lv_obj_set_style_text_letter_space(s_v2_speed_unit, 4, LV_PART_MAIN);
    lv_obj_align(s_v2_speed_unit, LV_ALIGN_BOTTOM_RIGHT, -6, 0);

    lv_obj_t *gear_col = make_plain_container(hero);
    lv_obj_set_size(gear_col, 126, 190);
    lv_obj_set_pos(gear_col, 344, 0);
    lv_obj_t *gear_caption = v2_label(gear_col, "GEAR", &v2_font_label13, V2_LABEL);
    lv_obj_set_style_text_letter_space(gear_caption, 3, LV_PART_MAIN);
    lv_obj_align(gear_caption, LV_ALIGN_TOP_MID, 0, 18);
    s_v2_slant[0] = (v2_slant_t){.fill = V2_RED, .border = V2_RED, .border_w = 0, .skew = 22};
    s_v2_gear_box = v2_slant_create(gear_col, &s_v2_slant[0], 122, 132);
    lv_obj_align(s_v2_gear_box, LV_ALIGN_TOP_MID, 0, 40);
    s_v2_gear_val = v2_label(s_v2_gear_box, "N", &v2_font_gear, lv_color_white());
    lv_obj_center(s_v2_gear_val);

    lv_obj_t *pills = make_plain_container(centre);
    lv_obj_set_size(pills, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pills, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(pills, 10, LV_PART_MAIN);
    s_v2_pill_vtec = v2_pill(pills, 0, "VTEC", 92);
    s_v2_pill_shift = v2_pill(pills, 1, "SHIFT", 98);
    s_v2_pill_launch = v2_pill(pills, 2, "LAUNCH", 112);
    if (!dash_config_get_show_vtec()) lv_obj_add_flag(s_v2_pill_vtec, LV_OBJ_FLAG_HIDDEN);

    /* bottom strip */
    /* the bottom V2_BOTTOM_SAFE px sit under the bezel: nothing is drawn there */
    lv_obj_t *strip = v2_box(root, 0, SCR_H - V2_BOTTOM_SAFE - 46, SCR_W, 46, V2_STRIP, 0);
    lv_obj_set_style_border_width(strip, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    ui_set_border_color(strip, V2_STRIP_LINE, LV_PART_MAIN);

    lv_obj_t *odo = make_plain_container(strip);
    lv_obj_set_pos(odo, 20, 3);
    lv_obj_set_size(odo, 130, 40);
    lv_obj_add_flag(odo, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(odo, odo_cycle_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(odo, odo_reset_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    s_v2_odo_caption_label = v2_label(odo, "ODO", &v2_font_label13, V2_UNIT);
    lv_obj_set_style_text_letter_space(s_v2_odo_caption_label, 2, LV_PART_MAIN);
    lv_obj_set_pos(s_v2_odo_caption_label, 0, 0);
    s_v2_odo_val_label = v2_label(odo, "0 MI", &v2_font_label16, V2_TEXT);
    lv_obj_set_pos(s_v2_odo_val_label, 0, 18);

    static const char *const chips[3] = {"CEL", "OIL", "KNOCK"};
    static const lv_coord_t chip_w[3] = {44, 42, 62};
    lv_coord_t chip_x = 168;
    for (int i = 0; i < 3; ++i) {
        s_v2_chip[i] = v2_box(strip, chip_x, 10, chip_w[i], 24, V2_CHIP_OFF, 3);
        s_v2_chip_label[i] = v2_label(s_v2_chip[i], chips[i], &v2_font_label13, V2_DIM);
        lv_obj_set_style_text_letter_space(s_v2_chip_label[i], 1, LV_PART_MAIN);
        lv_obj_center(s_v2_chip_label[i]);
        chip_x += chip_w[i] + 8;
    }

    lv_obj_t *brand = make_plain_container(strip);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    v2_label(brand, "MACKO", &v2_font_brand18, V2_TEXT);
    v2_label(brand, "TUNED", &v2_font_brand18, V2_RED);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 452, 0);

    v2_label(strip, "FUEL", &v2_font_label13, V2_LABEL);
    lv_obj_t *fuel_caption = lv_obj_get_child(strip, lv_obj_get_child_cnt(strip) - 1);
    lv_obj_set_style_text_letter_space(fuel_caption, 1, LV_PART_MAIN);
    lv_obj_align(fuel_caption, LV_ALIGN_LEFT_MID, 604, 0);
    lv_obj_t *fuel_track = v2_box(strip, 644, 17, 96, 10, V2_TRACK, 2);
    s_v2_fuel_fill = v2_box(fuel_track, 0, 0, 0, 10, V2_TEXT, 2);
    s_v2_fuel_label = v2_label(strip, "0%", &v2_font_label16, V2_TEXT);
    lv_obj_align(s_v2_fuel_label, LV_ALIGN_LEFT_MID, 748, 0);

    /* REC / settings / SIM: the shared controls every built-in theme uses */
    build_settings_button(root, true);
    lv_obj_t *controls = lv_obj_get_child(root, lv_obj_get_child_cnt(root) - 1);
    lv_obj_add_flag(controls, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(controls, DASH_CONTROL_ROW_WIDTH, 40);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(controls, LV_ALIGN_BOTTOM_RIGHT, -16, -(V2_BOTTOM_SAFE + 3));

    odo_tiles_refresh_display();
    return root;
}

/* ---- update --------------------------------------------------------------- */

static void v2_format_rpm(char *buf, size_t size, int rpm)
{
    if (rpm >= 1000) snprintf(buf, size, "%d,%03d", rpm / 1000, rpm % 1000);
    else snprintf(buf, size, "%d", rpm);
}

static void update_theme_v2(const honda_dash_data_t *data, int rpm, bool limiter_hit, float fuel)
{
    (void)limiter_hit;
    static int last_metric = -1;
    int metric = (dash_config_get_temperature_celsius() ? 1 : 0) |
                 (dash_config_get_pressure_kpa() ? 2 : 0) |
                 (dash_config_get_speed_kph() ? 4 : 0) |
                 (dash_config_get_distance_km() ? 8 : 0);
    if (metric != last_metric) {
        last_metric = metric;
        set_label_text_if_changed(s_v2_speed_unit, dash_config_get_speed_kph() ? "KPH" : "MPH");
        for (int slot = 0; slot < DASH_CONFIG_MODERN_FIELD_COUNT; ++slot) v2_refresh_identity(slot);
        odo_tiles_refresh_display();
    }

    static int last_rpm = -1;
    if (rpm != last_rpm) {
        last_rpm = rpm;
        char buf[12];
        v2_format_rpm(buf, sizeof(buf), rpm);
        lv_label_set_text(s_v2_rpm_val, buf);
        ui_set_text_color(s_v2_rpm_val, rpm >= REDLINE ? V2_RED_TEXT : V2_TEXT, LV_PART_MAIN);
    }

    static int last_speed = -1;
    int speed = (int)(data->speed_mph + 0.5f);
    if (speed < 0) speed = 0;
    if (speed > 999) speed = 999;
    if (speed != last_speed) {
        last_speed = speed;
        lv_label_set_text_fmt(s_v2_speed_val, "%d", speed);
    }

    static int last_gear = -99;
    if (data->gear != last_gear) {
        last_gear = data->gear;
        if (data->gear <= 0) lv_label_set_text(s_v2_gear_val, "N");
        else lv_label_set_text_fmt(s_v2_gear_val, "%d", data->gear);
    }

    /* indicator pills */
    bool show_vtec = dash_config_get_show_vtec();
    if (show_vtec == lv_obj_has_flag(s_v2_pill_vtec, LV_OBJ_FLAG_HIDDEN)) {
        if (show_vtec) lv_obj_clear_flag(s_v2_pill_vtec, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_v2_pill_vtec, LV_OBJ_FLAG_HIDDEN);
    }
    if (show_vtec) v2_pill_state(s_v2_pill_vtec, s_v2_pill_label[0], rpm >= VTEC_RPM ? 1 : 0);
    bool shift_on = dash_config_get_shift_light_enabled() && rpm >= dash_config_get_shift_stage_rpm(2);
    v2_pill_state(s_v2_pill_shift, s_v2_pill_label[1], shift_on ? 2 : 0);
    float launch = 0.0f;
    bool launch_on = ecu_channel_get(ECU_CH_LAUNCH_ACTIVE, &launch) && launch >= 0.5f;
    v2_pill_state(s_v2_pill_launch, s_v2_pill_label[2], launch_on ? 1 : 0);

    /* tiles share MackoDash V1's ten gauge assignments */
    float values[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        data->ect_f, data->iat_f, data->afr, data->timing_deg, data->map_psi,
        data->batt_v, data->tps_pct, data->oil_psi, data->duty_pct, data->knock_deg, fuel,
    };
    bool available[DASH_CONFIG_HAL_CHANNEL_COUNT] = {
        true, true, true, true, true, true, true, data->oil_valid,
        data->duty_valid, data->knock_valid, true,
    };
    for (int slot = 0; slot < DASH_CONFIG_MODERN_FIELD_COUNT; ++slot) {
        int channel = dash_config_get_modern_field_channel(slot);
        float value;
        bool ok;
        system_field_sample(channel, values, available, &value, &ok);
        system_field_refresh_live_unit(SYSTEM_FIELD_V2, slot, channel, value);
        v2_tile_set(slot, channel, value, ok);
    }

    /* odometer (data->odo_miles already follows the ODO / TRIP A / TRIP B mode) */
    static long last_odo = -1;
    double distance = data->odo_miles * (dash_config_get_distance_km() ? 1.60934 : 1.0);
    long odo = (long)(distance + 0.5);
    if (odo != last_odo) {
        last_odo = odo;
        char buf[24];
        long thousands = odo / 1000;
        if (thousands > 0) snprintf(buf, sizeof(buf), "%ld,%03ld %s", thousands, odo % 1000,
                                    dash_config_get_distance_km() ? "KM" : "MI");
        else snprintf(buf, sizeof(buf), "%ld %s", odo, dash_config_get_distance_km() ? "KM" : "MI");
        lv_label_set_text(s_v2_odo_val_label, buf);
    }

    /* warning chips */
    float oil_psi = dash_config_get_pressure_kpa() ? data->oil_psi / 6.89476f : data->oil_psi;
    float oil_low = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_OIL_LOW) / 10.0f;
    float knock_red = dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_KNOCK_RED) / 10.0f;
    bool lit[3] = {
        data->cel,
        data->oil_valid && oil_psi < oil_low,
        data->knock_valid && data->knock_deg >= knock_red,
    };
    for (int i = 0; i < 3; ++i) {
        ui_set_bg_color(s_v2_chip[i], lit[i] ? V2_CHIP_ON : V2_CHIP_OFF, LV_PART_MAIN);
        ui_set_text_color(s_v2_chip_label[i], lit[i] ? V2_RED_TEXT : V2_DIM, LV_PART_MAIN);
    }

    /* fuel */
    static int last_fuel = -1;
    int fuel_i = (int)(fuel + 0.5f);
    if (fuel_i != last_fuel) {
        last_fuel = fuel_i;
        lv_obj_set_width(s_v2_fuel_fill, (lv_coord_t)((96 * fuel_i) / 100));
        ui_set_bg_color(s_v2_fuel_fill, fuel_i <= 15 ? V2_RED_TEXT : (fuel_i <= 30 ? V2_AMBER : V2_TEXT),
                        LV_PART_MAIN);
        lv_label_set_text_fmt(s_v2_fuel_label, "%d%%", fuel_i);
    }
}

/* ---- theme picker thumbnail ---------------------------------------------- */

static void v2_build_preview(lv_obj_t *pv)
{
    ui_set_bg_color(pv, V2_BG, LV_PART_MAIN);
    for (int i = 0; i < 16; ++i) {
        float t = (i - 7.5f) / 7.5f;
        lv_obj_t *seg = v2_box(pv, 8 + i * 10, 4 + (int)(8 * t * t), 7, 7 + i / 4,
                               i < 11 ? V2_TEXT : (i < 13 ? V2_RED : V2_SEG_OFF), 0);
        lv_obj_add_flag(seg, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    lv_obj_t *speed = v2_label(pv, "87", &v2_font_value44, V2_TEXT);
    lv_obj_add_flag(speed, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(speed, LV_ALIGN_CENTER, -12, 12);
    lv_obj_t *gear = v2_box(pv, 0, 0, 22, 28, V2_RED, 0);
    lv_obj_add_flag(gear, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(gear, LV_ALIGN_CENTER, 34, 12);
    lv_obj_t *gear_label = v2_label(gear, "4", &v2_font_label16, lv_color_white());
    lv_obj_center(gear_label);
    for (int side = 0; side < 2; ++side) {
        for (int row = 0; row < 2; ++row) {
            lv_obj_t *tile = v2_box(pv, side ? 132 : 8, 40 + row * 26, 26, 20, V2_PANEL, 2);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
            ui_set_border_color(tile, V2_PANEL_LINE, LV_PART_MAIN);
        }
    }
}
