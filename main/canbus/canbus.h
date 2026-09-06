#ifndef CANBUS_H
#define CANBUS_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/twai.h"


// =======================================================
// DASH DATA STRUCTURE
// =======================================================

typedef struct{
    float rpm;
    float speed;
    float coolant_temp;
    float air_temp;
    float oil_temp;
    float battery_voltage;
    float oil_pressure;
    float fuel_pressure;
    float air_fuel_ratio;
    float boost;
    float fuel_comp;
    float gear;
    float tps;
    float ign_angle;
    float fuel_level;
    float analog_inputs[8];
} can_dash_data_t;

typedef enum {
    CANBUS_CONTROLLER_OFFLINE = 0,
    CANBUS_CONTROLLER_STOPPED,
    CANBUS_CONTROLLER_RUNNING,
    CANBUS_CONTROLLER_BUS_OFF,
    CANBUS_CONTROLLER_RECOVERING,
} canbus_controller_state_t;

typedef struct {
    canbus_controller_state_t controller_state;
    bool live_data;
    bool obd2_active;
    bool drivetrain_live;
    bool gear_live;
    bool oil_pressure_recent;
    uint32_t last_frame_age_ms;
    uint32_t queued_frames;
    uint32_t receive_missed_count;
    uint32_t bus_error_count;
    uint32_t rx_error_counter;
    int bitrate;
    char protocol[32];
} canbus_diagnostics_t;


// global decoded data
extern volatile can_dash_data_t can_data;



// =======================================================
// API
// =======================================================

void canbus_init(void);
void canbus_shutdown(void);
void canbus_task(void *arg);
void process_can_frame(uint32_t id, uint8_t *data);
bool canbus_has_live_data(void);
bool canbus_has_live_gear(void);
bool canbus_has_live_drivetrain(void);
bool canbus_has_recent_oil_pressure(void);
void canbus_get_diagnostics(canbus_diagnostics_t *diagnostics);

#endif