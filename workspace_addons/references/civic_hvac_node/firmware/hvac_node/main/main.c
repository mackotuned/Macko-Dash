// HVAC node for a 2000 Honda Civic climate control board.
// ESP32 DevKitC (WROOM-32), ESP-IDF 5.5 / 6.0.
//
// Fakes the board's three Gray-code dials (COM + A/B/C/D) with 12 AQY212
// PhotoMOS relays. Takes dial positions from the S3 dash over I2C (slave at
// HVAC_I2C_ADDR). The USB serial monitor has a test mode, type "help".

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_slave.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "hvac_protocol.h"

static const char *TAG = "hvac";

// ---------- I2C link to the S3 dash ----------
#define I2C_SDA_PIN  GPIO_NUM_21
#define I2C_SCL_PIN  GPIO_NUM_22

// Time per click when stepping a dial.
#define STEP_MS      60

// ---------- dial code tables: bit0 = A, bit1 = B, bit2 = C, bit3 = D ----------
// Temp: confirmed on the bench, Gray codes 1..15.
static const uint8_t TEMP_CODES[] = {1, 3, 2, 6, 7, 5, 4, 12, 13, 15, 14, 10, 11, 9, 8};
// Mode: positions 0-3 confirmed. Pos 4 measured B+C+D (14); if defrost
// doesn't select, try 13 (A+C+D).
static const uint8_t MODE_CODES[] = {1, 2, 7, 4, 14};
// Fan: positions 0, 2-4 and 6-10 measured. Pos 1 and 5 are best guesses from
// the Gray pattern; confirm with "raw fan N" in the serial test mode.
static const uint8_t FAN_CODES[]  = {1, 7, 5, 4, 12, 13, 15, 14, 10, 11, 9};

typedef struct {
    const char *name;
    gpio_num_t pins[4];          // relay drive pins for A, B, C, D
    const uint8_t *codes;
    uint8_t count;
    volatile uint8_t pos;        // position the board currently sees
    volatile uint8_t target;     // position we're stepping towards
} dial_t;

enum { FAN, MODE, TEMP, DIAL_COUNT };

static dial_t dials[DIAL_COUNT] = {
    [FAN]  = {"fan",  {GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19}, FAN_CODES,  sizeof(FAN_CODES),  0, 0},
    [MODE] = {"mode", {GPIO_NUM_23, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27}, MODE_CODES, sizeof(MODE_CODES), 0, 0},
    [TEMP] = {"temp", {GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_4,  GPIO_NUM_13}, TEMP_CODES, sizeof(TEMP_CODES), 7, 7},
};

static void write_code(const dial_t *d, uint8_t code)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(d->pins[i], (code >> i) & 1);
    }
}

static void set_target(dial_t *d, int t)
{
    if (t < 0) t = 0;
    if (t >= d->count) t = d->count - 1;
    d->target = (uint8_t)t;
}

// One click per dial every STEP_MS, like a hand turning the knobs.
static void step_task(void *arg)
{
    for (;;) {
        for (int i = 0; i < DIAL_COUNT; i++) {
            dial_t *d = &dials[i];
            if (d->pos == d->target) continue;
            d->pos += (d->target > d->pos) ? 1 : -1;
            write_code(d, d->codes[d->pos]);
        }
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}

// ---------- I2C slave ----------
typedef struct {
    uint8_t len;
    uint8_t data[16];
} i2c_msg_t;

static QueueHandle_t i2c_queue;

// Runs in ISR context: copy the bytes out and let i2c_task parse them.
static bool on_i2c_receive(i2c_slave_dev_handle_t dev, const i2c_slave_rx_done_event_data_t *evt, void *arg)
{
    i2c_msg_t msg = {0};
    msg.len = evt->length > sizeof(msg.data) ? sizeof(msg.data) : evt->length;
    memcpy(msg.data, evt->buffer, msg.len);
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(i2c_queue, &msg, &woken);
    return woken == pdTRUE;
}

static void handle_packet(const uint8_t *p)
{
    if (p[0] != HVAC_PKT_HEADER) return;
    if ((uint8_t)(p[1] + p[2] + p[3]) != p[4]) {
        ESP_LOGW(TAG, "bad checksum, packet ignored");
        return;
    }
    set_target(&dials[FAN], p[1]);
    set_target(&dials[MODE], p[2]);
    set_target(&dials[TEMP], p[3]);
    ESP_LOGI(TAG, "dash: fan %u  mode %u  temp %u", p[1], p[2], p[3]);
}

static void i2c_task(void *arg)
{
    i2c_msg_t msg;
    for (;;) {
        if (xQueueReceive(i2c_queue, &msg, portMAX_DELAY) != pdTRUE) continue;
        // Find a header with a full packet after it.
        for (int i = 0; i + HVAC_PKT_LEN <= msg.len; i++) {
            if (msg.data[i] == HVAC_PKT_HEADER) {
                handle_packet(&msg.data[i]);
                break;
            }
        }
    }
}

static void i2c_init(void)
{
    i2c_queue = xQueueCreate(8, sizeof(i2c_msg_t));

    i2c_slave_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .send_buf_depth = 64,
        .receive_buf_depth = 64,
        .slave_addr = HVAC_I2C_ADDR,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };
    i2c_slave_dev_handle_t dev;
    ESP_ERROR_CHECK(i2c_new_slave_device(&cfg, &dev));

    i2c_slave_event_callbacks_t cbs = {
        .on_receive = on_i2c_receive,
    };
    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(dev, &cbs, NULL));

    xTaskCreate(i2c_task, "i2c", 3072, NULL, 5, NULL);
}

// ---------- serial test mode ----------
static dial_t *dial_by_name(const char *name)
{
    for (int i = 0; i < DIAL_COUNT; i++) {
        if (strcmp(name, dials[i].name) == 0) return &dials[i];
    }
    return NULL;
}

static void print_status(void)
{
    printf("fan %u  mode %u  temp %u\n", dials[FAN].pos, dials[MODE].pos, dials[TEMP].pos);
}

static void print_help(void)
{
    printf("commands:\n"
           "  fan 3 | mode 2 | temp 10   step a dial to a position\n"
           "  raw fan 12                 put a raw 0-15 code on a dial's pads\n"
           "  off                        open every relay\n"
           "  status                     show current positions\n");
}

static void handle_command(char *line)
{
    char name[8];
    int value;

    if (strcmp(line, "") == 0) return;
    if (strcmp(line, "status") == 0) { print_status(); return; }
    if (strcmp(line, "help") == 0) { print_help(); return; }

    if (strcmp(line, "off") == 0) {
        for (int i = 0; i < DIAL_COUNT; i++) write_code(&dials[i], 0);
        printf("all relays open\n");
        return;
    }

    if (sscanf(line, "raw %7s %d", name, &value) == 2) {
        dial_t *d = dial_by_name(name);
        if (d && value >= 0 && value <= 15) {
            write_code(d, (uint8_t)value);
            printf("%s raw code %d  (D C B A = %d %d %d %d)\n", d->name, value,
                   (value >> 3) & 1, (value >> 2) & 1, (value >> 1) & 1, value & 1);
            return;
        }
    } else if (sscanf(line, "%7s %d", name, &value) == 2) {
        dial_t *d = dial_by_name(name);
        if (d) {
            set_target(d, value);
            printf("%s -> %u\n", d->name, d->target);
            return;
        }
    }
    printf("? type help\n");
}

static void console_task(void *arg)
{
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    char line[48];
    int len = 0;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) continue;
        if (c == '\r' || c == '\n') {
            if (len == 0) continue;
            uart_write_bytes(UART_NUM_0, "\r\n", 2);
            line[len] = '\0';
            for (int i = 0; i < len; i++) {
                if (line[i] >= 'A' && line[i] <= 'Z') line[i] += 'a' - 'A';
            }
            handle_command(line);
            len = 0;
        } else if ((c == 8 || c == 127) && len > 0) {
            len--;
            uart_write_bytes(UART_NUM_0, "\b \b", 3);
        } else if (c >= ' ' && len < (int)sizeof(line) - 1) {
            line[len++] = (char)c;
            uart_write_bytes(UART_NUM_0, (const char *)&c, 1);
        }
    }
}

void app_main(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < DIAL_COUNT; i++) {
        for (int j = 0; j < 4; j++) mask |= 1ULL << dials[i].pins[j];
    }
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    // Put a valid code on every dial right away, never all-open.
    for (int i = 0; i < DIAL_COUNT; i++) write_code(&dials[i], dials[i].codes[dials[i].pos]);

    i2c_init();
    xTaskCreate(step_task, "step", 2048, NULL, 6, NULL);
    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "Civic HVAC node ready, I2C address 0x%02X", HVAC_I2C_ADDR);
    print_status();
    print_help();
}
