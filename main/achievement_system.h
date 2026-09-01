#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "honda_dash_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACHIEVEMENT_SYSTEM_TOTAL 10

typedef struct {
    const char *name;
    const char *unit;
    uint32_t hit_count;
    float record_value;
    bool record_valid;
    bool lower_is_better;
} achievement_system_entry_t;

void achievement_system_init(void);
const char *achievement_system_update(const honda_dash_data_t *data,
                                      bool drivetrain_live,
                                      bool oil_pressure_live);
uint8_t achievement_system_get_count(void);
bool achievement_system_get_entry(uint8_t index, achievement_system_entry_t *entry);

#ifdef __cplusplus
}
#endif