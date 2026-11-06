#include "gps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STANDSTILL_KPH   2.0f       /* GPS drifts a little when parked */
#define FRESH_US         1500000    /* speed older than this is not used */

static const char *TAG = "gps";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static gps_status_t s_status;
static int64_t s_speed_us;          /* time of the last speed sentence */
static int64_t s_rate_window_us;
static int s_rate_count;
static bool s_vtg_seen;            /* VTG gives speed directly; RMC is only the fallback */

static char s_line[100];
static size_t s_line_len;

/* ------------------------------ NMEA -------------------------------- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Splits "$GPRMC,a,b,...*CS" in place; fields[0] is the sentence id. */
static int split_fields(char *body, char **fields, int max)
{
    int n = 0;
    fields[n++] = body;
    for (char *p = body; *p && n < max; ++p) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

static void got_speed(float kph, bool valid)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    s_status.fix = valid;
    s_status.speed_kph = valid && kph >= STANDSTILL_KPH ? kph : 0.0f;
    s_speed_us = now;
    if (!s_rate_window_us) s_rate_window_us = now;
    s_rate_count++;
    if (now - s_rate_window_us >= 2000000) {
        s_status.rate_hz = s_rate_count * 1000000.0f / (float)(now - s_rate_window_us);
        s_rate_count = 0;
        s_rate_window_us = now;
    }
    portEXIT_CRITICAL(&s_lock);
}

static bool handle_sentence(char *line)
{
    /* checksum: XOR of everything between '$' and '*' */
    char *star = strchr(line, '*');
    if (line[0] != '$' || !star || hexval(star[1]) < 0 || hexval(star[2]) < 0) return false;
    uint8_t sum = 0;
    for (char *p = line + 1; p < star; ++p) sum ^= (uint8_t)*p;
    if (sum != (uint8_t)(hexval(star[1]) * 16 + hexval(star[2]))) return false;
    *star = '\0';

    char *f[20];
    int n = split_fields(line + 1, f, 20);
    const char *id = f[0];
    if (strlen(id) < 5) return true;
    const char *type = id + 2;     /* GP / GN / GL talker prefix */

    if (strcmp(type, "VTG") == 0 && n > 9) {
        /* $GPVTG,course,T,,M,knots,N,kph,K,mode */
        bool valid = f[9][0] == 'A' || f[9][0] == 'D';
        s_vtg_seen = true;
        got_speed(valid ? strtof(f[7], NULL) : 0.0f, valid);
    } else if (strcmp(type, "RMC") == 0 && n > 7) {
        /* $GPRMC,time,status,lat,N,lon,E,knots,course,... -- used when VTG is off */
        bool valid = f[2][0] == 'A';
        if (!s_vtg_seen) got_speed(valid ? strtof(f[7], NULL) * 1.852f : 0.0f, valid);
    } else if (strcmp(type, "GGA") == 0 && n > 7) {
        /* $GPGGA,time,lat,N,lon,E,quality,satellites,... */
        portENTER_CRITICAL(&s_lock);
        s_status.satellites = atoi(f[7]);
        portEXIT_CRITICAL(&s_lock);
    }
    return true;
}

bool gps_feed(const uint8_t *data, size_t length)
{
    bool good = false;
    for (size_t i = 0; i < length; ++i) {
        char c = (char)data[i];
        if (c == '$') {
            s_line_len = 0;
        } else if (c == '\r' || c == '\n') {
            if (s_line_len > 6) {
                s_line[s_line_len] = '\0';
                good |= handle_sentence(s_line);
            }
            s_line_len = 0;
            continue;
        } else if (s_line_len == 0) {
            continue;                     /* wait for the start of a sentence */
        }
        if (s_line_len < sizeof(s_line) - 1) s_line[s_line_len++] = c;
        else s_line_len = 0;
    }
    return good;
}

/* ------------------------------ u-blox ------------------------------ */

static void ubx_send(uart_port_t uart, uint8_t cls, uint8_t id, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[6 + 32 + 2];
    frame[0] = 0xB5;
    frame[1] = 0x62;
    frame[2] = cls;
    frame[3] = id;
    frame[4] = (uint8_t)len;
    frame[5] = (uint8_t)(len >> 8);
    memcpy(&frame[6], payload, len);
    uint8_t a = 0, b = 0;
    for (int i = 2; i < 6 + len; ++i) {
        a += frame[i];
        b += a;
    }
    frame[6 + len] = a;
    frame[7 + len] = b;
    uart_write_bytes(uart, frame, 8 + len);
    uart_wait_tx_done(uart, pdMS_TO_TICKS(100));
}

static void ubx_msg_rate(uart_port_t uart, uint8_t nmea_id, uint8_t rate)
{
    /* CFG-MSG: class F0 (NMEA std), id, rate on the current port */
    uint8_t p[3] = {0xF0, nmea_id, rate};
    ubx_send(uart, 0x06, 0x01, p, sizeof(p));
}

int gps_configure(uart_port_t uart, int current_baud)
{
    const int fast_baud = 38400;
    if (current_baud != fast_baud) {
        /* CFG-PRT: UART1, 8N1, new baud, UBX+NMEA in, NMEA out */
        uint8_t prt[20] = {0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00,
                           (uint8_t)fast_baud, (uint8_t)(fast_baud >> 8), (uint8_t)(fast_baud >> 16), 0x00,
                           0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
        ubx_send(uart, 0x06, 0x00, prt, sizeof(prt));
        vTaskDelay(pdMS_TO_TICKS(150));
        uart_set_baudrate(uart, fast_baud);
        uart_flush_input(uart);
    }
    /* only GGA (satellites), RMC and VTG (speed) */
    ubx_msg_rate(uart, 0x01, 0);   /* GLL */
    ubx_msg_rate(uart, 0x02, 0);   /* GSA */
    ubx_msg_rate(uart, 0x03, 0);   /* GSV */
    ubx_msg_rate(uart, 0x00, 1);   /* GGA */
    ubx_msg_rate(uart, 0x04, 1);   /* RMC */
    ubx_msg_rate(uart, 0x05, 1);   /* VTG */
    /* CFG-RATE: 200 ms measurement (5 Hz), 1 cycle per solution, GPS time */
    uint8_t rate[6] = {200, 0, 1, 0, 1, 0};
    ubx_send(uart, 0x06, 0x08, rate, sizeof(rate));

    /* Did the module follow? Look for NMEA at the fast baud. */
    uint8_t buf[128];
    int64_t until = esp_timer_get_time() + 2500000;
    while (esp_timer_get_time() < until) {
        int n = uart_read_bytes(uart, buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (n > 0 && gps_feed(buf, n)) {
            ESP_LOGI(TAG, "u-blox set to 5 Hz at %d baud", fast_baud);
            return fast_baud;
        }
    }
    ESP_LOGI(TAG, "module kept its settings; using %d baud", current_baud);
    uart_set_baudrate(uart, current_baud);
    uart_flush_input(uart);
    return current_baud;
}

/* ------------------------------ status ------------------------------ */

void gps_set_detected(bool detected, int baud)
{
    portENTER_CRITICAL(&s_lock);
    s_status.detected = detected;
    s_status.baud = baud;
    if (!detected) {
        s_vtg_seen = false;
        s_status.fix = false;
        s_status.speed_kph = 0;
        s_status.satellites = 0;
        s_status.rate_hz = 0;
    }
    portEXIT_CRITICAL(&s_lock);
}

void gps_get_status(gps_status_t *out)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    bool fresh = now - s_speed_us < FRESH_US;
    portEXIT_CRITICAL(&s_lock);
    if (!fresh) {
        out->fix = false;
        out->speed_kph = 0;
        out->rate_hz = 0;
    }
}

bool gps_get_speed_kph(float *kph)
{
    gps_status_t st;
    gps_get_status(&st);
    if (!st.detected || !st.fix || !isfinite(st.speed_kph)) return false;
    *kph = st.speed_kph;
    return true;
}
