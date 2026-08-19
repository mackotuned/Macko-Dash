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
} can_dash_data_t;


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

#endif