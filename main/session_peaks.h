#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "honda_dash_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_data;
    bool afr_valid;
    bool oil_valid;
    bool battery_valid;
    bool duty_valid;
    bool knock_valid;
    uint16_t max_rpm;
    float max_speed_mph;
    float max_boost_psi;
    float max_coolant_f;
    float max_intake_f;
    float max_duty_pct;
    float max_knock_deg;
    float min_afr;
    float min_oil_psi;
    float min_battery_v;
} session_peaks_t;

void session_peaks_init(void);
void session_peaks_update(const honda_dash_data_t *data, bool drivetrain_live);
void session_peaks_get(session_peaks_t *peaks);
void session_peaks_reset(void);

#ifdef __cplusplus
}
#endif