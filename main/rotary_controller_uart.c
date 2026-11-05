#include "rotary_controller_uart.h"

#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "honda_dash_ui.h"
#include "rotary_controller_protocol.h"

#define CONTROLLER_UART UART_NUM_1
#define CONTROLLER_RX_BUFFER_SIZE 512

#ifndef CONFIG_ROTARY_CONTROLLER_ENABLE
#define CONFIG_ROTARY_CONTROLLER_ENABLE 0
#endif

static const char *TAG = "rotary_uart";
static uint8_t s_tx_sequence;
static SemaphoreHandle_t s_tx_mutex;

typedef struct {
    uint8_t data[ROTARY_PROTOCOL_MAX_FRAME];
    size_t length;
    size_t expected_length;
} frame_parser_t;

static esp_err_t send_frame(rotary_protocol_type_t type, uint8_t sequence,
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
    int written = uart_write_bytes(CONTROLLER_UART, frame,
                                   crc_offset + ROTARY_PROTOCOL_CRC_SIZE);
    xSemaphoreGive(s_tx_mutex);
    return written == (int)(crc_offset + ROTARY_PROTOCOL_CRC_SIZE) ? ESP_OK : ESP_FAIL;
}

static void send_state(uint8_t sequence, const honda_dash_controller_state_t *state)
{
    uint8_t payload[ROTARY_PROTOCOL_MAX_PAYLOAD] = {0};
    payload[0] = (uint8_t)state->page;
    rotary_protocol_put_u16(&payload[1], (uint16_t)state->selection);
    rotary_protocol_put_u16(&payload[3], (uint16_t)state->count);
    rotary_protocol_put_u32(&payload[5], (uint32_t)state->value);
    payload[9] = state->recording ? 1 : 0;
    /* bytes 10..18 (air pressure) are kept zero for protocol compatibility */
    size_t label_length = strnlen(state->label, sizeof(state->label));
    if (label_length > ROTARY_PROTOCOL_MAX_PAYLOAD - 19) {
        label_length = ROTARY_PROTOCOL_MAX_PAYLOAD - 19;
    }
    memcpy(&payload[19], state->label, label_length);
    send_frame(ROTARY_PROTOCOL_STATE, sequence, payload, (uint16_t)(19 + label_length));
}

static void handle_frame(const uint8_t *frame, size_t frame_length)
{
    uint16_t payload_length = rotary_protocol_get_u16(&frame[5]);
    if (frame_length != ROTARY_PROTOCOL_HEADER_SIZE + payload_length + ROTARY_PROTOCOL_CRC_SIZE) return;
    uint16_t expected_crc = rotary_protocol_get_u16(&frame[frame_length - ROTARY_PROTOCOL_CRC_SIZE]);
    if (rotary_protocol_crc16(frame, frame_length - ROTARY_PROTOCOL_CRC_SIZE) != expected_crc) return;

    rotary_protocol_type_t type = (rotary_protocol_type_t)frame[3];
    uint8_t sequence = frame[4];
    const uint8_t *payload = &frame[ROTARY_PROTOCOL_HEADER_SIZE];

    honda_dash_controller_event_t event = HONDA_DASH_CONTROLLER_GET_STATE;
    int32_t value = 0;

    if (type == ROTARY_PROTOCOL_EVENT) {
        if (payload_length != 3 || payload[0] > HONDA_DASH_CONTROLLER_GET_STATE) return;
        event = (honda_dash_controller_event_t)payload[0];
        value = (int16_t)rotary_protocol_get_u16(&payload[1]);
    } else if (type == ROTARY_PROTOCOL_PING) {
        send_frame(ROTARY_PROTOCOL_PONG, sequence, NULL, 0);
    } else if (type != ROTARY_PROTOCOL_GET_STATE) {
        return;
    }

    honda_dash_controller_state_t state;
    if (bsp_display_lock(1000) != ESP_OK) return;
    bool handled = honda_dash_ui_controller_handle(event, value, &state);
    bsp_display_unlock();
    if (handled) send_state(sequence, &state);
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
        handle_frame(parser->data, parser->length);
        parser_reset(parser);
    } else if (parser->length >= sizeof(parser->data)) {
        parser_reset(parser);
    }
}

static void controller_uart_task(void *argument)
{
    (void)argument;
    frame_parser_t parser = {0};
    uint8_t buffer[64];
    while (true) {
        int received = uart_read_bytes(CONTROLLER_UART, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
        for (int index = 0; index < received; ++index) parser_push(&parser, buffer[index]);
    }
}

esp_err_t rotary_controller_uart_start(void)
{
    if (!CONFIG_ROTARY_CONTROLLER_ENABLE) return ESP_OK;
    s_tx_mutex = xSemaphoreCreateMutex();
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
    if (xTaskCreate(controller_uart_task, "rotary_uart", 4096, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(CONTROLLER_UART);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Controller UART ready: TX GPIO%d, RX GPIO%d, %d baud",
             CONFIG_ROTARY_CONTROLLER_TX_GPIO, CONFIG_ROTARY_CONTROLLER_RX_GPIO,
             CONFIG_ROTARY_CONTROLLER_BAUD);
    send_frame(ROTARY_PROTOCOL_PONG, ++s_tx_sequence, NULL, 0);
    return ESP_OK;
}

