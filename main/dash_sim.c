#include "dash_sim.h"
#include "esp_random.h"

static bool s_enabled = false;
static dash_sim_mode_t s_mode = DASH_SIM_MODE_IDLE;

static float  s_rpm    = 850.0f;
static float  s_speed  = 0.0f;
static float  s_ect    = 90.0f;
static float  s_iat    = 70.0f;
static float  s_afr    = 14.7f;
static float  s_timing = 12.0f;
static float  s_map    = -14.0f;
static float  s_batt   = 14.1f;
static float  s_tps    = 0.0f;
static float  s_oil    = 45.0f;
static float  s_duty   = 10.0f;
static float  s_knock  = 0.0f;
static float  s_fuel   = 75.0f;
static double s_odo    = 50000.0;

static void dash_sim_reset_state(void)
{
    s_rpm    = 850.0f;
    s_speed  = 0.0f;
    s_ect    = 90.0f;
    s_iat    = 70.0f;
    s_afr    = 14.7f;
    s_timing = 12.0f;
    s_map    = -14.0f;
    s_batt   = 14.1f;
    s_tps    = 0.0f;
    s_oil    = 45.0f;
    s_duty   = 10.0f;
    s_knock  = 0.0f;
    s_fuel   = 75.0f;
    s_odo    = 50000.0;
}

void dash_sim_set_enabled(bool enabled)
{
    if (enabled && !s_enabled) {
        dash_sim_reset_state();
    }
    s_enabled = enabled;
}

bool dash_sim_is_enabled(void)
{
    return s_enabled;
}

void dash_sim_set_mode(dash_sim_mode_t mode)
{
    if (mode < 0 || mode >= DASH_SIM_MODE_COUNT) return;
    s_mode = mode;
    dash_sim_set_enabled(true);
}

dash_sim_mode_t dash_sim_get_mode(void)
{
    return s_mode;
}

/* returns a pseudo-random float in [-1, 1] */
static float rand_unit(void)
{
    return ((float)(esp_random() % 20001) / 10000.0f) - 1.0f;
}

/* nudges `current` by a random step within +/-max_step, clamped to [min_v, max_v] */
static float approach(float current, float target, float rate, float jitter)
{
    return current + (target - current) * rate + rand_unit() * jitter;
}

typedef struct {
    float rpm;
    float speed;
    int8_t gear;
    float ect;
    float iat;
    float afr;
    float timing;
    float map;
    float batt;
    float tps;
    float oil;
    float duty;
} sim_target_t;

static const sim_target_t SIM_TARGETS[DASH_SIM_MODE_COUNT] = {
    [DASH_SIM_MODE_IDLE] = {
        850.0f, 0.0f, 0, 195.0f, 82.0f, 14.7f, 12.0f, -14.0f, 14.1f, 1.5f, 32.0f, 7.0f,
    },
    [DASH_SIM_MODE_CRUISE] = {
        2750.0f, 65.0f, 5, 198.0f, 88.0f, 14.7f, 32.0f, -7.0f, 14.2f, 23.0f, 58.0f, 25.0f,
    },
    [DASH_SIM_MODE_FULL_THROTTLE] = {
        6500.0f, 105.0f, 4, 205.0f, 105.0f, 11.6f, 18.0f, 16.0f, 14.0f, 100.0f, 82.0f, 78.0f,
    },
    [DASH_SIM_MODE_REDLINE] = {
        8550.0f, 125.0f, 4, 212.0f, 115.0f, 11.3f, 15.0f, 18.0f, 13.9f, 100.0f, 95.0f, 92.0f,
    },
};

void dash_sim_step(honda_dash_data_t *out)
{
    if (!out) {
        return;
    }

    const sim_target_t *target = &SIM_TARGETS[s_mode];
    s_rpm    = approach(s_rpm, target->rpm, 0.045f, s_mode == DASH_SIM_MODE_IDLE ? 3.0f : 10.0f);
    s_speed  = approach(s_speed, target->speed, 0.018f, 0.04f);
    s_ect    = approach(s_ect, target->ect, 0.00018f, 0.002f);
    s_iat    = approach(s_iat, target->iat, 0.00035f, 0.003f);
    s_afr    = approach(s_afr, target->afr, 0.08f, 0.015f);
    s_timing = approach(s_timing, target->timing, 0.06f, 0.08f);
    s_map    = approach(s_map, target->map, 0.07f, 0.05f);
    s_batt   = approach(s_batt, target->batt, 0.03f, 0.005f);
    s_tps    = approach(s_tps, target->tps, 0.10f, 0.08f);
    s_oil    = approach(s_oil, target->oil, 0.035f, 0.08f);
    s_duty   = approach(s_duty, target->duty, 0.07f, 0.08f);

    /* knock mostly hovers near zero, with rare brief blips so the
       amber/red knock-zone coloring gets exercised too */
    s_knock = approach(s_knock, 0.05f, 0.08f, 0.015f);
    if (s_knock < 0.0f) s_knock = 0.0f;
    if (s_mode >= DASH_SIM_MODE_FULL_THROTTLE && (esp_random() % 800) == 0) {
        s_knock = 1.0f + (rand_unit() + 1.0f) * 0.5f;
    }

    /* fuel slowly drains, then loops back to full so the sim can run
       indefinitely without needing to be reset */
    s_fuel -= 0.0008f;
    if (s_fuel < 5.0f) {
        s_fuel = 95.0f;
    }

    /* odometer integrates the simulated speed -- gauge timer runs at 50Hz (20ms) */
    s_odo += (double)s_speed / 180000.0;

    out->rpm        = (uint16_t)s_rpm;
    out->speed_mph  = s_speed;
    out->gear       = target->gear;
    out->ect_f      = s_ect;
    out->iat_f      = s_iat;
    out->afr        = s_afr;
    out->timing_deg = s_timing;
    out->map_psi    = s_map;
    out->batt_v     = s_batt;
    out->tps_pct    = s_tps;
    out->oil_psi    = s_oil;
    out->duty_pct   = s_duty;
    out->knock_deg  = s_knock;
    out->cel        = false;
    out->odo_miles  = s_odo;
    out->fuel_pct   = s_fuel;
}
