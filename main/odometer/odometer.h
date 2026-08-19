#pragma once
#include <stdint.h>
#include <stdbool.h>

void odometer_init(void);
void odometer_add_meters(uint32_t meters);
void odometer_periodic_save(void);
void odometer_force_save(void);

/* Sets the odometer to an absolute value and saves immediately -- used
   for one-time calibration (e.g. matching a car's factory dash when
   this unit is first installed), not for normal distance accumulation. */
void odometer_set_miles(double miles);

uint64_t odometer_get_meters(void);
double odometer_get_miles(void);

/* --- trip A/B: accumulate automatically alongside the main odometer,
   independently resettable, shown via the dashboard's ODO tile cycling --- */
double odometer_get_trip_a_miles(void);
double odometer_get_trip_b_miles(void);
void odometer_reset_trip_a(void);
void odometer_reset_trip_b(void);