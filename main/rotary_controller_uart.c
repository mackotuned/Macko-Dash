#include "rotary_controller_uart.h"

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "honda_dash_ui.h"
#include "lwip/sockets.h"
#include "rotary_controller_protocol.h"

#define CONTROLLER_UART UART_NUM_1
#define CONTROLLER_RX_BUFFER_SIZE 512
#define KNOB_UDP_PORT 4210
#define KNOB_TIMEOUT_US 4000000

#ifndef CONFIG_ROTARY_CONTROLLER_ENABLE
#define CONFIG_ROTARY_CONTROLLER_ENABLE 0
#endif

static const char *TAG = "rotary_uart";
static uint8_t s_tx_sequence;
static SemaphoreHandle_t s_tx_mutex;
static bool s_uart_ready;

/* wireless knob (UDP over the dash's knob Wi-Fi network) */
typedef struct {
    struct sockaddr_in from;
    uint16_t length;
    uint8_t data[ROTARY_PROTOCOL_MAX_FRAME];
} net_frame_t;

static QueueHandle_t s_net_queue;
static int s_sock = -1;
static portMUX_TYPE s_knob_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_knob_last_rx;
static char s_knob_version[24];
static struct sockaddr_in s_knob_addr;
static bool s_knob_addr_valid;

typedef struct {
    uint8_t data[ROTARY_PROTOCOL_MAX_FRAME];
    size_t length;
    size_t expected_length;
} frame_parser_t;

/* where a reply goes: NULL = the wired UART, otherwise the knob's address */
typedef const struct sockaddr_in *reply_to_t;

static esp_err_t send_frame(reply_to_t to, rotary_protocol_type_t type, uint8_t sequence,
                            const uint8_t *payload, uint16_t payload_length)
{
    if (payload_length > ROTARY_PROTOCOL_MAX_PAYLOAD) return ESP_ERR_INVALID_SIZE;
    if (!s_tx_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    uint8_t frame[ROTARY_PROTOCOL_MAX_FRAME];
    frame[0] = ROTARY_PROTOCOL_MAGIC_0;
    frame[1] = ROTARY_PROTOCOL_MAGIC_1;
    frame[2] = ROTARY_PROTOCOL_VERSION;
    frame[3] = (uint8_t)type;
    frame[4] = sequence;
    rotary_protocol_put_u16(&frame[5], payload_length);
    if (payload_length) memcpy(&frame[ROTARY_PROTOCOL_HEADER_SIZE], payload, payload_length);
    size_t crc_offset = ROTARY_PROTOCOL_HEADER_SIZE + payload_length;
    rotary_protocol_put_u16(&frame[crc_offset], rotary_protocol_crc16(frame, crc_offset));
    size_t total = crc_offset + ROTARY_PROTOCOL_CRC_SIZE;
    bool ok;
    if (to) {
        ok = s_sock >= 0 && sendto(s_sock, frame, total, 0, (const struct sockaddr *)to, sizeof(*to)) == (int)total;
    } else {
        ok = s_uart_ready && uart_write_bytes(CONTROLLER_UART, frame, total) == (int)total;
    }
    xSemaphoreGive(s_tx_mutex);
    return ok ? ESP_OK : ESP_FAIL;
}

static void send_state(reply_to_t to, uint8_t sequence, const honda_dash_controller_state_t *state)
{
    uint8_t payload[ROTARY_PROTOCOL_MAX_PAYLOAD] = {0};
    payload[0] = (uint8_t)state->page;
    rotary_protocol_put_u16(&payload[1], (uint16_t)state->selection);
    rotary_protocol_put_u16(&payload[3], (uint16_t)state->count);
    rotary_protocol_put_u32(&payload[5], (uint32_t)state->value);
    payload[9] = state->recording ? 1 : 0;
    payload[10] = state->night ? 1 : 0;   /* status flags (bit 0: night mode) */
    /* bytes 11..18 (old air pressure values) are kept zero for compatibility */
    size_t label_length = strnlen(state->label, sizeof(state->label));
    if (label_length > ROTARY_PROTOCOL_MAX_PAYLOAD - 19) {
        label_length = ROTARY_PROTOCOL_MAX_PAYLOAD - 19;
    }
    memcpy(&payload[19], state->label, label_length);
    send_frame(to, ROTARY_PROTOCOL_STATE, sequence, payload, (uint16_t)(19 + label_length));
}

static void handle_frame(const uint8_t *frame, size_t frame_length, reply_to_t to)
{
    if (frame_length < ROTARY_PROTOCOL_HEADER_SIZE + ROTARY_PROTOCOL_CRC_SIZE) return;
    if (frame[0] != ROTARY_PROTOCOL_MAGIC_0 || frame[1] != ROTARY_PROTOCOL_MAGIC_1 ||
            frame[2] != ROTARY_PROTOCOL_VERSION) return;
    uint16_t payload_length = rotary_protocol_get_u16(&frame[5]);
    if (frame_length != ROTARY_PROTOCOL_HEADER_SIZE + payload_length + ROTARY_PROTOCOL_CRC_SIZE) return;
    uint16_t expected_crc = rotary_protocol_get_u16(&frame[frame_length - ROTARY_PROTOCOL_CRC_SIZE]);
    if (rotary_protocol_crc16(frame, frame_length - ROTARY_PROTOCOL_CRC_SIZE) != expected_crc) return;

    rotary_protocol_type_t type = (rotary_protocol_type_t)frame[3];
    uint8_t sequence = frame[4];
    const uint8_t *payload = &frame[ROTARY_PROTOCOL_HEADER_SIZE];

    if (to) {
        portENTER_CRITICAL(&s_knob_lock);
        s_knob_last_rx = esp_timer_get_time();
        s_knob_addr = *to;
        s_knob_addr_valid = true;
        /* knob 1.2.0+ puts its firmware version in the PING payload */
        if (type == ROTARY_PROTOCOL_PING && payload_length > 0) {
            size_t n = payload_length < sizeof(s_knob_version) - 1 ? payload_length : sizeof(s_knob_version) - 1;
            memcpy(s_knob_version, payload, n);
            s_knob_version[n] = '\0';
        }
        portEXIT_CRITICAL(&s_knob_lock);
    }

    honda_dash_controller_event_t event = HONDA_DASH_CONTROLLER_GET_STATE;
    int32_t value = 0;

    if (type == ROTARY_PROTOCOL_EVENT) {
        if (payload_length != 3 || payload[0] > HONDA_DASH_CONTROLLER_GET_STATE) return;
        event = (honda_dash_controller_event_t)payload[0];
        value = (int16_t)rotary_protocol_get_u16(&payload[1]);
    } else if (type == ROTARY_PROTOCOL_PING) {
        send_frame(to, ROTARY_PROTOCOL_PONG, sequence, NULL, 0);
    } else if (type != ROTARY_PROTOCOL_GET_STATE) {
        return;
    }

    honda_dash_controller_state_t state;
    if (bsp_display_lock(1000) != ESP_OK) return;
    bool handled = honda_dash_ui_controller_handle(event, value, &state);
    bsp_display_unlock();
    if (handled) send_state(to, sequence, &state);
}

static void parser_reset(frame_parser_t *parser)
{
    parser->length = 0;
    parser->expected_length = 0;
}

static void parser_push(frame_parser_t *parser, uint8_t byte)
{
    if (parser->length == 0 && byte != ROTARY_PROTOCOL_MAGIC_0) return;
    if (parser->length == 1 && byte != ROTARY_PROTOCOL_MAGIC_1) {
        parser->length = byte == ROTARY_PROTOCOL_MAGIC_0 ? 1 : 0;
        parser->data[0] = ROTARY_PROTOCOL_MAGIC_0;
        return;
    }
    parser->data[parser->length++] = byte;
    if (parser->length == ROTARY_PROTOCOL_HEADER_SIZE) {
        uint16_t payload_length = rotary_protocol_get_u16(&parser->data[5]);
        if (parser->data[2] != ROTARY_PROTOCOL_VERSION || payload_length > ROTARY_PROTOCOL_MAX_PAYLOAD) {
            parser_reset(parser);
            return;
        }
        parser->expected_length = ROTARY_PROTOCOL_HEADER_SIZE + payload_length + ROTARY_PROTOCOL_CRC_SIZE;
    }
    if (parser->expected_length && parser->length == parser->expected_length) {
        handle_frame(parser->data, parser->length, NULL);
        parser_reset(parser);
    } else if (parser->length >= sizeof(parser->data)) {
        parser_reset(parser);
    }
}

/* One task handles both links. Its stack is in internal RAM because knob
   actions can save settings (flash writes). */
static void controller_task(void *argument)
{
    (void)argument;
    frame_parser_t parser = {0};
    uint8_t buffer[64];
    net_frame_t net;
    while (true) {
        if (s_uart_ready) {
            int received = uart_read_bytes(CONTROLLER_UART, buffer, sizeof(buffer), pdMS_TO_TICKS(10));
            for (int index = 0; index < received; ++index) parser_push(&parser, buffer[index]);
        }
        TickType_t wait = s_uart_ready ? 0 : pdMS_TO_TICKS(100);
        while (s_net_queue && xQueueReceive(s_net_queue, &net, wait) == pdTRUE) {
            handle_frame(net.data, net.length, &net.from);
            wait = 0;
        }
        if (!s_net_queue && !s_uart_ready) vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Receives knob datagrams; its stack lives in PSRAM (no flash access here). */
static void knob_udp_task(void *argument)
{
    (void)argument;
    net_frame_t net;
    for (;;) {
        socklen_t from_len = sizeof(net.from);
        int n = recvfrom(s_sock, net.data, sizeof(net.data), 0, (struct sockaddr *)&net.from, &from_len);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        net.length = (uint16_t)n;
        xQueueSend(s_net_queue, &net, 0);
    }
}

static bool s_task_started;

static esp_err_t start_controller_task(void)
{
    if (s_task_started) return ESP_OK;
    if (xTaskCreate(controller_task, "rotary_link", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    s_task_started = true;
    return ESP_OK;
}

esp_err_t rotary_controller_uart_start(void)
{
    if (!CONFIG_ROTARY_CONTROLLER_ENABLE) return ESP_OK;
    if (!s_tx_mutex) s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) return ESP_ERR_NO_MEM;
    uart_config_t configuration = {
        .baud_rate = CONFIG_ROTARY_CONTROLLER_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_param_config(CONTROLLER_UART, &configuration), TAG, "UART config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(CONTROLLER_UART, CONFIG_ROTARY_CONTROLLER_TX_GPIO,
                                     CONFIG_ROTARY_CONTROLLER_RX_GPIO,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "UART pin config failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(CONTROLLER_UART, CONTROLLER_RX_BUFFER_SIZE, 0,
                                            0, NULL, 0), TAG, "UART driver install failed");
    s_uart_ready = true;
    esp_err_t err = start_controller_task();
    if (err != ESP_OK) {
        s_uart_ready = false;
        uart_driver_delete(CONTROLLER_UART);
        return err;
    }
    ESP_LOGI(TAG, "Controller UART ready: TX GPIO%d, RX GPIO%d, %d baud",
             CONFIG_ROTARY_CONTROLLER_TX_GPIO, CONFIG_ROTARY_CONTROLLER_RX_GPIO,
             CONFIG_ROTARY_CONTROLLER_BAUD);
    send_frame(NULL, ROTARY_PROTOCOL_PONG, ++s_tx_sequence, NULL, 0);
    return ESP_OK;
}

esp_err_t rotary_controller_net_start(void)
{
    if (s_sock >= 0) return ESP_OK;
    if (!s_tx_mutex) s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_net_queue) s_net_queue = xQueueCreate(8, sizeof(net_frame_t));
    if (!s_tx_mutex || !s_net_queue) return ESP_ERR_NO_MEM;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return ESP_FAIL;
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(KNOB_UDP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sock);
        return ESP_FAIL;
    }
    s_sock = sock;
    if (xTaskCreateWithCaps(knob_udp_task, "knob_udp", 4096, NULL, 5, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        close(sock);
        s_sock = -1;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = start_controller_task();
    ESP_LOGI(TAG, "Wireless knob link listening on UDP %d", KNOB_UDP_PORT);
    return err;
}

bool rotary_controller_send_knob_update(uint16_t port, uint32_t size, const char *version)
{
    portENTER_CRITICAL(&s_knob_lock);
    bool valid = s_knob_addr_valid;
    struct sockaddr_in to = s_knob_addr;
    portEXIT_CRITICAL(&s_knob_lock);
    if (!valid) return false;
    uint8_t payload[6 + 24] = {0};
    rotary_protocol_put_u16(&payload[0], port);
    rotary_protocol_put_u32(&payload[2], size);
    size_t n = version ? strnlen(version, 23) : 0;
    memcpy(&payload[6], version, n);
    return send_frame(&to, ROTARY_PROTOCOL_KNOB_UPDATE, ++s_tx_sequence, payload, (uint16_t)(6 + n)) == ESP_OK;
}

bool rotary_controller_wireless_status(char *version, size_t version_len)
{
    portENTER_CRITICAL(&s_knob_lock);
    int64_t last = s_knob_last_rx;
    if (version && version_len) snprintf(version, version_len, "%s", s_knob_version);
    portEXIT_CRITICAL(&s_knob_lock);
    return last && esp_timer_get_time() - last < KNOB_TIMEOUT_US;
}
