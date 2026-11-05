#include "protocol_loader.h"
#include "protocol_list.h"
#include "canbus.h"
#include "ecu_channels.h"
#include "dash_config.h"

#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "PROTO";

can_frame_def_t *frame_lookup[CAN_ID_MAX];
can_protocol_t protocols[MAX_PROTOCOLS];
static int protocol_hits[MAX_PROTOCOLS];

int protocol_count = 0;
can_protocol_t *active_protocol = NULL;

extern volatile can_dash_data_t can_data;

static bool detection_done = false;

static volatile float* signal_name_to_ptr(const char *name){
    if (!strcmp(name, "rpm")) return &can_data.rpm;
    if (!strcmp(name, "speed")) return &can_data.speed;

    if (!strcmp(name, "coolant_temp")) return &can_data.coolant_temp;
    if (!strcmp(name, "air_temp") || !strcmp(name, "iat") || !strcmp(name, "charge_temp")) return &can_data.air_temp;
    if (!strcmp(name, "oil_temp")) return &can_data.oil_temp;

    if (!strcmp(name, "battery_voltage")) return &can_data.battery_voltage;
    if (!strcmp(name, "oil_pressure")) return &can_data.oil_pressure;
    if (!strcmp(name, "fuel_pressure") || !strcmp(name, "fuel_pressure_diff")) return &can_data.fuel_pressure;

    if (!strcmp(name, "lambda") || !strcmp(name, "lambda1") || !strcmp(name, "lambda_a") || !strcmp(name, "lambda_avg") || !strcmp(name, "wb1")) return &can_data.air_fuel_ratio;
    if (!strcmp(name, "map")) return &can_data.boost;

    if (!strcmp(name, "fuel_comp") || !strcmp(name, "total_fuel_corr")) return &can_data.fuel_comp;

    if (!strcmp(name, "gear")) return &can_data.gear;
    if (!strcmp(name, "tps") || !strcmp(name, "tps_pedal")) return &can_data.tps;
    if (!strcmp(name, "ign_angle")) return &can_data.ign_angle;
    if (!strcmp(name, "inj_duty") || !strcmp(name, "inj_stage1_duty")) return &can_data.inj_duty;
    if (!strcmp(name, "check_engine")) return &can_data.cel;

    if (!strcmp(name, "fuel_level") || !strcmp(name, "virtual_fuel_tank")) return &can_data.fuel_level;
    if (!strncmp(name, "analog", 6) && name[6] >= '0' && name[6] <= '7' && name[7] == '\0') {
        return &can_data.analog_inputs[name[6] - '0'];
    }

    return NULL;
}

static void activate_protocol(can_protocol_t *proto){
    memset(frame_lookup, 0, sizeof(frame_lookup));

    for (int f = 0; f < proto->frame_count; f++){
        uint32_t frame_id = proto->frames[f].id;

        if (frame_id < CAN_ID_MAX)
            frame_lookup[frame_id] = &proto->frames[f];
    }

    active_protocol = proto;
}

static void free_protocol(can_protocol_t *proto){
    for (int f = 0; f < MAX_FRAMES; f++){
        free(proto->frames[f].signals);
        proto->frames[f].signals = NULL;
    }
    memset(proto, 0, sizeof(*proto));
}

static bool json_whole_number(const cJSON *item, int *out){
    if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint) return false;
    *out = item->valueint;
    return true;
}

static void load_protocol_from_json(const char *json){
    cJSON *root = cJSON_Parse(json);

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }

    if (protocol_count >= MAX_PROTOCOLS){
        cJSON_Delete(root);
        return;
    }

    can_protocol_t *proto = &protocols[protocol_count];

    cJSON *name = cJSON_GetObjectItem(root, "name");
    cJSON *frames = cJSON_GetObjectItem(root, "frames");
    cJSON *bitrate = cJSON_GetObjectItem(root, "bitrate");

    if (!cJSON_IsString(name) || !name->valuestring[0] ||
            strlen(name->valuestring) >= sizeof(proto->name) ||
            !cJSON_IsArray(frames) || !cJSON_IsNumber(bitrate)) {
        goto invalid_protocol;
    }

    free_protocol(proto);
    snprintf(proto->name, sizeof(proto->name), "%s", name->valuestring);
    proto->bitrate = bitrate->valueint;

    int frame_count = cJSON_GetArraySize(frames);
    if (frame_count > MAX_FRAMES) {
        ESP_LOGW(TAG, "%s: %d frames, only %d used", proto->name, frame_count, MAX_FRAMES);
        frame_count = MAX_FRAMES;
    }
    proto->frame_count = frame_count;

    for (int i = 0; i < proto->frame_count; i++){
        cJSON *frame = cJSON_GetArrayItem(frames, i);
        if (!cJSON_IsObject(frame)) goto invalid_protocol;

        cJSON *id = cJSON_GetObjectItem(frame, "id");
        cJSON *signals = cJSON_GetObjectItem(frame, "signals");

        if (!cJSON_IsArray(signals)) goto invalid_protocol;
        if (cJSON_IsString(id)) {
            char *end = NULL;
            unsigned long parsed_id = strtoul(id->valuestring, &end, 0);
            if (!end || *end != '\0' || parsed_id >= CAN_ID_MAX) goto invalid_protocol;
            proto->frames[i].id = (uint32_t)parsed_id;
        } else if (cJSON_IsNumber(id) && id->valuedouble >= 0 && id->valuedouble < CAN_ID_MAX &&
                   id->valuedouble == (double)id->valueint) {
            proto->frames[i].id = id->valueint;
        } else {
            goto invalid_protocol;
        }

        int sig_count = cJSON_GetArraySize(signals);
        if (sig_count > MAX_SIGNALS) {
            ESP_LOGW(TAG, "%s: frame 0x%03lx has %d signals, only %d used", proto->name,
                     (unsigned long)proto->frames[i].id, sig_count, MAX_SIGNALS);
            sig_count = MAX_SIGNALS;
        }
        if (sig_count > 0) {
            proto->frames[i].signals = calloc((size_t)sig_count, sizeof(can_signal_t));
            if (!proto->frames[i].signals) goto invalid_protocol;
        }
        proto->frames[i].signal_count = sig_count;

        for (int s = 0; s < sig_count; s++){
            cJSON *sig = cJSON_GetArrayItem(signals, s);
            if (!cJSON_IsObject(sig)) goto invalid_protocol;

            can_signal_t *signal = &proto->frames[i].signals[s];

            cJSON *sig_name = cJSON_GetObjectItem(sig, "name");
            cJSON *offset = cJSON_GetObjectItem(sig, "offset");
            cJSON *len = cJSON_GetObjectItem(sig, "len");
            cJSON *scale = cJSON_GetObjectItem(sig, "scale");
            cJSON *offset_val = cJSON_GetObjectItem(sig, "offset_val");
            cJSON *endian = cJSON_GetObjectItem(sig, "endian");
            cJSON *is_signed = cJSON_GetObjectItem(sig, "signed");
            cJSON *bit = cJSON_GetObjectItem(sig, "bit");
            cJSON *bits = cJSON_GetObjectItem(sig, "bits");

            if (!cJSON_IsString(sig_name) ||
                    (is_signed && !cJSON_IsBool(is_signed)) ||
                    (offset && !cJSON_IsNumber(offset)) ||
                    (len && !cJSON_IsNumber(len)) ||
                    (bit && !cJSON_IsNumber(bit)) ||
                    (bits && !cJSON_IsNumber(bits)) ||
                    (scale && !cJSON_IsNumber(scale)) ||
                    (offset_val && !cJSON_IsNumber(offset_val)) ||
                    (endian && !cJSON_IsString(endian))) {
                goto invalid_protocol;
            }

            int signal_offset = 0;
            int signal_length = 1;
            int signal_bit = 0;
            int signal_bits = 0;
            if ((offset && !json_whole_number(offset, &signal_offset)) ||
                    (len && !json_whole_number(len, &signal_length)) ||
                    (bit && !json_whole_number(bit, &signal_bit)) ||
                    (bits && !json_whole_number(bits, &signal_bits))) {
                goto invalid_protocol;
            }
            /* "bit" alone means a single on/off flag */
            if (bit && !bits) signal_bits = 1;

            if (signal_offset < 0 || signal_offset >= 8 ||
                    (signal_length != 1 && signal_length != 2 && signal_length != 4) ||
                    signal_offset + signal_length > 8 ||
                    signal_bit < 0 || signal_bits < 0 ||
                    signal_bit + signal_bits > signal_length * 8) {
                goto invalid_protocol;
            }

            signal->offset = (uint8_t)signal_offset;
            signal->len = (uint8_t)signal_length;
            signal->bit = (uint8_t)signal_bit;
            signal->bits = (uint8_t)signal_bits;
            signal->target = signal_name_to_ptr(sig_name->valuestring);
            signal->ecu_channel = (int16_t)ecu_channel_find(sig_name->valuestring);
            signal->scale = scale ? scale->valuedouble : 1.0f;
            signal->offset_val = offset_val ? offset_val->valuedouble : 0;
            signal->is_signed = cJSON_IsTrue(is_signed) ? 1 : 0;

            if (endian && !strcmp(endian->valuestring, "little"))
                signal->endian = ENDIAN_LITTLE;
            else
                signal->endian = ENDIAN_BIG;

            if (!signal->target && signal->ecu_channel < 0)
                ESP_LOGD(TAG, "%s: signal '%s' has no dashboard destination",
                         proto->name, sig_name->valuestring);
        }
    }

    protocol_count++;
    cJSON_Delete(root);
    return;

invalid_protocol:
    ESP_LOGW(TAG, "Skipping invalid CAN protocol JSON");
    free_protocol(proto);
    cJSON_Delete(root);
}

void protocol_loader_init(void){
    ESP_LOGI(TAG, "Loading CAN protocols");

    active_protocol = NULL;
    detection_done = false;
    memset(frame_lookup, 0, sizeof(frame_lookup));

    for (int p = 0; p < MAX_PROTOCOLS; p++)
        free_protocol(&protocols[p]);
    protocol_count = 0;
    memset(protocol_hits, 0, sizeof(protocol_hits));
    ecu_channel_reset();

    for (int i = 0; i < protocol_json_count; i++)
        load_protocol_from_json(protocol_json_list[i]);

    const char *forced_name = dash_config_get_can_protocol();

    if (forced_name && forced_name[0] != '\0') {
        for (int p = 0; p < protocol_count; p++) {
            if (!strcmp(protocols[p].name, forced_name)) {
                activate_protocol(&protocols[p]);
                detection_done = true;
                ESP_LOGI(TAG, "Forced CAN protocol: %s", protocols[p].name);
                break;
            }
        }

        if (!active_protocol) {
            ESP_LOGW(TAG, "Forced protocol '%s' not found; falling back to auto-detect", forced_name);
        }
    } else {
        ESP_LOGI(TAG, "CAN protocol set to auto-detect");
    }

    ESP_LOGI(TAG, "Loaded %d CAN protocols", protocol_count);
}

static bool protocol_has_id(const can_protocol_t *proto, uint32_t id){
    for (int f = 0; f < proto->frame_count; f++){
        if (proto->frames[f].id == id) return true;
    }
    return false;
}

void protocol_detect(uint32_t id){
    if (detection_done)
        return;

    /* IDs that several protocols use (e.g. Haltech 0x3E8-0x3EB and Link
       0x3E8-0x3EB) can't tell protocols apart, so they don't count. */
    int owner = -1;
    for (int p = 0; p < protocol_count; p++){
        if (!protocol_has_id(&protocols[p], id)) continue;
        if (owner >= 0) return;
        owner = p;
    }
    if (owner < 0)
        return;

    protocol_hits[owner]++;

    if (protocol_hits[owner] >= 2){
        activate_protocol(&protocols[owner]);
        detection_done = true;

        ESP_LOGI(TAG, "Detected CAN protocol: %s", protocols[owner].name);
    }
}
