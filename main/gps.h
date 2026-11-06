#pragma once

/*
 * GPS on the accessory port (the old wired-knob plug, UART1 GPIO49/50).
 * NMEA from u-blox NEO-6M style modules: speed, fix and satellites.
 * The port owner (rotary_controller_uart.c) detects the module and feeds
 * received bytes to gps_feed().
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/uart.h"

typedef struct {
    bool detected;          /* NMEA seen on the port recently */
    bool fix;               /* position fix with valid speed */
    int satellites;
    float speed_kph;        /* 0 below the standstill threshold */
    float rate_hz;          /* measured updates per second */
    int baud;
} gps_status_t;

/* Parses NMEA bytes; returns true once a complete sentence with a good
   checksum has been seen (used for detection). */
bool gps_feed(const uint8_t *data, size_t length);

/* Asks a u-blox module for 5 Hz updates at a faster baud rate. Returns the
   baud the port should use afterwards (the old one if it is not u-blox). */
int gps_configure(uart_port_t uart, int current_baud);

void gps_set_detected(bool detected, int baud);

void gps_get_status(gps_status_t *out);

/* GPS speed if there is a fix and it is fresh. */
bool gps_get_speed_kph(float *kph);
