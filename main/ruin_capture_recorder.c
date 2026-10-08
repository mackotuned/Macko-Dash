#include "ruin_capture_recorder.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rotary_controller_protocol.h"
#include "theme_storage.h"

#define CAPTURE_ROOT "/sdcard/MACKODASH/CAPTURES"
#define CAPTURE_QUEUE_LENGTH 8
#define CAPTURE_MAX_FRAME_SIZE 512
#define CAPTURE_FLUSH_INTERVAL 16
#define PCAP_LINKTYPE_IEEE802_11 105

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    int32_t timezone;
    uint32_t timestamp_accuracy;
    uint32_t snapshot_length;
    uint32_t link_type;
} pcap_header_t;

typedef struct __attribute__((packed)) {
    uint32_t seconds;
    uint32_t microseconds;
    uint32_t captured_length;
    uint32_t original_length;
} pcap_record_header_t;

typedef struct {
    uint64_t timestamp_us;
    uint16_t captured_length;
    uint16_t original_length;
    uint8_t data[CAPTURE_MAX_FRAME_SIZE];
} capture_record_t;

typedef struct {
    bool active;
    uint16_t packet_id;
    uint64_t timestamp_us;
    uint16_t captured_length;
    uint16_t original_length;
    uint16_t received_length;
    uint8_t data[CAPTURE_MAX_FRAME_SIZE];
} capture_assembly_t;

static const char *TAG = "ruin_capture";
static QueueHandle_t s_record_queue;
static SemaphoreHandle_t s_file_mutex;
static FILE *s_file;
static volatile bool s_recording;
static unsigned s_records_since_flush;
static capture_assembly_t s_assembly;

static void capture_writer_task(void *argument)
{
    (void)argument;
    capture_record_t record;
    while (true) {
        if (xQueueReceive(s_record_queue, &record, portMAX_DELAY) != pdTRUE) continue;
        if (xSemaphoreTake(s_file_mutex, portMAX_DELAY) != pdTRUE) continue;
        if (s_file && s_recording) {
            pcap_record_header_t header = {
                .seconds = (uint32_t)(record.timestamp_us / 1000000),
                .microseconds = (uint32_t)(record.timestamp_us % 1000000),
                .captured_length = record.captured_length,
                .original_length = record.original_length,
            };
            bool written = fwrite(&header, sizeof(header), 1, s_file) == 1 &&
                           fwrite(record.data, record.captured_length, 1, s_file) == 1;
            if (!written) {
                ESP_LOGE(TAG, "PCAP write failed: errno=%d", errno);
            } else if (++s_records_since_flush >= CAPTURE_FLUSH_INTERVAL) {
                fflush(s_file);
                s_records_since_flush = 0;
            }
        }
        xSemaphoreGive(s_file_mutex);
    }
}

void ruin_capture_recorder_init(void)
{
    if (s_record_queue) return;
    s_record_queue = xQueueCreate(CAPTURE_QUEUE_LENGTH, sizeof(capture_record_t));
    s_file_mutex = xSemaphoreCreateMutex();
    if (!s_record_queue || !s_file_mutex ||
        xTaskCreatePinnedToCore(capture_writer_task, "ruin_pcap", 4096, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to allocate capture recorder resources");
        if (s_record_queue) vQueueDelete(s_record_queue);
        if (s_file_mutex) vSemaphoreDelete(s_file_mutex);
        s_record_queue = NULL;
        s_file_mutex = NULL;
    }
}

esp_err_t ruin_capture_recorder_start(char *filename, size_t filename_size)
{
    if (!s_record_queue || !s_file_mutex) return ESP_ERR_INVALID_STATE;
    if (!theme_storage_is_available()) return ESP_ERR_NOT_FOUND;
    if (s_recording) return ESP_OK;
    if (mkdir(CAPTURE_ROOT, 0775) != 0 && errno != EEXIST) return ESP_FAIL;

    char path[112];
    char selected_filename[20];
    struct stat file_info;
    unsigned index;
    for (index = 1; index <= 9999; ++index) {
        snprintf(selected_filename, sizeof(selected_filename), "RUIN%04u.PCAP", index);
        snprintf(path, sizeof(path), "%s/%s", CAPTURE_ROOT, selected_filename);
        if (stat(path, &file_info) != 0) break;
    }
    if (index > 9999) return ESP_ERR_NO_MEM;

    FILE *file = fopen(path, "wb");
    if (!file) return ESP_FAIL;
    const pcap_header_t header = {
        .magic = 0xa1b2c3d4,
        .major = 2,
        .minor = 4,
        .snapshot_length = CAPTURE_MAX_FRAME_SIZE,
        .link_type = PCAP_LINKTYPE_IEEE802_11,
    };
    if (fwrite(&header, sizeof(header), 1, file) != 1 || fflush(file) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    xSemaphoreTake(s_file_mutex, portMAX_DELAY);
    xQueueReset(s_record_queue);
    memset(&s_assembly, 0, sizeof(s_assembly));
    s_file = file;
    s_records_since_flush = 0;
    s_recording = true;
    xSemaphoreGive(s_file_mutex);
    if (filename && filename_size) snprintf(filename, filename_size, "%s", selected_filename);
    ESP_LOGI(TAG, "Capture recording started: %s", path);
    return ESP_OK;
}

esp_err_t ruin_capture_recorder_stop(void)
{
    if (!s_file_mutex || !s_recording) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);
    s_recording = false;
    fflush(s_file);
    fclose(s_file);
    s_file = NULL;
    xQueueReset(s_record_queue);
    memset(&s_assembly, 0, sizeof(s_assembly));
    xSemaphoreGive(s_file_mutex);
    ESP_LOGI(TAG, "Capture recording stopped");
    return ESP_OK;
}

void ruin_capture_recorder_ingest(const uint8_t *payload, uint16_t payload_length)
{
    if (!s_recording || !payload || payload_length < ROTARY_CAPTURE_CHUNK_HEADER_SIZE) return;
    uint16_t packet_id = rotary_protocol_get_u16(&payload[0]);
    uint64_t timestamp_us = rotary_protocol_get_u64(&payload[2]);
    uint16_t captured_length = rotary_protocol_get_u16(&payload[10]);
    uint16_t original_length = rotary_protocol_get_u16(&payload[12]);
    uint16_t offset = rotary_protocol_get_u16(&payload[14]);
    uint8_t flags = payload[16];
    uint16_t data_length = payload_length - ROTARY_CAPTURE_CHUNK_HEADER_SIZE;

    if (captured_length == 0 || captured_length > CAPTURE_MAX_FRAME_SIZE ||
        offset + data_length > captured_length) {
        memset(&s_assembly, 0, sizeof(s_assembly));
        return;
    }
    if (flags & ROTARY_CAPTURE_FLAG_FIRST) {
        s_assembly = (capture_assembly_t) {
            .active = true,
            .packet_id = packet_id,
            .timestamp_us = timestamp_us,
            .captured_length = captured_length,
            .original_length = original_length,
        };
    }
    if (!s_assembly.active || s_assembly.packet_id != packet_id ||
        s_assembly.timestamp_us != timestamp_us || s_assembly.captured_length != captured_length ||
        s_assembly.original_length != original_length || offset != s_assembly.received_length) {
        memset(&s_assembly, 0, sizeof(s_assembly));
        return;
    }

    memcpy(&s_assembly.data[offset], &payload[ROTARY_CAPTURE_CHUNK_HEADER_SIZE], data_length);
    s_assembly.received_length += data_length;
    if (flags & ROTARY_CAPTURE_FLAG_LAST) {
        if (s_assembly.received_length == s_assembly.captured_length) {
            capture_record_t record = {
                .timestamp_us = s_assembly.timestamp_us,
                .captured_length = s_assembly.captured_length,
                .original_length = s_assembly.original_length,
            };
            memcpy(record.data, s_assembly.data, record.captured_length);
            if (xQueueSend(s_record_queue, &record, 0) != pdTRUE) {
                ESP_LOGW(TAG, "PCAP queue full; packet dropped");
            }
        }
        memset(&s_assembly, 0, sizeof(s_assembly));
    }
}

void ruin_capture_recorder_handle_status(const uint8_t *payload, uint16_t payload_length)
{
    if (!payload || payload_length != 10) return;
    ESP_LOGI(TAG, "S3 capture %s on channel %u: %lu packets, %lu drops",
             payload[0] ? "active" : "stopped", payload[1],
             (unsigned long)rotary_protocol_get_u32(&payload[2]),
             (unsigned long)rotary_protocol_get_u32(&payload[6]));
}

bool ruin_capture_recorder_is_recording(void)
{
    return s_recording;
}
