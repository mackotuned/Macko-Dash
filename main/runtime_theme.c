#include "runtime_theme.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "dash_config.h"
#include "esp_log.h"

#define RUNTIME_LAYOUT_MAX_SIZE (64 * 1024)
#define RUNTIME_ASSET_MAX_SIZE (2 * 1024 * 1024)
#define RUNTIME_OBJECT_MAX 96
#define RUNTIME_IMAGE_MAX 12
#define RUNTIME_NAME_MAX 80
#define TARGET_WIDTH 1024
#define TARGET_HEIGHT 600

static const char *TAG = "runtime_theme";

typedef enum {
    BIND_NONE = 0,
    BIND_RPM,
    BIND_SPEED,
    BIND_GEAR,
    BIND_ECT,
    BIND_IAT,
    BIND_AFR,
    BIND_TIMING,
    BIND_MAP,
    BIND_BATT,
    BIND_TPS,
    BIND_OIL,
    BIND_DUTY,
    BIND_KNOCK,
    BIND_FUEL,
    BIND_ODO,
    BIND_CEL_INDICATOR,
    BIND_VTEC_INDICATOR,
    BIND_SHIFT_INDICATOR,
    BIND_OIL_INDICATOR,
    BIND_KNOCK_INDICATOR,
    BIND_SPEED_UNIT,
    BIND_ECT_UNIT,
    BIND_IAT_UNIT,
    BIND_MAP_UNIT,
    BIND_FUEL_UNIT,
    BIND_TPS_UNIT,
    BIND_OIL_UNIT,
    BIND_SETTINGS_BUTTON,
    BIND_RECORD_BUTTON,
} binding_id_t;

typedef enum {
    WIDGET_LABEL,
    WIDGET_BAR,
    WIDGET_ARC,
    WIDGET_INDICATOR,
    WIDGET_BUTTON,
    WIDGET_IMAGE,
} widget_kind_t;

typedef struct {
    lv_obj_t *object;
    binding_id_t id;
    widget_kind_t kind;
} runtime_binding_t;

typedef struct {
    binding_id_t id;
    const char *canonical;
    const char *aliases[4];
} binding_name_t;

static const binding_name_t BINDING_NAMES[] = {
    {BIND_RPM, "dash_rpm_value", {"rpm_value", "engine_rpm_value", NULL}},
    {BIND_SPEED, "dash_speed_value", {"speed_value", "mph_value", "kph_value", NULL}},
    {BIND_GEAR, "dash_gear_value", {"gear_value", NULL}},
    {BIND_ECT, "dash_ect_value", {"coolant_value", "water_temp_value", NULL}},
    {BIND_IAT, "dash_iat_value", {"intake_temp_value", "air_temp_value", NULL}},
    {BIND_AFR, "dash_afr_value", {"afr_value", "lambda_value", NULL}},
    {BIND_TIMING, "dash_timing_value", {"timing_value", "ignition_value", NULL}},
    {BIND_MAP, "dash_map_value", {"map_value", "boost_value", NULL}},
    {BIND_BATT, "dash_batt_value", {"battery_value", "voltage_value", NULL}},
    {BIND_TPS, "dash_tps_value", {"tps_value", "throttle_value", NULL}},
    {BIND_OIL, "dash_oil_value", {"oil_pressure_value", NULL}},
    {BIND_DUTY, "dash_duty_value", {"injector_duty_value", NULL}},
    {BIND_KNOCK, "dash_knock_value", {"knock_value", NULL}},
    {BIND_FUEL, "dash_fuel_value", {"fuel_level_value", NULL}},
    {BIND_ODO, "dash_odo_value", {"odometer_value", NULL}},
    {BIND_RPM, "dash_rpm_bar", {"rpm_bar", NULL}},
    {BIND_RPM, "dash_rpm_arc", {"rpm_arc", NULL}},
    {BIND_SPEED, "dash_speed_bar", {"speed_bar", NULL}},
    {BIND_SPEED, "dash_speed_arc", {"speed_arc", NULL}},
    {BIND_FUEL, "dash_fuel_bar", {"fuel_bar", NULL}},
    {BIND_FUEL, "dash_fuel_arc", {"fuel_arc", NULL}},
    {BIND_TPS, "dash_tps_bar", {"tps_bar", NULL}},
    {BIND_TPS, "dash_tps_arc", {"tps_arc", NULL}},
    {BIND_ECT, "dash_ect_bar", {"ect_bar", "coolant_bar", NULL}},
    {BIND_ECT, "dash_ect_arc", {"ect_arc", "coolant_arc", NULL}},
    {BIND_IAT, "dash_iat_bar", {"iat_bar", "air_temp_bar", NULL}},
    {BIND_IAT, "dash_iat_arc", {"iat_arc", "air_temp_arc", NULL}},
    {BIND_OIL, "dash_oil_bar", {"oil_bar", NULL}},
    {BIND_OIL, "dash_oil_arc", {"oil_arc", NULL}},
    {BIND_CEL_INDICATOR, "dash_cel_indicator", {"cel_indicator", NULL}},
    {BIND_VTEC_INDICATOR, "dash_vtec_indicator", {"vtec_indicator", NULL}},
    {BIND_SHIFT_INDICATOR, "dash_shift_indicator", {"shift_indicator", NULL}},
    {BIND_OIL_INDICATOR, "dash_oil_indicator", {"oil_indicator", NULL}},
    {BIND_KNOCK_INDICATOR, "dash_knock_indicator", {"knock_indicator", NULL}},
    {BIND_SPEED_UNIT, "dash_speed_unit", {"speed_unit", "mph_label", "kph_label", NULL}},
    {BIND_ECT_UNIT, "dash_ect_unit", {"ect_unit", "coolant_unit", NULL}},
    {BIND_IAT_UNIT, "dash_iat_unit", {"iat_unit", NULL}},
    {BIND_MAP_UNIT, "dash_map_unit", {"map_unit", "boost_unit", NULL}},
    {BIND_FUEL_UNIT, "dash_fuel_unit", {"fuel_unit", NULL}},
    {BIND_TPS_UNIT, "dash_tps_unit", {"tps_unit", NULL}},
    {BIND_OIL_UNIT, "dash_oil_unit", {"oil_unit", NULL}},
    {BIND_SETTINGS_BUTTON, "dash_settings_button", {"settings_button", NULL}},
    {BIND_RECORD_BUTTON, "dash_record_button", {"record_button", "logging_button", NULL}},
};

static lv_obj_t *s_root;
static const theme_storage_package_t *s_package;
static runtime_binding_t s_bindings[RUNTIME_OBJECT_MAX];
static size_t s_binding_count;
static lv_img_dsc_t *s_image_descriptors[RUNTIME_IMAGE_MAX];
static uint8_t *s_image_data[RUNTIME_IMAGE_MAX];
static size_t s_image_count;
static float s_canvas_scale = 1.0f;
static int s_canvas_offset_x;
static int s_canvas_offset_y;
static int s_design_width = TARGET_WIDTH;
static int s_design_height = TARGET_HEIGHT;

static void normalize_name(const char *source, char output[RUNTIME_NAME_MAX])
{
    size_t out = 0;
    bool underscore = false;
    for (size_t i = 0; source && source[i] && out + 1 < RUNTIME_NAME_MAX; ++i) {
        unsigned char ch = (unsigned char)source[i];
        if (isalnum(ch)) {
            output[out++] = (char)tolower(ch);
            underscore = false;
        } else if (!underscore && out > 0) {
            output[out++] = '_';
            underscore = true;
        }
    }
    while (out > 0 && output[out - 1] == '_') --out;
    output[out] = '\0';
    if (strncmp(output, "ui_", 3) == 0) memmove(output, output + 3, strlen(output + 3) + 1);

    char *last = strrchr(output, '_');
    if (last && last[1] >= '2' && last[1] <= '9' && last[2] == '\0') *last = '\0';
}

static bool one_edit_apart(const char *left, const char *right)
{
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    if (left_len > right_len + 1 || right_len > left_len + 1) return false;
    if (left_len == right_len) {
        size_t mismatch[2];
        size_t count = 0;
        for (size_t i = 0; i < left_len; ++i) {
            if (left[i] != right[i] && count < 2) mismatch[count++] = i;
            else if (left[i] != right[i]) return false;
        }
        return count == 1 || (count == 2 && mismatch[1] == mismatch[0] + 1 &&
               left[mismatch[0]] == right[mismatch[1]] && left[mismatch[1]] == right[mismatch[0]]);
    }
    const char *shorter = left_len < right_len ? left : right;
    const char *longer = left_len < right_len ? right : left;
    size_t short_pos = 0;
    size_t long_pos = 0;
    bool skipped = false;
    while (shorter[short_pos] && longer[long_pos]) {
        if (shorter[short_pos] == longer[long_pos]) {
            ++short_pos;
            ++long_pos;
        } else if (!skipped) {
            skipped = true;
            ++long_pos;
        } else {
            return false;
        }
    }
    return true;
}

static binding_id_t resolve_binding(const char *name)
{
    char normalized[RUNTIME_NAME_MAX];
    normalize_name(name, normalized);
    if (!normalized[0]) return BIND_NONE;

    for (size_t i = 0; i < sizeof(BINDING_NAMES) / sizeof(BINDING_NAMES[0]); ++i) {
        if (strcmp(normalized, BINDING_NAMES[i].canonical) == 0) return BINDING_NAMES[i].id;
        for (size_t alias = 0; alias < 4 && BINDING_NAMES[i].aliases[alias]; ++alias) {
            if (strcmp(normalized, BINDING_NAMES[i].aliases[alias]) == 0) return BINDING_NAMES[i].id;
        }
    }

    binding_id_t match = BIND_NONE;
    for (size_t i = 0; i < sizeof(BINDING_NAMES) / sizeof(BINDING_NAMES[0]); ++i) {
        bool close = one_edit_apart(normalized, BINDING_NAMES[i].canonical);
        for (size_t alias = 0; !close && alias < 4 && BINDING_NAMES[i].aliases[alias]; ++alias) {
            close = one_edit_apart(normalized, BINDING_NAMES[i].aliases[alias]);
        }
        if (!close) continue;
        if (match != BIND_NONE && match != BINDING_NAMES[i].id) return BIND_NONE;
        match = BINDING_NAMES[i].id;
    }
    return match;
}

static uint32_t json_color(cJSON *object, const char *key, uint32_t fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item)) return (uint32_t)item->valuedouble & 0xffffff;
    if (cJSON_IsString(item) && item->valuestring) {
        const char *value = item->valuestring;
        if (*value == '#') ++value;
        if (strlen(value) == 6) return (uint32_t)strtoul(value, NULL, 16) & 0xffffff;
    }
    return fallback;
}

static int json_int(cJSON *object, const char *key, int fallback, int low, int high)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    int value = cJSON_IsNumber(item) ? item->valueint : fallback;
    if (value < low) value = low;
    if (value > high) value = high;
    return value;
}

static const lv_font_t *font_for_size(int size)
{
    if (size >= 36) return &lv_font_montserrat_44;
    if (size >= 21) return &lv_font_montserrat_28;
    if (size >= 14) return &lv_font_montserrat_14;
    return &lv_font_montserrat_12;
}

static int scale_position(int value, int offset)
{
    return offset + (int)lroundf((float)value * s_canvas_scale);
}

static int scale_dimension(int value)
{
    int scaled = (int)lroundf((float)value * s_canvas_scale);
    return scaled > 0 ? scaled : 1;
}

static widget_kind_t kind_for_type(const char *type)
{
    if (!strcmp(type, "bar")) return WIDGET_BAR;
    if (!strcmp(type, "arc")) return WIDGET_ARC;
    if (!strcmp(type, "button")) return WIDGET_BUTTON;
    if (!strcmp(type, "image")) return WIDGET_IMAGE;
    if (!strcmp(type, "indicator") || !strcmp(type, "object")) return WIDGET_INDICATOR;
    return WIDGET_LABEL;
}

static void add_binding(lv_obj_t *object, binding_id_t id, widget_kind_t kind)
{
    if (id == BIND_NONE || s_binding_count >= RUNTIME_OBJECT_MAX) return;
    s_bindings[s_binding_count++] = (runtime_binding_t){.object = object, .id = id, .kind = kind};
}

static lv_img_dsc_t *load_image_descriptor(cJSON *definition, const char *asset_key,
                                           const char *format_key, const char *width_key,
                                           const char *height_key)
{
    if (s_image_count >= RUNTIME_IMAGE_MAX) return NULL;
    cJSON *asset = cJSON_GetObjectItemCaseSensitive(definition, asset_key);
    cJSON *format = cJSON_GetObjectItemCaseSensitive(definition, format_key);
    if (!cJSON_IsString(asset) || !cJSON_IsString(format)) return NULL;

    int source_width = json_int(definition, width_key, 0, 1, 2048);
    int source_height = json_int(definition, height_key, 0, 1, 2048);
    lv_img_cf_t color_format;
    size_t bytes_per_pixel;
    if (!strcmp(format->valuestring, "rgb565")) {
        color_format = LV_IMG_CF_TRUE_COLOR;
        bytes_per_pixel = 2;
    } else if (!strcmp(format->valuestring, "rgb565a8")) {
        color_format = LV_IMG_CF_TRUE_COLOR_ALPHA;
        bytes_per_pixel = 3;
    } else {
        return NULL;
    }

    size_t expected_size = (size_t)source_width * (size_t)source_height * bytes_per_pixel;
    if (expected_size == 0 || expected_size > RUNTIME_ASSET_MAX_SIZE) return NULL;
    uint8_t *data = NULL;
    size_t data_size = 0;
    if (theme_storage_read_file(s_package, asset->valuestring, expected_size, &data, &data_size) != ESP_OK ||
        data_size != expected_size) {
        free(data);
        ESP_LOGW(TAG, "Cannot load image asset %s", asset->valuestring);
        return NULL;
    }

    lv_img_dsc_t *descriptor = calloc(1, sizeof(*descriptor));
    if (!descriptor) {
        free(data);
        return NULL;
    }
    descriptor->header.cf = color_format;
    descriptor->header.w = (uint32_t)source_width;
    descriptor->header.h = (uint32_t)source_height;
    descriptor->data_size = (uint32_t)data_size;
    descriptor->data = data;
    s_image_descriptors[s_image_count] = descriptor;
    s_image_data[s_image_count] = data;
    ++s_image_count;
    return descriptor;
}

static lv_obj_t *build_image(lv_obj_t *parent, cJSON *definition)
{
    lv_img_dsc_t *descriptor = load_image_descriptor(definition, "asset", "format",
                                                     "source_width", "source_height");
    if (!descriptor) return NULL;
    lv_obj_t *image = lv_img_create(parent);
    lv_img_set_src(image, descriptor);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

static lv_align_t object_align(cJSON *definition)
{
    cJSON *align = cJSON_GetObjectItemCaseSensitive(definition, "object_align");
    if (!cJSON_IsString(align)) return LV_ALIGN_DEFAULT;
    if (!strcmp(align->valuestring, "center")) return LV_ALIGN_CENTER;
    if (!strcmp(align->valuestring, "top_left")) return LV_ALIGN_TOP_LEFT;
    if (!strcmp(align->valuestring, "top_mid")) return LV_ALIGN_TOP_MID;
    if (!strcmp(align->valuestring, "top_right")) return LV_ALIGN_TOP_RIGHT;
    if (!strcmp(align->valuestring, "left_mid")) return LV_ALIGN_LEFT_MID;
    if (!strcmp(align->valuestring, "right_mid")) return LV_ALIGN_RIGHT_MID;
    if (!strcmp(align->valuestring, "bottom_left")) return LV_ALIGN_BOTTOM_LEFT;
    if (!strcmp(align->valuestring, "bottom_mid")) return LV_ALIGN_BOTTOM_MID;
    if (!strcmp(align->valuestring, "bottom_right")) return LV_ALIGN_BOTTOM_RIGHT;
    return LV_ALIGN_DEFAULT;
}

static void align_to_canvas(lv_obj_t *object, lv_align_t align, int x, int y)
{
    int scaled_x = scale_dimension(abs(x));
    int scaled_y = scale_dimension(abs(y));
    if (x < 0) scaled_x = -scaled_x;
    if (y < 0) scaled_y = -scaled_y;
    switch (align) {
        case LV_ALIGN_TOP_LEFT:
        case LV_ALIGN_LEFT_MID:
        case LV_ALIGN_BOTTOM_LEFT: scaled_x += s_canvas_offset_x; break;
        case LV_ALIGN_TOP_RIGHT:
        case LV_ALIGN_RIGHT_MID:
        case LV_ALIGN_BOTTOM_RIGHT: scaled_x -= s_canvas_offset_x; break;
        default: break;
    }
    switch (align) {
        case LV_ALIGN_TOP_LEFT:
        case LV_ALIGN_TOP_MID:
        case LV_ALIGN_TOP_RIGHT: scaled_y += s_canvas_offset_y; break;
        case LV_ALIGN_BOTTOM_LEFT:
        case LV_ALIGN_BOTTOM_MID:
        case LV_ALIGN_BOTTOM_RIGHT: scaled_y -= s_canvas_offset_y; break;
        default: break;
    }
    lv_obj_align(object, align, scaled_x, scaled_y);
}

static lv_obj_t *build_object(lv_obj_t *parent, cJSON *definition,
                              lv_event_cb_t settings_cb, lv_event_cb_t record_cb)
{
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(definition, "type");
    cJSON *name_item = cJSON_GetObjectItemCaseSensitive(definition, "name");
    const char *type = cJSON_IsString(type_item) ? type_item->valuestring : "label";
    const char *name = cJSON_IsString(name_item) ? name_item->valuestring : "";
    widget_kind_t kind = kind_for_type(type);
    binding_id_t binding = resolve_binding(name);
    lv_obj_t *object = NULL;

    if (kind == WIDGET_IMAGE) {
        object = build_image(parent, definition);
        if (!object) return NULL;
    } else if (kind == WIDGET_LABEL) {
        object = lv_label_create(parent);
        cJSON *text = cJSON_GetObjectItemCaseSensitive(definition, "text");
        lv_label_set_text(object, cJSON_IsString(text) ? text->valuestring : "--");
        lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
        int font_size = json_int(definition, "font_size", 14, 8, 96);
        lv_obj_set_style_text_font(object, font_for_size(scale_dimension(font_size)), LV_PART_MAIN);
        lv_obj_set_style_text_color(object, lv_color_hex(json_color(definition, "color", 0xffffff)), LV_PART_MAIN);
        cJSON *align = cJSON_GetObjectItemCaseSensitive(definition, "align");
        if (cJSON_IsString(align) && !strcmp(align->valuestring, "center"))
            lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        else if (cJSON_IsString(align) && !strcmp(align->valuestring, "right"))
            lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    } else if (kind == WIDGET_BAR) {
        object = lv_bar_create(parent);
        lv_bar_set_range(object, json_int(definition, "min", 0, -10000, 10000),
                         json_int(definition, "max", 100, -10000, 10000));
        lv_obj_set_style_bg_color(object, lv_color_hex(json_color(definition, "track_color", 0x25282d)), LV_PART_MAIN);
        lv_obj_set_style_bg_color(object, lv_color_hex(json_color(definition, "color", 0xe4002b)), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(object, json_int(definition, "track_opa", 255, 0, 255), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(object, json_int(definition, "indicator_opa", 255, 0, 255), LV_PART_INDICATOR);
        int radius = json_int(definition, "radius", 0, 0, 1024);
        lv_obj_set_style_radius(object, radius, LV_PART_MAIN);
        lv_obj_set_style_radius(object, radius, LV_PART_INDICATOR);
        int transform_angle = json_int(definition, "transform_angle", 0, -360, 360);
        if (transform_angle) lv_obj_set_style_transform_angle(object, transform_angle * 10, LV_PART_MAIN);
        cJSON *indicator_asset = cJSON_GetObjectItemCaseSensitive(definition, "indicator_asset");
        if (cJSON_IsString(indicator_asset)) {
            lv_img_dsc_t *descriptor = load_image_descriptor(definition, "indicator_asset",
                                                             "indicator_format", "indicator_source_width",
                                                             "indicator_source_height");
            if (descriptor) lv_obj_set_style_bg_img_src(object, descriptor, LV_PART_INDICATOR);
        }
    } else if (kind == WIDGET_ARC) {
        object = lv_arc_create(parent);
        lv_arc_set_range(object, json_int(definition, "min", 0, -10000, 10000),
                         json_int(definition, "max", 100, -10000, 10000));
        lv_arc_set_rotation(object, json_int(definition, "rotation", 135, 0, 359));
        lv_arc_set_bg_angles(object, 0, json_int(definition, "sweep", 270, 1, 360));
        lv_obj_remove_style(object, NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_color(object, lv_color_hex(json_color(definition, "track_color", 0x25282d)), LV_PART_MAIN);
        lv_obj_set_style_arc_color(object, lv_color_hex(json_color(definition, "color", 0xe4002b)), LV_PART_INDICATOR);
    } else if (kind == WIDGET_BUTTON) {
        object = lv_btn_create(parent);
        lv_obj_set_style_bg_color(object, lv_color_hex(json_color(definition, "background", 0x151619)), LV_PART_MAIN);
        cJSON *text = cJSON_GetObjectItemCaseSensitive(definition, "text");
        lv_obj_t *label = lv_label_create(object);
        const char *default_text = binding == BIND_RECORD_BUTTON ? "REC" : "Settings";
        lv_label_set_text(label, cJSON_IsString(text) ? text->valuestring : default_text);
        lv_obj_center(label);
        if (binding == BIND_SETTINGS_BUTTON && settings_cb) {
            lv_obj_add_event_cb(object, settings_cb, LV_EVENT_SHORT_CLICKED, NULL);
            lv_obj_add_event_cb(object, settings_cb, LV_EVENT_LONG_PRESSED, NULL);
        }
        if (binding == BIND_RECORD_BUTTON && record_cb) lv_obj_add_event_cb(object, record_cb, LV_EVENT_CLICKED, NULL);
    } else {
        object = lv_obj_create(parent);
        lv_obj_set_style_bg_color(object, lv_color_hex(json_color(definition, "color", 0xe4002b)), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    }

    lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_align_t align = object_align(definition);
    bool anchored = align != LV_ALIGN_DEFAULT;
    int x = json_int(definition, "x", 0, anchored ? -s_design_width : 0, s_design_width);
    int y = json_int(definition, "y", 0, anchored ? -s_design_height : 0, s_design_height);
    cJSON *width_item = cJSON_GetObjectItemCaseSensitive(definition, "width");
    cJSON *height_item = cJSON_GetObjectItemCaseSensitive(definition, "height");
    bool width_content = cJSON_IsString(width_item) && !strcmp(width_item->valuestring, "content");
    bool height_content = cJSON_IsString(height_item) && !strcmp(height_item->valuestring, "content");
    int width = json_int(definition, "width", 120, 1, s_design_width);
    int height = json_int(definition, "height", 40, 1, s_design_height);
    if (!anchored && width > s_design_width - x) width = s_design_width - x;
    if (!anchored && height > s_design_height - y) height = s_design_height - y;
    int scaled_x = scale_position(x, s_canvas_offset_x);
    int scaled_y = scale_position(y, s_canvas_offset_y);
    int scaled_width = scale_dimension(width);
    int scaled_height = scale_dimension(height);
    if (kind == WIDGET_IMAGE) {
        const lv_img_dsc_t *descriptor = lv_img_get_src(object);
        int source_width = descriptor->header.w;
        int source_height = descriptor->header.h;
        int zoom;
        cJSON *zoom_item = cJSON_GetObjectItemCaseSensitive(definition, "zoom");
        if (cJSON_IsNumber(zoom_item)) {
            zoom = (int)lroundf((float)zoom_item->valueint * s_canvas_scale);
        } else {
            int zoom_x = (scaled_width * 256) / source_width;
            int zoom_y = (scaled_height * 256) / source_height;
            zoom = zoom_x < zoom_y ? zoom_x : zoom_y;
        }
        if (zoom < 1) zoom = 1;
        if (zoom > 768) zoom = 768;
        int rendered_width = (source_width * zoom) / 256;
        int rendered_height = (source_height * zoom) / 256;
        lv_img_set_zoom(object, (uint16_t)zoom);
        if (anchored) {
            align_to_canvas(object, align, x, y);
        } else {
            lv_obj_set_pos(object, scaled_x + (rendered_width - source_width) / 2,
                           scaled_y + (rendered_height - source_height) / 2);
        }
    } else {
        lv_obj_set_size(object, width_content ? LV_SIZE_CONTENT : scaled_width,
                        height_content ? LV_SIZE_CONTENT : scaled_height);
        if (anchored) align_to_canvas(object, align, x, y);
        else lv_obj_set_pos(object, scaled_x, scaled_y);
    }
    if (kind != WIDGET_BUTTON) lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    if (kind != WIDGET_LABEL) lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    add_binding(object, binding, kind);
    if (name[0] && binding == BIND_NONE && strncmp(name, "dash", 4) == 0)
        ESP_LOGW(TAG, "Unresolved object name: %s", name);
    return object;
}

void runtime_theme_unload(void)
{
    if (s_root) lv_obj_del(s_root);
    for (size_t i = 0; i < s_image_count; ++i) {
        free(s_image_data[i]);
        free(s_image_descriptors[i]);
        s_image_data[i] = NULL;
        s_image_descriptors[i] = NULL;
    }
    s_root = NULL;
    s_package = NULL;
    s_binding_count = 0;
    s_image_count = 0;
}

esp_err_t runtime_theme_load(lv_obj_t *parent, const theme_storage_package_t *package,
                             lv_event_cb_t settings_cb, lv_event_cb_t record_cb)
{
    if (!parent || !package || !package->manifest_valid || !package->layout_path[0]) return ESP_ERR_INVALID_ARG;
    uint8_t *layout_data = NULL;
    size_t layout_size = 0;
    esp_err_t err = theme_storage_read_file(package, package->layout_path, RUNTIME_LAYOUT_MAX_SIZE,
                                            &layout_data, &layout_size);
    if (err != ESP_OK) return err;

    cJSON *layout = cJSON_ParseWithLength((const char *)layout_data, layout_size);
    free(layout_data);
    if (!layout) return ESP_ERR_INVALID_ARG;
    cJSON *objects = cJSON_GetObjectItemCaseSensitive(layout, "objects");
    if (!cJSON_IsArray(objects) || cJSON_GetArraySize(objects) > RUNTIME_OBJECT_MAX) {
        cJSON_Delete(layout);
        return ESP_ERR_INVALID_SIZE;
    }

    runtime_theme_unload();
    s_package = package;
    s_design_width = package->design_width;
    s_design_height = package->design_height;
    float scale_x = (float)TARGET_WIDTH / (float)s_design_width;
    float scale_y = (float)TARGET_HEIGHT / (float)s_design_height;
    s_canvas_scale = scale_x < scale_y ? scale_x : scale_y;
    int scaled_width = scale_dimension(s_design_width);
    int scaled_height = scale_dimension(s_design_height);
    s_canvas_offset_x = (TARGET_WIDTH - scaled_width) / 2;
    s_canvas_offset_y = (TARGET_HEIGHT - scaled_height) / 2;
    s_root = lv_obj_create(parent);
    lv_obj_set_size(s_root, TARGET_WIDTH, TARGET_HEIGHT);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_root, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(json_color(layout, "background", 0x08090a)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    cJSON *definition = NULL;
    cJSON_ArrayForEach(definition, objects) {
        if (cJSON_IsObject(definition)) build_object(s_root, definition, settings_cb, record_cb);
    }
    bool has_settings_button = false;
    bool has_record_button = false;
    for (size_t i = 0; i < s_binding_count; ++i) {
        if (s_bindings[i].id == BIND_SETTINGS_BUTTON) {
            has_settings_button = true;
        }
        if (s_bindings[i].id == BIND_RECORD_BUTTON) has_record_button = true;
    }
    if (!has_settings_button && settings_cb) {
        lv_obj_t *button = lv_btn_create(s_root);
        lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(button, 56, 56);
        lv_obj_set_pos(button, 952, 528);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x151619), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_80, LV_PART_MAIN);
        lv_obj_add_event_cb(button, settings_cb, LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_event_cb(button, settings_cb, LV_EVENT_LONG_PRESSED, NULL);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, LV_SYMBOL_SETTINGS);
        lv_obj_center(label);
        ESP_LOGW(TAG, "Theme has no settings button; added fallback control");
    }
    if (!has_record_button && record_cb) {
        lv_obj_t *button = lv_btn_create(s_root);
        lv_obj_add_flag(button, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(button, 56, 56);
        lv_obj_set_pos(button, 888, 528);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x4a0413), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(0xe4002b), LV_PART_MAIN);
        lv_obj_add_event_cb(button, record_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, "REC");
        lv_obj_center(label);
        ESP_LOGW(TAG, "Theme has no record button; added fallback control");
    }
    cJSON_Delete(layout);
    ESP_LOGI(TAG, "Loaded %s (%ux%u at %.2fx, offset %d,%d) with %u live binding(s)",
             package->display_name, package->design_width, package->design_height,
             (double)s_canvas_scale, s_canvas_offset_x, s_canvas_offset_y, (unsigned)s_binding_count);
    return ESP_OK;
}

lv_obj_t *runtime_theme_get_root(void)
{
    return s_root;
}

static float binding_value(binding_id_t id, const honda_dash_data_t *data)
{
    switch (id) {
        case BIND_RPM: return data->rpm;
        case BIND_SPEED: return data->speed_mph;
        case BIND_GEAR: return data->gear;
        case BIND_ECT: return data->ect_f;
        case BIND_IAT: return data->iat_f;
        case BIND_AFR: return data->afr;
        case BIND_TIMING: return data->timing_deg;
        case BIND_MAP: return data->map_psi;
        case BIND_BATT: return data->batt_v;
        case BIND_TPS: return data->tps_pct;
        case BIND_OIL: return data->oil_psi;
        case BIND_DUTY: return data->duty_pct;
        case BIND_KNOCK: return data->knock_deg;
        case BIND_FUEL: return data->fuel_pct;
        case BIND_ODO: return (float)(dash_config_get_distance_km() ?
                         data->odo_miles * 1.60934 : data->odo_miles);
        default: return 0;
    }
}

static void update_label(runtime_binding_t *binding, const honda_dash_data_t *data)
{
    char text[24];
    float value = binding_value(binding->id, data);
    switch (binding->id) {
        case BIND_GEAR:
            if (data->gear <= 0) snprintf(text, sizeof(text), "N");
            else snprintf(text, sizeof(text), "%d", data->gear);
            break;
        case BIND_AFR:
        case BIND_BATT:
        case BIND_KNOCK: snprintf(text, sizeof(text), "%.1f", value); break;
        case BIND_MAP:
            if (!dash_config_get_pressure_kpa() && value < 0.0f) value *= 2.03602f;
            snprintf(text, sizeof(text), "%.1f", value);
            break;
        case BIND_ODO: snprintf(text, sizeof(text), "%.1f", value); break;
        case BIND_SPEED_UNIT: snprintf(text, sizeof(text), "%s", dash_config_get_speed_kph() ? "KPH" : "MPH"); break;
        case BIND_ECT_UNIT:
        case BIND_IAT_UNIT: snprintf(text, sizeof(text), "%s", dash_config_get_temperature_celsius() ? "C" : "F"); break;
        case BIND_MAP_UNIT:
            snprintf(text, sizeof(text), "%s", dash_config_get_pressure_kpa() ? "kPa" :
                     (data->map_psi < 0.0f ? "inHg" : "PSI"));
            break;
        case BIND_OIL_UNIT: snprintf(text, sizeof(text), "%s", dash_config_get_pressure_kpa() ? "kPa" : "PSI"); break;
        case BIND_FUEL_UNIT:
        case BIND_TPS_UNIT: snprintf(text, sizeof(text), "%%"); break;
        default: snprintf(text, sizeof(text), "%.0f", value); break;
    }
    lv_label_set_text(binding->object, text);
}

void runtime_theme_update(const honda_dash_data_t *data)
{
    if (!s_root || !data) return;
    for (size_t i = 0; i < s_binding_count; ++i) {
        runtime_binding_t *binding = &s_bindings[i];
        if (binding->kind == WIDGET_LABEL) {
            update_label(binding, data);
        } else if (binding->kind == WIDGET_BAR) {
            lv_bar_set_value(binding->object, (int32_t)lroundf(binding_value(binding->id, data)), LV_ANIM_OFF);
        } else if (binding->kind == WIDGET_ARC) {
            lv_arc_set_value(binding->object, (int32_t)lroundf(binding_value(binding->id, data)));
        } else if (binding->kind == WIDGET_INDICATOR) {
            bool visible = false;
            switch (binding->id) {
                case BIND_CEL_INDICATOR: visible = data->cel; break;
                case BIND_VTEC_INDICATOR: visible = data->rpm >= dash_config_get_vtec_rpm(); break;
                case BIND_SHIFT_INDICATOR:
                    visible = dash_config_get_shift_light_enabled() &&
                              data->rpm >= dash_config_get_shift_stage_rpm(2);
                    break;
                case BIND_OIL_INDICATOR: visible = data->oil_valid && data->oil_psi < 15.0f; break;
                case BIND_KNOCK_INDICATOR: visible = data->knock_valid && data->knock_deg > 1.5f; break;
                default: break;
            }
            if (visible) lv_obj_clear_flag(binding->object, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(binding->object, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
