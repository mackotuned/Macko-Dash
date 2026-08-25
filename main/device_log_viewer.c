#include "device_log_viewer.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"

#define LOG_DIRECTORY "/sdcard/MACKODASH/LOGS"
#define LOG_PATH_SIZE 96

typedef struct {
    uint32_t elapsed_ms;
    int rpm;
    float speed_mph;
    int gear;
    float coolant_f;
    float intake_f;
    float afr;
    float timing_deg;
    float map_psi;
    float battery_v;
    float tps_pct;
    float oil_psi;
    int oil_valid;
    float duty_pct;
    int duty_valid;
    float knock_deg;
    int knock_valid;
    int cel;
    float odometer_miles;
    float fuel_pct;
} parsed_row_t;

typedef enum {
    FIELD_RPM,
    FIELD_COOLANT,
    FIELD_INTAKE,
    FIELD_AFR,
    FIELD_TIMING,
    FIELD_BOOST,
    FIELD_BATTERY,
    FIELD_OIL,
    FIELD_KNOCK,
} chart_field_t;

typedef struct {
    size_t count;
    chart_field_t fields[DEVICE_LOG_MAX_SERIES];
    const char *names[DEVICE_LOG_MAX_SERIES];
    const char *units[DEVICE_LOG_MAX_SERIES];
} preset_definition_t;

static const char *TAG = "device_logs";
static const preset_definition_t PRESETS[DEVICE_LOG_PRESET_COUNT] = {
    [DEVICE_LOG_PRESET_AFR_TIMING_BOOST] = {
        3, {FIELD_AFR, FIELD_TIMING, FIELD_BOOST},
        {"AFR", "TIMING", "BOOST / MAP"}, {":1", "deg", "psi"},
    },
    [DEVICE_LOG_PRESET_BOOST_TIMING] = {
        2, {FIELD_BOOST, FIELD_TIMING},
        {"BOOST / MAP", "TIMING"}, {"psi", "deg"},
    },
    [DEVICE_LOG_PRESET_TEMPS_RPM_BOOST] = {
        4, {FIELD_COOLANT, FIELD_INTAKE, FIELD_RPM, FIELD_BOOST},
        {"COOLANT", "INTAKE", "RPM", "BOOST / MAP"}, {"F", "F", "rpm", "psi"},
    },
    [DEVICE_LOG_PRESET_ENGINE_HEALTH] = {
        4, {FIELD_RPM, FIELD_COOLANT, FIELD_OIL, FIELD_BATTERY},
        {"RPM", "COOLANT", "OIL", "BATTERY"}, {"rpm", "F", "psi", "V"},
    },
};

static bool valid_filename(const char *filename)
{
    if (!filename || strlen(filename) != 11 || strncmp(filename, "LOG", 3) != 0 ||
        strcmp(filename + 7, ".CSV") != 0) {
        return false;
    }
    for (size_t index = 3; index < 7; ++index) {
        if (filename[index] < '0' || filename[index] > '9') return false;
    }
    return true;
}

static int file_compare_descending(const void *left, const void *right)
{
    const device_log_file_t *a = left;
    const device_log_file_t *b = right;
    return strcmp(b->filename, a->filename);
}

size_t device_log_list(device_log_file_t *files, size_t capacity)
{
    if (!files || capacity == 0) return 0;
    DIR *directory = opendir(LOG_DIRECTORY);
    if (!directory) return 0;
    size_t count = 0;
    struct dirent *entry;
    while (count < capacity && (entry = readdir(directory)) != NULL) {
        if (!valid_filename(entry->d_name)) continue;
        char path[LOG_PATH_SIZE];
        struct stat info;
        snprintf(path, sizeof(path), "%s/%s", LOG_DIRECTORY, entry->d_name);
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        snprintf(files[count].filename, sizeof(files[count].filename), "%s", entry->d_name);
        files[count].size_bytes = (uint32_t)info.st_size;
        ++count;
    }
    closedir(directory);
    qsort(files, count, sizeof(files[0]), file_compare_descending);
    return count;
}

static bool parse_row(const char *line, parsed_row_t *row)
{
    return sscanf(line,
                  "%" SCNu32 ",%d,%f,%d,%f,%f,%f,%f,%f,%f,%f,%f,%d,%f,%d,%f,%d,%d,%f,%f",
                  &row->elapsed_ms, &row->rpm, &row->speed_mph, &row->gear,
                  &row->coolant_f, &row->intake_f, &row->afr, &row->timing_deg,
                  &row->map_psi, &row->battery_v, &row->tps_pct, &row->oil_psi,
                  &row->oil_valid, &row->duty_pct, &row->duty_valid, &row->knock_deg,
                  &row->knock_valid, &row->cel, &row->odometer_miles, &row->fuel_pct) == 20;
}

static int16_t field_value(const parsed_row_t *row, chart_field_t field)
{
    switch (field) {
        case FIELD_RPM: return row->rpm;
        case FIELD_COOLANT: return (int16_t)(row->coolant_f * 10.0f);
        case FIELD_INTAKE: return (int16_t)(row->intake_f * 10.0f);
        case FIELD_AFR: return (int16_t)(row->afr * 100.0f);
        case FIELD_TIMING: return (int16_t)(row->timing_deg * 10.0f);
        case FIELD_BOOST: return (int16_t)(row->map_psi * 10.0f);
        case FIELD_BATTERY: return (int16_t)(row->battery_v * 100.0f);
        case FIELD_OIL: return row->oil_valid ? (int16_t)(row->oil_psi * 10.0f) : INT16_MIN;
        case FIELD_KNOCK: return row->knock_valid ? (int16_t)(row->knock_deg * 10.0f) : INT16_MIN;
        default: return 0;
    }
}

esp_err_t device_log_load_chart(const char *filename, device_log_preset_t preset,
                                device_log_chart_t *chart)
{
    if (!valid_filename(filename) || !chart || preset >= DEVICE_LOG_PRESET_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[LOG_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/%s", LOG_DIRECTORY, filename);
    FILE *file = fopen(path, "r");
    if (!file) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;

    char line[384];
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t rows = 0;
    while (fgets(line, sizeof(line), file)) ++rows;
    if (rows == 0) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(file);
    fgets(line, sizeof(line), file);

    memset(chart, 0, sizeof(*chart));
    snprintf(chart->filename, sizeof(chart->filename), "%s", filename);
    chart->source_rows = rows;
    const preset_definition_t *definition = &PRESETS[preset];
    chart->series_count = definition->count;
    for (size_t series = 0; series < definition->count; ++series) {
        chart->series_names[series] = definition->names[series];
        chart->series_units[series] = definition->units[series];
        chart->minimums[series] = INT32_MAX;
        chart->maximums[series] = INT32_MIN;
    }

    size_t source_index = 0;
    size_t next_sample = 0;
    size_t point = 0;
    parsed_row_t row;
    while (fgets(line, sizeof(line), file)) {
        if (!parse_row(line, &row)) {
            ++source_index;
            continue;
        }
        bool select = rows <= DEVICE_LOG_MAX_POINTS || source_index >= next_sample || source_index + 1 == rows;
        if (select && point < DEVICE_LOG_MAX_POINTS) {
            for (size_t series = 0; series < definition->count; ++series) {
                int16_t value = field_value(&row, definition->fields[series]);
                chart->values[series][point] = value;
                if (value != INT16_MIN) {
                    if (value < chart->minimums[series]) chart->minimums[series] = value;
                    if (value > chart->maximums[series]) chart->maximums[series] = value;
                }
            }
            chart->duration_ms = row.elapsed_ms;
            ++point;
            next_sample = (point * rows) / DEVICE_LOG_MAX_POINTS;
        }
        ++source_index;
    }
    fclose(file);
    chart->point_count = point;
    for (size_t series = 0; series < definition->count; ++series) {
        if (chart->minimums[series] == INT32_MAX) {
            chart->minimums[series] = 0;
            chart->maximums[series] = 1;
        } else if (chart->minimums[series] == chart->maximums[series]) {
            --chart->minimums[series];
            ++chart->maximums[series];
        }
    }
    ESP_LOGI(TAG, "Loaded %s: %u rows -> %u points", filename, (unsigned)rows, (unsigned)point);
    return point > 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}