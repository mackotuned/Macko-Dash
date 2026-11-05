#include "ecu_channels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dash_config.h"
#include "esp_timer.h"

/* Slowest Haltech frames are 5 Hz; allow a few missed frames before N/A. */
#define ECU_CHANNEL_STALE_US 2000000

typedef struct {
    const char *signal;
    const char *label;
    ecu_unit_t unit;
    uint8_t dp;
    float lo;
    float hi;
} ecu_channel_def_t;

#define ECU_CHANNEL_DEF_ENTRY(id, signal, label, unit, dp, lo, hi) \
    [id] = {signal, label, unit, dp, (float)(lo), (float)(hi)},
static const ecu_channel_def_t CHANNELS[ECU_CHANNEL_COUNT] = {
    ECU_CHANNEL_LIST(ECU_CHANNEL_DEF_ENTRY)
};
#undef ECU_CHANNEL_DEF_ENTRY

static volatile float s_values[ECU_CHANNEL_COUNT];
static volatile int64_t s_last_us[ECU_CHANNEL_COUNT];

static bool valid_channel(int channel)
{
    return channel >= 0 && channel < ECU_CHANNEL_COUNT;
}

int ecu_channel_find(const char *signal_name)
{
    if (!signal_name) return -1;
    for (int i = 0; i < ECU_CHANNEL_COUNT; ++i) {
        if (strcmp(CHANNELS[i].signal, signal_name) == 0) return i;
    }
    return -1;
}

void ecu_channel_set(int channel, float value)
{
    if (!valid_channel(channel) || !isfinite(value)) return;
    s_values[channel] = value;
    s_last_us[channel] = esp_timer_get_time();
}

void ecu_channel_reset(void)
{
    for (int i = 0; i < ECU_CHANNEL_COUNT; ++i) {
        s_values[i] = 0.0f;
        s_last_us[i] = 0;
    }
}

bool ecu_channel_get(int channel, float *value)
{
    if (!valid_channel(channel)) return false;
    int64_t last = s_last_us[channel];
    float current = s_values[channel];
    if (value) *value = current;
    return last != 0 && (esp_timer_get_time() - last) <= ECU_CHANNEL_STALE_US;
}

const char *ecu_channel_signal(int channel)
{
    return valid_channel(channel) ? CHANNELS[channel].signal : "";
}

const char *ecu_channel_label(int channel)
{
    return valid_channel(channel) ? CHANNELS[channel].label : "";
}

ecu_unit_t ecu_channel_unit_kind(int channel)
{
    return valid_channel(channel) ? CHANNELS[channel].unit : ECU_UNIT_NONE;
}

const char *ecu_channel_unit(int channel)
{
    switch (ecu_channel_unit_kind(channel)) {
        case ECU_UNIT_TEMP: return dash_config_get_temperature_celsius() ? "DEG C" : "DEG F";
        case ECU_UNIT_PRESSURE: return dash_config_get_pressure_kpa() ? "KPA" : "PSI";
        case ECU_UNIT_SPEED: return dash_config_get_speed_kph() ? "KPH" : "MPH";
        case ECU_UNIT_PERCENT: return "%";
        case ECU_UNIT_DEG: return "DEG";
        case ECU_UNIT_LAMBDA: return "LAMBDA";
        case ECU_UNIT_RPM: return "RPM";
        case ECU_UNIT_VOLT: return "V";
        case ECU_UNIT_G: return "G";
        case ECU_UNIT_FLOW: return "CC/MIN";
        case ECU_UNIT_VOLUME: return dash_config_get_distance_km() ? "L" : "GAL";
        case ECU_UNIT_DB: return "DB";
        case ECU_UNIT_SECONDS: return "SEC";
        case ECU_UNIT_PPM: return "PPM";
        case ECU_UNIT_GM3: return "G/M3";
        default: return "";
    }
}

float ecu_channel_display_value(int channel, float canonical)
{
    switch (ecu_channel_unit_kind(channel)) {
        case ECU_UNIT_TEMP:
            return dash_config_get_temperature_celsius() ? (canonical - 32.0f) * (5.0f / 9.0f) : canonical;
        case ECU_UNIT_PRESSURE:
            return dash_config_get_pressure_kpa() ? canonical * 6.89476f : canonical;
        case ECU_UNIT_SPEED:
            return dash_config_get_speed_kph() ? canonical : canonical * 0.621371f;
        case ECU_UNIT_VOLUME:
            return dash_config_get_distance_km() ? canonical : canonical * 0.264172f;
        default:
            return canonical;
    }
}

static const char *gear_selector_text(int value)
{
    switch (value) {
        case 0: return "N";
        case -1: return "R";
        case -2: return "P";
        case -4: return "D";
        case -5: return "S";
        case -6: return "M";
        case -7: return "L";
        case -8: return "OD";
        default: return "?";
    }
}

static const char *cruise_state_text(int value)
{
    switch (value) {
        case 0: return "OFF";
        case 1: return "IDLE";
        case 2: return "CRUISE";
        case 3: return "ACCEL";
        case 4: return "DECEL";
        default: return "?";
    }
}

void ecu_channel_format(int channel, float canonical, char *buffer, size_t size)
{
    if (!buffer || size == 0) return;
    int whole = (int)lroundf(canonical);
    switch (ecu_channel_unit_kind(channel)) {
        case ECU_UNIT_BOOL:
            snprintf(buffer, size, "%s", canonical >= 0.5f ? "ON" : "OFF");
            return;
        case ECU_UNIT_GEAR_SELECTOR:
            snprintf(buffer, size, "%s", gear_selector_text(whole));
            return;
        case ECU_UNIT_CRUISE_STATE:
            snprintf(buffer, size, "%s", cruise_state_text(whole));
            return;
        default:
            break;
    }
    float value = ecu_channel_display_value(channel, canonical);
    int dp = valid_channel(channel) ? CHANNELS[channel].dp : 0;
    if (dp == 0) snprintf(buffer, size, "%d", (int)lroundf(value));
    else snprintf(buffer, size, "%.*f", dp, (double)value);
}

float ecu_channel_fraction(int channel, float canonical)
{
    if (!valid_channel(channel)) return 0.0f;
    float lo = CHANNELS[channel].lo;
    float hi = CHANNELS[channel].hi;
    if (hi <= lo) return 0.0f;
    float fraction = (canonical - lo) / (hi - lo);
    if (fraction < 0.0f) return 0.0f;
    if (fraction > 1.0f) return 1.0f;
    return fraction;
}
