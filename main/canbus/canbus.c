#include "canbus.h"
#include "protocol_loader.h"

#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

#include "esp_spiffs.h"

static const char *TAG = "CANBUS";

static const int can_rates[] = {
    1000000,
    500000,
    250000,
    125000
};

#define NUM_RATES (sizeof(can_rates)/sizeof(can_rates[0]))


// =======================================================
// GPIO CONFIG
// =======================================================

#define CAN_TX GPIO_NUM_47
#define CAN_RX GPIO_NUM_48

// =======================================================
// GLOBAL DATA
// =======================================================

volatile can_dash_data_t can_data = {0};
static volatile int64_t s_last_can_rx_us = 0;
static volatile int64_t s_last_gear_rx_us = 0;
static volatile int64_t s_last_rpm_rx_us = 0;
static volatile int64_t s_last_speed_rx_us = 0;
static volatile int64_t s_last_oil_pressure_rx_us = 0;
static volatile int64_t s_last_obd2_rx_us = 0;
static bool s_obd2_active = false;
static bool s_can_driver_ready = false;

#define CAN_LIVE_TIMEOUT_US 500000
#define CAN_GEAR_LIVE_TIMEOUT_US 1500000
#define CAN_DRIVETRAIN_LIVE_TIMEOUT_US 1500000
#define CAN_OPTIONAL_SIGNAL_TIMEOUT_US 30000000
#define OBD2_ACTIVE_TIMEOUT_US 2000000
#define CAN_STATE_CHECK_INTERVAL_US 250000
#define CAN_BITRATE_SCAN_PASSES 3
#define CAN_BITRATE_PROBE_COUNT 50

static const uint8_t s_obd2_pids[] = {
    0x0C, // RPM
    0x0D, // Vehicle speed (kph)
    0x05, // Coolant temp (C)
    0x0F, // IAT (C)
    0x11, // TPS (% )
    0x0B, // MAP (kPa)
    0x42  // Control module voltage (V)
};

static bool send_obd2_pid_request(uint8_t pid)
{
    twai_message_t req = {0};
    req.identifier = 0x7DF;
    req.extd = 0;
    req.rtr = 0;
    req.data_length_code = 8;
    req.data[0] = 0x02; // Two bytes follow: mode + PID
    req.data[1] = 0x01; // Show current data
    req.data[2] = pid;

    return twai_transmit(&req, 0) == ESP_OK;
}

static bool process_obd2_response(uint32_t id, const uint8_t *data)
{
    if (id < 0x7E8 || id > 0x7EF) {
        return false;
    }

    // Only handle ISO-TP single-frame replies here.
    if ((data[0] & 0xF0) != 0x00) {
        return false;
    }

    if (data[1] != 0x41) {
        return false;
    }

    int64_t now_us = esp_timer_get_time();
    uint8_t pid = data[2];

    switch (pid) {
        case 0x0C: { // RPM = ((A*256)+B)/4
            uint16_t raw = ((uint16_t)data[3] << 8) | data[4];
            can_data.rpm = (float)raw * 0.25f;
            s_last_rpm_rx_us = now_us;
            break;
        }
        case 0x0D: { // Speed kph
            can_data.speed = (float)data[3];
            s_last_speed_rx_us = now_us;
            break;
        }
        case 0x05: { // Coolant C -> F
            float c = (float)data[3] - 40.0f;
            can_data.coolant_temp = c * 1.8f + 32.0f;
            break;
        }
        case 0x0F: { // IAT C -> F
            float c = (float)data[3] - 40.0f;
            can_data.air_temp = c * 1.8f + 32.0f;
            break;
        }
        case 0x11: { // TPS %
            can_data.tps = ((float)data[3] * 100.0f) / 255.0f;
            break;
        }
        case 0x0B: { // MAP kPa -> gauge psi
            float map_kpa = (float)data[3];
            can_data.boost = (map_kpa * 0.1450377f) - 14.6959f;
            break;
        }
        case 0x42: { // Voltage = ((A*256)+B)/1000
            uint16_t raw = ((uint16_t)data[3] << 8) | data[4];
            can_data.battery_voltage = (float)raw / 1000.0f;
            break;
        }
        default:
            return false;
    }

    s_last_obd2_rx_us = now_us;
    s_obd2_active = true;
    return true;
}


// =======================================================
// CAN FRAME PROCESSOR
// =======================================================

void process_can_frame(uint32_t id, uint8_t *data){
    protocol_detect(id);

    if(!active_protocol)
        return;

    if(id >= CAN_ID_MAX)
        return;

    can_frame_def_t *frame = frame_lookup[id];

    if(!frame)
        return;

    int64_t now_us = esp_timer_get_time();

    for(int s=0;s<frame->signal_count;s++){
        can_signal_t *sig = &frame->signals[s];

        uint32_t raw = 0;

        if(sig->len==2){
            if(sig->endian==ENDIAN_BIG)
                raw=(data[sig->offset]<<8)|data[sig->offset+1];
            else
                raw=(data[sig->offset+1]<<8)|data[sig->offset];
        }
        else{
            raw=data[sig->offset];
        }

        if(sig->target) {
            *sig->target = raw*sig->scale + sig->offset_val;
            if (sig->target == &can_data.gear) {
                s_last_gear_rx_us = now_us;
            } else if (sig->target == &can_data.rpm) {
                s_last_rpm_rx_us = now_us;
            } else if (sig->target == &can_data.speed) {
                s_last_speed_rx_us = now_us;
            } else if (sig->target == &can_data.oil_pressure) {
                s_last_oil_pressure_rx_us = now_us;
            }
        }
    }
}


void mount_fs() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true
    };

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
}

static twai_timing_config_t get_timing(int bitrate){
    twai_timing_config_t t;

    switch (bitrate){
        case 1000000:
            t = (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
            break;

        case 500000:
            t = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
            break;

        case 250000:
            t = (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
            break;

        case 125000:
            t = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
            break;

        default:
            t = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
            break;
    }

    return t;
}


int detect_can_bitrate()
{
    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX, CAN_RX, TWAI_MODE_NORMAL);

    twai_filter_config_t f_config =
        TWAI_FILTER_CONFIG_ACCEPT_ALL();

    twai_message_t msg;

    for (int pass = 0; pass < CAN_BITRATE_SCAN_PASSES; pass++){
        for (int i = 0; i < NUM_RATES; i++){
            int rate = can_rates[i];

            twai_timing_config_t t_config = get_timing(rate);

            ESP_LOGI(TAG, "Trying CAN bitrate %d (pass %d/%d)",
                     rate, pass + 1, CAN_BITRATE_SCAN_PASSES);

            ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
            ESP_ERROR_CHECK(twai_start());

            int frames = 0;
            int timeout = CAN_BITRATE_PROBE_COUNT;

            while (timeout--){
                if (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK){
                    if (msg.extd || msg.rtr)
                        continue;

                    frames++;

                    if (frames >= 3){
                        ESP_LOGI(TAG, "Detected CAN bitrate %d", rate);

                        twai_stop();
                        twai_driver_uninstall();

                        return rate;
                    }
                }
            }

            twai_stop();
            twai_driver_uninstall();
        }
    }

    ESP_LOGW(TAG, "CAN bitrate detection failed, defaulting to 500k");

    return 500000;
}


// =======================================================
// CAN INIT
// =======================================================

void canbus_init(void)
{
    mount_fs();

    protocol_loader_init();

    int bitrate;
    if (active_protocol && active_protocol->bitrate > 0) {
        bitrate = active_protocol->bitrate;
        ESP_LOGI(TAG, "Using %s protocol bitrate %d", active_protocol->name, bitrate);
    } else {
        bitrate = detect_can_bitrate();
    }

    twai_general_config_t g_config =
        TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX, CAN_RX, TWAI_MODE_NORMAL);

    twai_timing_config_t t_config = get_timing(bitrate);

    twai_filter_config_t f_config =
        TWAI_FILTER_CONFIG_ACCEPT_ALL();

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_ERROR_CHECK(twai_start());
    s_can_driver_ready = true;

    ESP_LOGI(TAG, "CAN initialized at %d", bitrate);
}

void canbus_shutdown(void)
{
    s_obd2_active = false;
    s_last_can_rx_us = 0;
    s_last_gear_rx_us = 0;
    s_last_rpm_rx_us = 0;
    s_last_speed_rx_us = 0;
    s_last_oil_pressure_rx_us = 0;

    active_protocol = NULL;
    memset(frame_lookup, 0, sizeof(frame_lookup));

    if (!s_can_driver_ready) {
        return;
    }

    esp_err_t stop_err = twai_stop();
    if (stop_err != ESP_OK) {
        ESP_LOGW(TAG, "twai_stop failed during shutdown: %s", esp_err_to_name(stop_err));
    }

    esp_err_t uninstall_err = twai_driver_uninstall();
    if (uninstall_err != ESP_OK) {
        ESP_LOGW(TAG, "twai_driver_uninstall failed during shutdown: %s", esp_err_to_name(uninstall_err));
    }

    s_can_driver_ready = false;
}


// =======================================================
// CAN RECEIVE TASK
// =======================================================

void canbus_task(void *arg){
    twai_message_t message;
    uint32_t frames_since_yield = 0;
    int64_t last_pid_tx_us = 0;
    int64_t last_state_check_us = 0;
    size_t pid_index = 0;

    while (1){
        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_state_check_us) >= CAN_STATE_CHECK_INTERVAL_US) {
            twai_status_info_t status;
            last_state_check_us = now_us;

            if (twai_get_status_info(&status) == ESP_OK) {
                if (status.state == TWAI_STATE_BUS_OFF) {
                    esp_err_t err = twai_initiate_recovery();
                    if (err == ESP_OK) {
                        ESP_LOGW(TAG, "CAN bus-off; recovery started");
                    } else if (err != ESP_ERR_INVALID_STATE) {
                        ESP_LOGW(TAG, "CAN recovery failed: %s", esp_err_to_name(err));
                    }
                } else if (status.state == TWAI_STATE_STOPPED) {
                    esp_err_t err = twai_start();
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, "CAN controller restarted after recovery");
                    } else if (err != ESP_ERR_INVALID_STATE) {
                        ESP_LOGW(TAG, "CAN restart failed: %s", esp_err_to_name(err));
                    }
                }
            }
        }

        if (s_obd2_active && (now_us - s_last_obd2_rx_us) > OBD2_ACTIVE_TIMEOUT_US) {
            s_obd2_active = false;
        }

        // If drivetrain is not live, start OBD2 polling and keep polling while active.
        if ((s_obd2_active || !canbus_has_live_drivetrain()) &&
            (now_us - last_pid_tx_us) >= 80000) {
            if (send_obd2_pid_request(s_obd2_pids[pid_index])) {
                pid_index = (pid_index + 1) % (sizeof(s_obd2_pids) / sizeof(s_obd2_pids[0]));
                last_pid_tx_us = now_us;
            }
        }

        if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK){
            if (!message.extd && !message.rtr) {
                s_last_can_rx_us = esp_timer_get_time();
                (void)process_obd2_response(message.identifier, message.data);
                process_can_frame(message.identifier, message.data);
                frames_since_yield++;

                // Prevent CPU starvation on very busy CAN buses.
                if (frames_since_yield >= 32) {
                    frames_since_yield = 0;
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
        } else {
            frames_since_yield = 0;
        }
    }
}

bool canbus_has_live_data(void)
{
    int64_t last = s_last_can_rx_us;
    if (last == 0) {
        return false;
    }
    return (esp_timer_get_time() - last) <= CAN_LIVE_TIMEOUT_US;
}

bool canbus_has_live_gear(void)
{
    int64_t last = s_last_gear_rx_us;
    if (last == 0) {
        return false;
    }
    return (esp_timer_get_time() - last) <= CAN_GEAR_LIVE_TIMEOUT_US;
}

bool canbus_has_live_drivetrain(void)
{
    int64_t now = esp_timer_get_time();
    int64_t rpm_last = s_last_rpm_rx_us;
    int64_t speed_last = s_last_speed_rx_us;

    if (rpm_last == 0 || speed_last == 0) {
        return false;
    }

    return (now - rpm_last) <= CAN_DRIVETRAIN_LIVE_TIMEOUT_US &&
           (now - speed_last) <= CAN_DRIVETRAIN_LIVE_TIMEOUT_US;
}

bool canbus_has_recent_oil_pressure(void)
{
    int64_t last = s_last_oil_pressure_rx_us;
    return last != 0 && (esp_timer_get_time() - last) <= CAN_OPTIONAL_SIGNAL_TIMEOUT_US;
}