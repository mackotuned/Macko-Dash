#include "achievement_system.h"

#include "dash_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#define ACHIEVEMENT_NAMESPACE "achievements"
#define ACHIEVEMENT_KEY       "unlocked"
#define ACHIEVEMENT_COUNTS_KEY "counts"
#define ACHIEVEMENT_RECORDS_KEY "records"
#define ACHIEVEMENT_RECORDED_KEY "recorded"
#define ACHIEVEMENT_HOLD_US   800000
#define ACHIEVEMENT_ACTIVE_SAVE_US 30000000

typedef enum {
    ACHIEVEMENT_HIGH_RPM = 0,
    ACHIEVEMENT_TRIPLE_DIGIT,
    ACHIEVEMENT_VTEC,
    ACHIEVEMENT_HIGH_BOOST,
    ACHIEVEMENT_HIGH_COOLANT,
    ACHIEVEMENT_HIGH_INTAKE,
    ACHIEVEMENT_LOW_OIL,
    ACHIEVEMENT_HIGH_DUTY,
    ACHIEVEMENT_KNOCK,
    ACHIEVEMENT_CEL,
    ACHIEVEMENT_COUNT,
} achievement_id_t;

static const char *const ACHIEVEMENT_NAMES[ACHIEVEMENT_COUNT] = {
    "High RPM - 9,500 RPM",
    "Triple Digit - 100 MPH",
    "VTEC Engaged",
    "High Boost",
    "High Coolant Temp",
    "Heat Soaked",
    "Low Oil Pressure",
    "High Injector Duty",
    "Knock Limit",
    "Check Engine",
};

static const char *const ACHIEVEMENT_UNITS[ACHIEVEMENT_COUNT] = {
    "RPM", "MPH", "RPM", "PSI", "F", "F", "PSI", "%", "deg", "",
};

static const char *TAG = "ACHIEVEMENTS";
static uint32_t s_unlocked;
static uint32_t s_recorded;
static uint32_t s_hit_counts[ACHIEVEMENT_COUNT];
static float s_records[ACHIEVEMENT_COUNT];
static int64_t s_condition_since[ACHIEVEMENT_COUNT];
static bool s_event_active[ACHIEVEMENT_COUNT];
static bool s_records_dirty;
static int64_t s_last_save_us;

static void achievement_save(void)
{
    nvs_handle_t handle;
    if (nvs_open(ACHIEVEMENT_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u32(handle, ACHIEVEMENT_KEY, s_unlocked);
    nvs_set_u32(handle, ACHIEVEMENT_RECORDED_KEY, s_recorded);
    nvs_set_blob(handle, ACHIEVEMENT_COUNTS_KEY, s_hit_counts, sizeof(s_hit_counts));
    nvs_set_blob(handle, ACHIEVEMENT_RECORDS_KEY, s_records, sizeof(s_records));
    nvs_commit(handle);
    nvs_close(handle);
    s_records_dirty = false;
    s_last_save_us = esp_timer_get_time();
}

static float achievement_measurement(achievement_id_t id, const honda_dash_data_t *data)
{
    switch (id) {
        case ACHIEVEMENT_HIGH_RPM:
        case ACHIEVEMENT_VTEC:
            return data->rpm;
        case ACHIEVEMENT_TRIPLE_DIGIT:
            return data->speed_mph;
        case ACHIEVEMENT_HIGH_BOOST:
            return data->map_psi;
        case ACHIEVEMENT_HIGH_COOLANT:
            return data->ect_f;
        case ACHIEVEMENT_HIGH_INTAKE:
            return data->iat_f;
        case ACHIEVEMENT_LOW_OIL:
            return data->oil_psi;
        case ACHIEVEMENT_HIGH_DUTY:
            return data->duty_pct;
        case ACHIEVEMENT_KNOCK:
            return data->knock_deg;
        default:
            return 0.0f;
    }
}

static void achievement_update_record(achievement_id_t id, const honda_dash_data_t *data)
{
    if (id == ACHIEVEMENT_CEL) return;
    uint32_t bit = 1U << id;
    float value = achievement_measurement(id, data);
    bool better = !(s_recorded & bit) ||
                  (id == ACHIEVEMENT_LOW_OIL ? value < s_records[id] : value > s_records[id]);
    if (!better) return;
    s_records[id] = value;
    s_recorded |= bit;
    s_records_dirty = true;
}

static bool achievement_condition(achievement_id_t id,
                                  const honda_dash_data_t *data,
                                  bool oil_pressure_live)
{
    switch (id) {
        case ACHIEVEMENT_HIGH_RPM:
            return data->rpm >= 9500;
        case ACHIEVEMENT_TRIPLE_DIGIT:
            return data->speed_mph >= 100.0f;
        case ACHIEVEMENT_VTEC:
            return data->rpm >= dash_config_get_vtec_rpm() && data->tps_pct >= 20.0f;
        case ACHIEVEMENT_HIGH_BOOST:
            return data->map_psi >=
                   dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_MAP_HIGH) / 10.0f;
        case ACHIEVEMENT_HIGH_COOLANT:
            return data->ect_f >=
                   dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_ECT_HIGH) / 10.0f;
        case ACHIEVEMENT_HIGH_INTAKE:
            return data->iat_f >=
                   dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_IAT_HIGH) / 10.0f;
        case ACHIEVEMENT_LOW_OIL:
            return data->rpm >= 800 && data->oil_valid && oil_pressure_live &&
                   data->oil_psi <
                   dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_OIL_LOW) / 10.0f;
        case ACHIEVEMENT_HIGH_DUTY:
            return data->duty_valid && data->duty_pct >=
                   dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_DUTY_HIGH) / 10.0f;
        case ACHIEVEMENT_KNOCK:
            return data->knock_valid && data->knock_deg >=
                   dash_config_get_threshold_tenths(DASH_CONFIG_THRESHOLD_KNOCK_RED) / 10.0f;
        case ACHIEVEMENT_CEL:
            return data->cel;
        default:
            return false;
    }
}

void achievement_system_init(void)
{
    nvs_handle_t handle;
    if (nvs_open(ACHIEVEMENT_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    nvs_get_u32(handle, ACHIEVEMENT_KEY, &s_unlocked);
    nvs_get_u32(handle, ACHIEVEMENT_RECORDED_KEY, &s_recorded);
    size_t counts_size = sizeof(s_hit_counts);
    size_t records_size = sizeof(s_records);
    nvs_get_blob(handle, ACHIEVEMENT_COUNTS_KEY, s_hit_counts, &counts_size);
    nvs_get_blob(handle, ACHIEVEMENT_RECORDS_KEY, s_records, &records_size);
    nvs_close(handle);
    s_unlocked &= (1U << ACHIEVEMENT_COUNT) - 1U;
    s_recorded &= (1U << ACHIEVEMENT_COUNT) - 1U;

    bool migrated = false;
    for (int id = 0; id < ACHIEVEMENT_COUNT; ++id) {
        if ((s_unlocked & (1U << id)) && s_hit_counts[id] == 0) {
            s_hit_counts[id] = 1;
            migrated = true;
        }
    }
    if (migrated) achievement_save();
}

const char *achievement_system_update(const honda_dash_data_t *data,
                                      bool drivetrain_live,
                                      bool oil_pressure_live)
{
    if (!data || !drivetrain_live) return NULL;

    int64_t now_us = esp_timer_get_time();
    uint32_t newly_unlocked = 0;
    bool new_hit = false;
    const char *announcement = NULL;
    for (int id = 0; id < ACHIEVEMENT_COUNT; ++id) {
        uint32_t bit = 1U << id;
        if (!achievement_condition((achievement_id_t)id, data, oil_pressure_live)) {
            s_condition_since[id] = 0;
            if (s_event_active[id]) {
                s_event_active[id] = false;
                if (s_records_dirty) achievement_save();
            }
            continue;
        }
        if (s_event_active[id]) {
            achievement_update_record((achievement_id_t)id, data);
            continue;
        }
        if (s_condition_since[id] == 0) {
            s_condition_since[id] = now_us;
            continue;
        }
        if (now_us - s_condition_since[id] < ACHIEVEMENT_HOLD_US) continue;

        newly_unlocked |= bit;
        s_event_active[id] = true;
        if (s_hit_counts[id] < UINT32_MAX) s_hit_counts[id]++;
        achievement_update_record((achievement_id_t)id, data);
        new_hit = true;
        if (!(s_unlocked & bit) && !announcement) announcement = ACHIEVEMENT_NAMES[id];
        ESP_LOGI(TAG, "Hit: %s (%lu)", ACHIEVEMENT_NAMES[id],
                 (unsigned long)s_hit_counts[id]);
    }

    if (new_hit) {
        s_unlocked |= newly_unlocked;
        achievement_save();
    } else if (s_records_dirty && now_us - s_last_save_us >= ACHIEVEMENT_ACTIVE_SAVE_US) {
        achievement_save();
    }
    return announcement;
}

uint8_t achievement_system_get_count(void)
{
    uint8_t count = 0;
    uint32_t value = s_unlocked;
    while (value) {
        count += value & 1U;
        value >>= 1;
    }
    return count;
}

bool achievement_system_get_entry(uint8_t index, achievement_system_entry_t *entry)
{
    if (!entry || index >= ACHIEVEMENT_COUNT) return false;
    entry->name = ACHIEVEMENT_NAMES[index];
    entry->unit = ACHIEVEMENT_UNITS[index];
    entry->hit_count = s_hit_counts[index];
    entry->record_value = s_records[index];
    entry->record_valid = (s_recorded & (1U << index)) != 0;
    entry->lower_is_better = index == ACHIEVEMENT_LOW_OIL;
    return true;
}