#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hvac_protocol.h"
#include "nvs_flash.h"

#define STEP_MS 60
#define STATE_PERIOD_MS 500
#define LINK_TIMEOUT_US 2000000LL
#define COMMAND_QUEUE_LENGTH 1

static const char *TAG = "hvac_node";
static const uint8_t S3_STA_MAC[ESP_NOW_ETH_ALEN] = {0xf4, 0x65, 0x0b, 0xa9, 0xdd, 0xc4};
static const uint8_t ESPNOW_PMK[ESP_NOW_KEY_LEN] = {
    0x4d, 0x61, 0x63, 0x6b, 0x6f, 0x44, 0x61, 0x73,
    0x68, 0x48, 0x56, 0x41, 0x43, 0x50, 0x4d, 0x4b,
};
static const uint8_t ESPNOW_LMK[ESP_NOW_KEY_LEN] = {
    0x43, 0x69, 0x76, 0x69, 0x63, 0x48, 0x56, 0x41,
    0x43, 0x4c, 0x69, 0x6e, 0x6b, 0x4c, 0x4d, 0x4b,
};

static const uint8_t TEMP_CODES[] = {1, 3, 2, 6, 7, 5, 4, 12, 13, 15, 14, 10, 11, 9, 8};
static const uint8_t MODE_CODES[] = {1, 2, 7, 4, 14};
static const uint8_t FAN_CODES[] = {1, 7, 5, 4, 12, 13, 15, 14, 10, 11, 9};

typedef struct {
    const char *name;
    gpio_num_t pins[4];
    const uint8_t *codes;
    uint8_t count;
    volatile uint8_t pos;
    volatile uint8_t target;
    volatile bool raw_override;
} dial_t;

enum { FAN, MODE, TEMP, DIAL_COUNT };

static dial_t s_dials[DIAL_COUNT] = {
    [FAN] = {"fan", {GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19},
             FAN_CODES, sizeof(FAN_CODES), 0, 0, false},
    [MODE] = {"mode", {GPIO_NUM_23, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27},
              MODE_CODES, sizeof(MODE_CODES), 0, 0, false},
    [TEMP] = {"temp", {GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_4, GPIO_NUM_13},
              TEMP_CODES, sizeof(TEMP_CODES), 7, 7, false},
};

static QueueHandle_t s_command_queue;
static volatile int64_t s_last_command_us;
static volatile uint16_t s_last_sequence;
static volatile uint8_t s_fault_flags = HVAC_FAULT_LINK_TIMEOUT;

static void write_code(const dial_t *dial, uint8_t code)
{
    for (int index = 0; index < 4; ++index) {
        gpio_set_level(dial->pins[index], (code >> index) & 1);
    }
}

static void set_target(dial_t *dial, uint8_t target)
{
    if (target >= dial->count) target = dial->count - 1;
    if (dial->raw_override) {
        dial->pos = target;
        write_code(dial, dial->codes[target]);
        dial->raw_override = false;
    }
    dial->target = target;
}

static uint8_t moving_mask(void)
{
    uint8_t mask = 0;
    if (s_dials[FAN].pos != s_dials[FAN].target) mask |= HVAC_MOVING_FAN;
    if (s_dials[MODE].pos != s_dials[MODE].target) mask |= HVAC_MOVING_MODE;
    if (s_dials[TEMP].pos != s_dials[TEMP].target) mask |= HVAC_MOVING_TEMP;
    return mask;
}

static void step_task(void *argument)
{
    (void)argument;
    while (true) {
        for (int index = 0; index < DIAL_COUNT; ++index) {
            dial_t *dial = &s_dials[index];
            if (dial->raw_override || dial->pos == dial->target) continue;
            dial->pos += dial->target > dial->pos ? 1 : -1;
            write_code(dial, dial->codes[dial->pos]);
        }
        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}

static esp_err_t send_state(void)
{
    hvac_state_packet_t packet = {
        .magic = HVAC_PROTOCOL_MAGIC,
        .version = HVAC_PROTOCOL_VERSION,
        .type = HVAC_MESSAGE_STATE,
        .sequence = s_last_sequence,
        .fan = s_dials[FAN].pos,
        .mode = s_dials[MODE].pos,
        .temp = s_dials[TEMP].pos,
        .target_fan = s_dials[FAN].target,
        .target_mode = s_dials[MODE].target,
        .target_temp = s_dials[TEMP].target,
        .moving_mask = moving_mask(),
        .fault_flags = s_fault_flags,
    };
    packet.crc = hvac_crc16(&packet, offsetof(hvac_state_packet_t, crc));
    return esp_now_send(S3_STA_MAC, (const uint8_t *)&packet, sizeof(packet));
}

static void receive_callback(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (!info || !data || length != sizeof(hvac_command_packet_t) ||
        memcmp(info->src_addr, S3_STA_MAC, ESP_NOW_ETH_ALEN) != 0) {
        return;
    }
    hvac_command_packet_t packet;
    memcpy(&packet, data, sizeof(packet));
    if (!hvac_command_valid(&packet)) {
        s_fault_flags |= HVAC_FAULT_BAD_COMMAND;
        return;
    }
    xQueueOverwrite(s_command_queue, &packet);
}

static void command_task(void *argument)
{
    (void)argument;
    hvac_command_packet_t packet;
    int64_t last_state_us = 0;
    while (true) {
        if (xQueueReceive(s_command_queue, &packet, pdMS_TO_TICKS(50)) == pdTRUE) {
            set_target(&s_dials[FAN], packet.fan);
            set_target(&s_dials[MODE], packet.mode);
            set_target(&s_dials[TEMP], packet.temp);
            s_last_sequence = packet.sequence;
            s_last_command_us = esp_timer_get_time();
            s_fault_flags = 0;
            ESP_LOGI(TAG, "command %u: fan=%u mode=%u temp=%u", packet.sequence,
                     packet.fan, packet.mode, packet.temp);
            ESP_ERROR_CHECK_WITHOUT_ABORT(send_state());
        }
        int64_t now = esp_timer_get_time();
        if (s_last_command_us == 0 || now - s_last_command_us > LINK_TIMEOUT_US) {
            s_fault_flags |= HVAC_FAULT_LINK_TIMEOUT;
        }
        if (now - last_state_us >= STATE_PERIOD_MS * 1000LL) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(send_state());
            last_state_us = now;
        }
    }
}

static void network_init(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
    ESP_ERROR_CHECK(esp_netif_init());
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(error);

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(HVAC_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_set_pmk(ESPNOW_PMK));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(receive_callback));

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, S3_STA_MAC, ESP_NOW_ETH_ALEN);
    memcpy(peer.lmk, ESPNOW_LMK, ESP_NOW_KEY_LEN);
    peer.channel = HVAC_ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_LOGI(TAG, "ESP-NOW channel %d, STA %02X:%02X:%02X:%02X:%02X:%02X",
             HVAC_ESPNOW_CHANNEL, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static dial_t *dial_by_name(const char *name)
{
    for (int index = 0; index < DIAL_COUNT; ++index) {
        if (strcmp(name, s_dials[index].name) == 0) return &s_dials[index];
    }
    return NULL;
}

static void print_status(void)
{
    printf("fan %u/%u  mode %u/%u  temp %u/%u  moving=0x%02x faults=0x%02x\n",
           s_dials[FAN].pos, s_dials[FAN].target,
           s_dials[MODE].pos, s_dials[MODE].target,
           s_dials[TEMP].pos, s_dials[TEMP].target, moving_mask(), s_fault_flags);
}

static void handle_command(char *line)
{
    char name[8];
    int value;
    if (strcmp(line, "status") == 0) {
        print_status();
        return;
    }
    if (strcmp(line, "help") == 0) {
        printf("fan N | mode N | temp N | raw DIAL CODE | off | status\n");
        return;
    }
    if (strcmp(line, "off") == 0) {
        for (int index = 0; index < DIAL_COUNT; ++index) {
            write_code(&s_dials[index], 0);
            s_dials[index].raw_override = true;
        }
        printf("all relay outputs open; next position command resynchronizes them\n");
        return;
    }
    if (sscanf(line, "raw %7s %d", name, &value) == 2) {
        dial_t *dial = dial_by_name(name);
        if (dial && value >= 0 && value <= 15) {
            write_code(dial, (uint8_t)value);
            dial->raw_override = true;
            printf("%s raw code %d\n", dial->name, value);
            return;
        }
    } else if (sscanf(line, "%7s %d", name, &value) == 2) {
        dial_t *dial = dial_by_name(name);
        if (dial && value >= 0 && value < dial->count) {
            set_target(dial, (uint8_t)value);
            printf("%s -> %u\n", dial->name, dial->target);
            return;
        }
    }
    printf("? type help\n");
}

static void console_task(void *argument)
{
    (void)argument;
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    char line[48];
    int length = 0;
    while (true) {
        uint8_t byte;
        if (uart_read_bytes(UART_NUM_0, &byte, 1, portMAX_DELAY) != 1) continue;
        if (byte == '\r' || byte == '\n') {
            if (length == 0) continue;
            line[length] = '\0';
            for (int index = 0; index < length; ++index) {
                if (line[index] >= 'A' && line[index] <= 'Z') line[index] += 'a' - 'A';
            }
            handle_command(line);
            length = 0;
        } else if ((byte == 8 || byte == 127) && length > 0) {
            --length;
        } else if (byte >= ' ' && length < (int)sizeof(line) - 1) {
            line[length++] = (char)byte;
        }
    }
}

void app_main(void)
{
    uint64_t pin_mask = 0;
    for (int dial = 0; dial < DIAL_COUNT; ++dial) {
        for (int pin = 0; pin < 4; ++pin) pin_mask |= 1ULL << s_dials[dial].pins[pin];
    }
    gpio_config_t output_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&output_config));
    for (int index = 0; index < DIAL_COUNT; ++index) {
        write_code(&s_dials[index], s_dials[index].codes[s_dials[index].pos]);
    }

    s_command_queue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(hvac_command_packet_t));
    ESP_ERROR_CHECK(s_command_queue ? ESP_OK : ESP_ERR_NO_MEM);
    network_init();
    ESP_ERROR_CHECK(xTaskCreate(step_task, "hvac_step", 2048, NULL, 6, NULL) == pdPASS ?
                    ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(command_task, "hvac_link", 4096, NULL, 5, NULL) == pdPASS ?
                    ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(console_task, "hvac_console", 4096, NULL, 3, NULL) == pdPASS ?
                    ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "MackoDash HVAC node ready");
    print_status();
}
