#include "session_peaks.h"

#include <float.h>
#include <string.h>

static session_peaks_t s_peaks;

void session_peaks_reset(void)
{
    memset(&s_peaks, 0, sizeof(s_peaks));
    s_peaks.min_afr = FLT_MAX;
    s_peaks.min_oil_psi = FLT_MAX;
    s_peaks.min_battery_v = FLT_MAX;
}

void session_peaks_init(void)
{
    session_peaks_reset();
}

void session_peaks_update(const honda_dash_data_t *data, bool drivetrain_live)
{
    if (!data || !drivetrain_live) return;

    bool first_sample = !s_peaks.has_data;
    s_peaks.has_data = true;
    if (first_sample || data->rpm > s_peaks.max_rpm) s_peaks.max_rpm = data->rpm;
    if (first_sample || data->speed_mph > s_peaks.max_speed_mph) s_peaks.max_speed_mph = data->speed_mph;
    if (first_sample || data->map_psi > s_peaks.max_boost_psi) s_peaks.max_boost_psi = data->map_psi;
    if (first_sample || data->ect_f > s_peaks.max_coolant_f) s_peaks.max_coolant_f = data->ect_f;
    if (first_sample || data->iat_f > s_peaks.max_intake_f) s_peaks.max_intake_f = data->iat_f;

    if (data->afr >= 5.0f && data->afr <= 25.0f && data->afr < s_peaks.min_afr) {
        s_peaks.min_afr = data->afr;
        s_peaks.afr_valid = true;
    }
    if (data->oil_valid && data->rpm >= 400 && data->oil_psi >= 0.0f &&
        data->oil_psi < s_peaks.min_oil_psi) {
        s_peaks.min_oil_psi = data->oil_psi;
        s_peaks.oil_valid = true;
    }
    if (data->batt_v >= 5.0f && data->batt_v <= 20.0f && data->batt_v < s_peaks.min_battery_v) {
        s_peaks.min_battery_v = data->batt_v;
        s_peaks.battery_valid = true;
    }
    if (data->duty_valid) {
        if (!s_peaks.duty_valid || data->duty_pct > s_peaks.max_duty_pct) {
            s_peaks.max_duty_pct = data->duty_pct;
        }
        s_peaks.duty_valid = true;
    }
    if (data->knock_valid) {
        if (!s_peaks.knock_valid || data->knock_deg > s_peaks.max_knock_deg) {
            s_peaks.max_knock_deg = data->knock_deg;
        }
        s_peaks.knock_valid = true;
    }
}

void session_peaks_get(session_peaks_t *peaks)
{
    if (peaks) *peaks = s_peaks;
}