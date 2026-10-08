#include "protocol_loader.h"
#include "protocol_list.h"
#include "canbus.h"
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
    if (!strcmp(name, "oil_temp") || !strcmp(name, "gearbox_oil_temp") || !strcmp(name, "diff_oil_temp")) return &can_data.oil_temp;

    if (!strcmp(name, "battery_voltage")) return &can_data.battery_voltage;
    if (!strcmp(name, "oil_pressure")) return &can_data.oil_pressure;
    if (!strcmp(name, "fuel_pressure") || !strcmp(name, "fuel_pressure_diff")) return &can_data.fuel_pressure;

    if (!strcmp(name, "lambda") || !strcmp(name, "lambda1") || !strcmp(name, "lambda_a") || !strcmp(name, "lambda_avg") || !strcmp(name, "wb1")) return &can_data.air_fuel_ratio;
    if (!strcmp(name, "map")) return &can_data.boost;

    if (!strcmp(name, "fuel_comp") || !strcmp(name, "total_fuel_corr")) return &can_data.fuel_comp;

    if (!strcmp(name, "gear")) return &can_data.gear;
    if (!strcmp(name, "tps") || !strcmp(name, "tps_pedal")) return &can_data.tps;
    if (!strcmp(name, "ign_angle")) return &can_data.ign_angle;

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

    memset(proto, 0, sizeof(*proto));
    snprintf(proto->name, sizeof(proto->name), "%s", name->valuestring);
    proto->bitrate = bitrate->valueint;

    int frame_count = cJSON_GetArraySize(frames);
    proto->frame_count = frame_count > MAX_FRAMES ? MAX_FRAMES : frame_count;

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
        proto->frames[i].signal_count = sig_count > MAX_SIGNALS ? MAX_SIGNALS : sig_count;

        for (int s = 0; s < proto->frames[i].signal_count; s++){
            cJSON *sig = cJSON_GetArrayItem(signals, s);
            if (!cJSON_IsObject(sig)) goto invalid_protocol;

            can_signal_t *signal = &proto->frames[i].signals[s];

            cJSON *sig_name = cJSON_GetObjectItem(sig, "name");
            cJSON *offset = cJSON_GetObjectItem(sig, "offset");
            cJSON *len = cJSON_GetObjectItem(sig, "len");
            cJSON *scale = cJSON_GetObjectItem(sig, "scale");
            cJSON *offset_val = cJSON_GetObjectItem(sig, "offset_val");
            cJSON *endian = cJSON_GetObjectItem(sig, "endian");

            if (!cJSON_IsString(sig_name) ||
                    (offset && !cJSON_IsNumber(offset)) ||
                    (len && !cJSON_IsNumber(len)) ||
                    (scale && !cJSON_IsNumber(scale)) ||
                    (offset_val && !cJSON_IsNumber(offset_val)) ||
                    (endian && !cJSON_IsString(endian))) {
                goto invalid_protocol;
            }

                int signal_offset = offset ? offset->valueint : 0;
                int signal_length = len ? len->valueint : 1;
                if ((offset && offset->valuedouble != (double)signal_offset) ||
                    (len && len->valuedouble != (double)signal_length) ||
                    signal_offset < 0 || signal_offset >= 8 ||
                    (signal_length != 1 && signal_length != 2) ||
                    signal_offset + signal_length > 8) {
                goto invalid_protocol;
            }

                signal->offset = (uint8_t)signal_offset;
                signal->len = (uint8_t)signal_length;
            signal->target = signal_name_to_ptr(sig_name->valuestring);
            signal->scale = scale ? scale->valuedouble : 1.0f;
            signal->offset_val = offset_val ? offset_val->valuedouble : 0;

            if (endian && !strcmp(endian->valuestring, "little"))
                signal->endian = ENDIAN_LITTLE;
            else
                signal->endian = ENDIAN_BIG;
        }
    }

    protocol_count++;
    cJSON_Delete(root);
    return;

invalid_protocol:
    ESP_LOGW(TAG, "Skipping invalid CAN protocol JSON");
    memset(proto, 0, sizeof(*proto));
    cJSON_Delete(root);
}

void protocol_loader_init(void){
    ESP_LOGI(TAG, "Loading CAN protocols");

    protocol_count = 0;
    active_protocol = NULL;
    detection_done = false;

    memset(frame_lookup, 0, sizeof(frame_lookup));
    memset(protocols, 0, sizeof(protocols));
    memset(protocol_hits, 0, sizeof(protocol_hits));

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

void protocol_detect(uint32_t id){
    if (detection_done)
        return;

    for (int p = 0; p < protocol_count; p++){
        can_protocol_t *proto = &protocols[p];

        for (int f = 0; f < proto->frame_count; f++){
            if (proto->frames[f].id == id){
                protocol_hits[p]++;

                if (protocol_hits[p] >= 2){
                    activate_protocol(proto);
                    detection_done = true;

                    ESP_LOGI(TAG, "Detected CAN protocol: %s", proto->name);

                    return;
                }
            }
        }
    }
}