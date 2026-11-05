#pragma once

/*
 * Extended ECU channels.
 *
 * Every value an ECU can broadcast that is not one of the dashboard's core
 * values (RPM, speed, gear, coolant, intake air, AFR, timing, boost, battery,
 * throttle, oil pressure, injector duty, knock, fuel %) lives here.
 *
 * A protocol JSON signal whose "name" matches a channel's signal name below is
 * stored in that channel. Any dashboard field slot can show any channel, and
 * custom themes can bind to them as dash_<signal>_value / _unit / _bar / _arc /
 * _path_gauge / _needle / _indicator.
 *
 * Values are stored in the dashboard's canonical units (temperature F,
 * pressure PSI, speed KPH, volume litres, acceleration G) and converted to the
 * user's unit preferences only for display.
 *
 * IMPORTANT: the position of each entry is saved in NVS as a field-slot
 * channel number. Only ever APPEND new entries to the end of this list.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ECU_UNIT_NONE = 0,
    ECU_UNIT_TEMP,
    ECU_UNIT_PRESSURE,
    ECU_UNIT_SPEED,
    ECU_UNIT_PERCENT,
    ECU_UNIT_DEG,
    ECU_UNIT_LAMBDA,
    ECU_UNIT_RPM,
    ECU_UNIT_VOLT,
    ECU_UNIT_G,
    ECU_UNIT_FLOW,
    ECU_UNIT_VOLUME,
    ECU_UNIT_DB,
    ECU_UNIT_SECONDS,
    ECU_UNIT_PPM,
    ECU_UNIT_GM3,
    ECU_UNIT_BOOL,
    ECU_UNIT_GEAR_SELECTOR,
    ECU_UNIT_CRUISE_STATE,
} ecu_unit_t;

/*   X(enum id,              signal name,            field label,         unit,               dp, bar min, bar max) */
#define ECU_CHANNEL_LIST(X) \
    X(ECU_CH_FUEL_PRESSURE,    "fuel_pressure",         "Fuel Pressure",     ECU_UNIT_PRESSURE,   0, 0, 100) \
    X(ECU_CH_OIL_TEMP,         "oil_temp",              "Oil Temp",          ECU_UNIT_TEMP,       0, 0, 300) \
    X(ECU_CH_FUEL_TEMP,        "fuel_temp",             "Fuel Temp",         ECU_UNIT_TEMP,       0, 0, 200) \
    X(ECU_CH_TRANS_TEMP,       "trans_temp",            "Gearbox Temp",      ECU_UNIT_TEMP,       0, 0, 300) \
    X(ECU_CH_DIFF_TEMP,        "diff_temp",             "Diff Temp",         ECU_UNIT_TEMP,       0, 0, 300) \
    X(ECU_CH_ETHANOL,          "fuel_comp",             "Ethanol",           ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_FUEL_LEVEL_L,     "fuel_level_l",          "Fuel Level",        ECU_UNIT_VOLUME,     1, 0, 100) \
    X(ECU_CH_COOLANT_PRESS,    "coolant_pressure",      "Coolant Press",     ECU_UNIT_PRESSURE,   1, 0, 30) \
    X(ECU_CH_ENGINE_DEMAND,    "engine_demand",         "Engine Demand",     ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_WASTEGATE_PRESS,  "wastegate_pressure",    "Wastegate Press",   ECU_UNIT_PRESSURE,   1, 0, 40) \
    X(ECU_CH_INJ2_DUTY,        "inj_stage2_duty",       "Inj Stage 2 Duty",  ECU_UNIT_PERCENT,    0, 0, 100) \
    X(ECU_CH_INJ3_DUTY,        "inj_stage3_duty",       "Inj Stage 3 Duty",  ECU_UNIT_PERCENT,    0, 0, 100) \
    X(ECU_CH_INJ4_DUTY,        "inj_stage4_duty",       "Inj Stage 4 Duty",  ECU_UNIT_PERCENT,    0, 0, 100) \
    X(ECU_CH_WB1,              "wideband1",             "Wideband 1",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB2,              "wideband2",             "Wideband 2",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB3,              "wideband3",             "Wideband 3",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB4,              "wideband4",             "Wideband 4",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB5,              "wideband5",             "Wideband 5",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB6,              "wideband6",             "Wideband 6",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB7,              "wideband7",             "Wideband 7",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB8,              "wideband8",             "Wideband 8",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB9,              "wideband9",             "Wideband 9",        ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB10,             "wideband10",            "Wideband 10",       ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB11,             "wideband11",            "Wideband 11",       ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB12,             "wideband12",            "Wideband 12",       ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB_OVERALL,       "wideband_overall",      "Wideband Overall",  ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB_BANK1,         "wideband_bank1",        "Wideband Bank 1",   ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_WB_BANK2,         "wideband_bank2",        "Wideband Bank 2",   ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_TARGET_LAMBDA,    "target_lambda",         "Target Lambda",     ECU_UNIT_LAMBDA,     3, 0.6f, 1.4f) \
    X(ECU_CH_STFT1,            "stft_bank1",            "Short Trim B1",     ECU_UNIT_PERCENT,    1, -25, 25) \
    X(ECU_CH_STFT2,            "stft_bank2",            "Short Trim B2",     ECU_UNIT_PERCENT,    1, -25, 25) \
    X(ECU_CH_LTFT1,            "ltft_bank1",            "Long Trim B1",      ECU_UNIT_PERCENT,    1, -25, 25) \
    X(ECU_CH_LTFT2,            "ltft_bank2",            "Long Trim B2",      ECU_UNIT_PERCENT,    1, -25, 25) \
    X(ECU_CH_IGN_BANK1,        "ign_angle_bank1",       "Timing Bank 1",     ECU_UNIT_DEG,        1, -10, 50) \
    X(ECU_CH_IGN_BANK2,        "ign_angle_bank2",       "Timing Bank 2",     ECU_UNIT_DEG,        1, -10, 50) \
    X(ECU_CH_KNOCK1,           "knock_level1",          "Knock Level 1",     ECU_UNIT_DB,         2, 0, 30) \
    X(ECU_CH_KNOCK2,           "knock_level2",          "Knock Level 2",     ECU_UNIT_DB,         2, 0, 30) \
    X(ECU_CH_IN_CAM1,          "in_cam1",               "Intake Cam 1",      ECU_UNIT_DEG,        1, -60, 60) \
    X(ECU_CH_IN_CAM2,          "in_cam2",               "Intake Cam 2",      ECU_UNIT_DEG,        1, -60, 60) \
    X(ECU_CH_EX_CAM1,          "ex_cam1",               "Exhaust Cam 1",     ECU_UNIT_DEG,        1, -60, 60) \
    X(ECU_CH_EX_CAM2,          "ex_cam2",               "Exhaust Cam 2",     ECU_UNIT_DEG,        1, -60, 60) \
    X(ECU_CH_EGT1,             "egt1",                  "EGT 1",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT2,             "egt2",                  "EGT 2",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT3,             "egt3",                  "EGT 3",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT4,             "egt4",                  "EGT 4",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT5,             "egt5",                  "EGT 5",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT6,             "egt6",                  "EGT 6",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT7,             "egt7",                  "EGT 7",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT8,             "egt8",                  "EGT 8",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT9,             "egt9",                  "EGT 9",             ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT10,            "egt10",                 "EGT 10",            ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT11,            "egt11",                 "EGT 11",            ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_EGT12,            "egt12",                 "EGT 12",            ECU_UNIT_TEMP,       0, 0, 1800) \
    X(ECU_CH_TARGET_BOOST,     "target_boost",          "Target Boost",      ECU_UNIT_PRESSURE,   1, -15, 40) \
    X(ECU_CH_BARO,             "baro_pressure",         "Baro (Abs)",        ECU_UNIT_PRESSURE,   1, 10, 16) \
    X(ECU_CH_BOOST_CTRL_OUT,   "boost_control_output",  "Boost Ctrl Duty",   ECU_UNIT_PERCENT,    0, 0, 100) \
    X(ECU_CH_GEN_OUT1_DUTY,    "gen_output1_duty",      "Gen Output 1",      ECU_UNIT_PERCENT,    0, 0, 100) \
    X(ECU_CH_TURBO_SPEED1,     "turbo_speed1",          "Turbo Speed 1",     ECU_UNIT_RPM,        0, 0, 200000) \
    X(ECU_CH_TURBO_SPEED2,     "turbo_speed2",          "Turbo Speed 2",     ECU_UNIT_RPM,        0, 0, 200000) \
    X(ECU_CH_WS_FL,            "ws_fl",                 "Wheel Spd FL",      ECU_UNIT_SPEED,      0, 0, 320) \
    X(ECU_CH_WS_FR,            "ws_fr",                 "Wheel Spd FR",      ECU_UNIT_SPEED,      0, 0, 320) \
    X(ECU_CH_WS_RL,            "ws_rl",                 "Wheel Spd RL",      ECU_UNIT_SPEED,      0, 0, 320) \
    X(ECU_CH_WS_RR,            "ws_rr",                 "Wheel Spd RR",      ECU_UNIT_SPEED,      0, 0, 320) \
    X(ECU_CH_WHEEL_SLIP,       "wheel_slip",            "Wheel Slip",        ECU_UNIT_SPEED,      1, -20, 20) \
    X(ECU_CH_WHEEL_DIFF,       "wheel_diff",            "Wheel Diff",        ECU_UNIT_SPEED,      1, -20, 20) \
    X(ECU_CH_LAT_G,            "lateral_g",             "Lateral G",         ECU_UNIT_G,          2, -2, 2) \
    X(ECU_CH_LONG_G,           "long_g",                "Long G",            ECU_UNIT_G,          2, -2, 2) \
    X(ECU_CH_STEERING_ANGLE,   "steering_angle",        "Steering Angle",    ECU_UNIT_DEG,        1, -540, 540) \
    X(ECU_CH_DRIVESHAFT_RPM,   "driveshaft_rpm",        "Driveshaft RPM",    ECU_UNIT_RPM,        0, 0, 8000) \
    X(ECU_CH_BRAKE_PRESS,      "brake_pressure",        "Brake Pressure",    ECU_UNIT_PRESSURE,   0, 0, 1500) \
    X(ECU_CH_NOS_PRESS1,       "nos_pressure1",         "NOS Pressure 1",    ECU_UNIT_PRESSURE,   0, 0, 1500) \
    X(ECU_CH_NOS_PRESS2,       "nos_pressure2",         "NOS Pressure 2",    ECU_UNIT_PRESSURE,   0, 0, 1500) \
    X(ECU_CH_NOS_PRESS3,       "nos_pressure3",         "NOS Pressure 3",    ECU_UNIT_PRESSURE,   0, 0, 1500) \
    X(ECU_CH_NOS_PRESS4,       "nos_pressure4",         "NOS Pressure 4",    ECU_UNIT_PRESSURE,   0, 0, 1500) \
    X(ECU_CH_GEARBOX_LINE_PRESS, "gearbox_line_pressure", "Trans Line Press", ECU_UNIT_PRESSURE,  0, 0, 300) \
    X(ECU_CH_CRANKCASE_PRESS,  "crankcase_pressure",    "Crankcase Press",   ECU_UNIT_PRESSURE,   1, -15, 15) \
    X(ECU_CH_INJ_PRESS_DIFF,   "inj_pressure_diff",     "Inj Press Diff",    ECU_UNIT_PRESSURE,   0, 0, 100) \
    X(ECU_CH_ACCEL_PEDAL,      "accel_pedal",           "Accel Pedal",       ECU_UNIT_PERCENT,    0, 0, 100) \
    X(ECU_CH_FUEL_FLOW,        "fuel_flow",             "Fuel Flow",         ECU_UNIT_FLOW,       0, 0, 5000) \
    X(ECU_CH_FUEL_FLOW_RETURN, "fuel_flow_return",      "Fuel Flow Return",  ECU_UNIT_FLOW,       0, 0, 5000) \
    X(ECU_CH_AMBIENT_TEMP,     "ambient_temp",          "Ambient Temp",      ECU_UNIT_TEMP,       0, 0, 130) \
    X(ECU_CH_REL_HUMIDITY,     "relative_humidity",     "Rel Humidity",      ECU_UNIT_PERCENT,    0, 0, 100) \
    X(ECU_CH_SPEC_HUMIDITY,    "specific_humidity",     "Spec Humidity",     ECU_UNIT_PPM,        0, 0, 30000) \
    X(ECU_CH_ABS_HUMIDITY,     "absolute_humidity",     "Abs Humidity",      ECU_UNIT_GM3,        1, 0, 30) \
    X(ECU_CH_LAUNCH_RETARD,    "launch_retard",         "Launch Retard",     ECU_UNIT_DEG,        1, 0, 30) \
    X(ECU_CH_LAUNCH_ENRICH,    "launch_enrich",         "Launch Enrich",     ECU_UNIT_PERCENT,    1, 0, 50) \
    X(ECU_CH_ENGINE_LIMITING,  "engine_limiting",       "Engine Limiting",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_TRIGGER_ERRORS,   "trigger_errors",        "Trigger Errors",    ECU_UNIT_NONE,       0, 0, 100) \
    X(ECU_CH_TRIGGER_COUNTER,  "trigger_counter",       "Trigger Counter",   ECU_UNIT_NONE,       0, 0, 65535) \
    X(ECU_CH_TRIGGER_SYNC,     "trigger_sync",          "Trigger Sync",      ECU_UNIT_NONE,       0, 0, 10) \
    X(ECU_CH_GENERIC1,         "generic1",              "Generic Sensor 1",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC2,         "generic2",              "Generic Sensor 2",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC3,         "generic3",              "Generic Sensor 3",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC4,         "generic4",              "Generic Sensor 4",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC5,         "generic5",              "Generic Sensor 5",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC6,         "generic6",              "Generic Sensor 6",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC7,         "generic7",              "Generic Sensor 7",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC8,         "generic8",              "Generic Sensor 8",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC9,         "generic9",              "Generic Sensor 9",  ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GENERIC10,        "generic10",             "Generic Sensor 10", ECU_UNIT_PERCENT,    1, 0, 100) \
    X(ECU_CH_GEAR_SELECTOR,    "gear_selector",         "Gear Selector",     ECU_UNIT_GEAR_SELECTOR, 0, -8, 0) \
    X(ECU_CH_NEUTRAL_SW,       "neutral_switch",        "Neutral Switch",    ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_REVERSE_SW,       "reverse_switch",        "Reverse Switch",    ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_GEAR_SW,          "gear_switch",           "Gear Switch",       ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_CLUTCH_SW,        "clutch_switch",         "Clutch Switch",     ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_BRAKE_SW,         "brake_switch",          "Brake Switch",      ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_DECEL_CUT,        "decel_cut",             "Decel Cut",         ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_TRANSIENT_THROTTLE, "transient_throttle",  "Transient Thr",     ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_OIL_LIGHT,        "oil_light",             "Oil Press Light",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_CEL,              "check_engine",          "Check Engine",      ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_BATTERY_LIGHT,    "battery_light",         "Battery Light",     ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_LAUNCH_ACTIVE,    "launch_active",         "Launch Active",     ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_LAUNCH_SW,        "launch_switch",         "Launch Switch",     ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_AUX_LIMITER,      "aux_limiter",           "Aux Rev Limiter",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_FLAT_SHIFT_SW,    "flat_shift_switch",     "Flat Shift Sw",     ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_TORQUE_REDUCTION, "torque_reduction",      "Torque Reduction",  ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_TC_ACTIVE,        "tc_active",             "Traction Active",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_TC_ENABLED,       "tc_enabled",            "Traction Enabled",  ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_AC_REQUEST,       "ac_request",            "A/C Request",       ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_AC_OUTPUT,        "ac_output",             "A/C Output",        ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_FAN1,             "fan1",                  "Thermo Fan 1",      ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_FAN2,             "fan2",                  "Thermo Fan 2",      ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_FAN3,             "fan3",                  "Thermo Fan 3",      ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_FAN4,             "fan4",                  "Thermo Fan 4",      ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_IGNITION_SW,      "ignition_switch",       "Ignition Switch",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_TRIM_POT1,        "trim_pot1",             "Trim Pot 1",        ECU_UNIT_NONE,       0, -127, 127) \
    X(ECU_CH_TRIM_POT2,        "trim_pot2",             "Trim Pot 2",        ECU_UNIT_NONE,       0, -127, 127) \
    X(ECU_CH_TRIM_POT3,        "trim_pot3",             "Trim Pot 3",        ECU_UNIT_NONE,       0, -127, 127) \
    X(ECU_CH_TQ_KNOB,          "torque_knob",           "Torque Mgmt Knob",  ECU_UNIT_NONE,       0, -127, 127) \
    X(ECU_CH_TURBO_TIMER,      "turbo_timer",           "Turbo Timer",       ECU_UNIT_SECONDS,    0, 0, 255) \
    X(ECU_CH_TURBO_TIMER_ENG,  "turbo_timer_engine",    "Turbo Timer Eng",   ECU_UNIT_SECONDS,    0, 0, 255) \
    X(ECU_CH_NITROUS1,         "nitrous_stage1",        "Nitrous Stage 1",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_NITROUS2,         "nitrous_stage2",        "Nitrous Stage 2",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_NITROUS3,         "nitrous_stage3",        "Nitrous Stage 3",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_NITROUS4,         "nitrous_stage4",        "Nitrous Stage 4",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_NITROUS5,         "nitrous_stage5",        "Nitrous Stage 5",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_NITROUS6,         "nitrous_stage6",        "Nitrous Stage 6",   ECU_UNIT_BOOL,       0, 0, 1) \
    X(ECU_CH_RACE_TIMER,       "race_timer",            "Race Timer",        ECU_UNIT_SECONDS,    2, 0, 60) \
    X(ECU_CH_TQ_DS_TARGET,     "tq_driveshaft_target",  "TQ Shaft Target",   ECU_UNIT_RPM,        0, 0, 8000) \
    X(ECU_CH_TQ_DS_ERROR,      "tq_driveshaft_error",   "TQ Shaft Error",    ECU_UNIT_RPM,        0, -2000, 2000) \
    X(ECU_CH_TQ_ERR_IGN,       "tq_error_ign_corr",     "TQ Error Ign",      ECU_UNIT_DEG,        1, -20, 20) \
    X(ECU_CH_TQ_TIMED_IGN,     "tq_timed_ign_corr",     "TQ Timed Ign",      ECU_UNIT_DEG,        1, -20, 20) \
    X(ECU_CH_TQ_COMBINED_IGN,  "tq_combined_ign_corr",  "TQ Combined Ign",   ECU_UNIT_DEG,        1, -20, 20) \
    X(ECU_CH_CRUISE_TARGET,    "cruise_target",         "Cruise Target",     ECU_UNIT_SPEED,      0, 0, 200) \
    X(ECU_CH_CRUISE_LAST,      "cruise_last_target",    "Cruise Last Tgt",   ECU_UNIT_SPEED,      0, 0, 200) \
    X(ECU_CH_CRUISE_ERROR,     "cruise_speed_error",    "Cruise Error",      ECU_UNIT_SPEED,      1, -20, 20) \
    X(ECU_CH_CRUISE_STATE,     "cruise_state",          "Cruise State",      ECU_UNIT_CRUISE_STATE, 0, 0, 4) \
    X(ECU_CH_CRUISE_INPUT,     "cruise_input",          "Cruise Input",      ECU_UNIT_NONE,       0, 0, 512)

#define ECU_CHANNEL_ENUM_ENTRY(id, signal, label, unit, dp, lo, hi) id,
typedef enum {
    ECU_CHANNEL_LIST(ECU_CHANNEL_ENUM_ENTRY)
    ECU_CHANNEL_COUNT
} ecu_channel_t;
#undef ECU_CHANNEL_ENUM_ENTRY

/* Returns the channel index for a protocol signal name, or -1. */
int ecu_channel_find(const char *signal_name);

/* Called from the CAN task when a signal for this channel is decoded. */
void ecu_channel_set(int channel, float value);

/* Clears every channel (e.g. when the CAN driver restarts). */
void ecu_channel_reset(void);

/* Latest canonical value; returns false when not received recently. */
bool ecu_channel_get(int channel, float *value);

const char *ecu_channel_signal(int channel);
const char *ecu_channel_label(int channel);
ecu_unit_t ecu_channel_unit_kind(int channel);

/* Unit text matching the user's unit preferences (e.g. "DEG F", "KPA"). */
const char *ecu_channel_unit(int channel);

/* Converts a canonical value into the user's display units. */
float ecu_channel_display_value(int channel, float canonical);

/* Formats a canonical value for display (handles ON/OFF and enums). */
void ecu_channel_format(int channel, float canonical, char *buffer, size_t size);

/* 0..1 position of a canonical value within the channel's bar range. */
float ecu_channel_fraction(int channel, float canonical);
