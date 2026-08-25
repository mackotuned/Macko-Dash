#include "data_logger.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "theme_storage.h"

#define DATA_LOG_ROOT "/sdcard/MACKODASH/LOGS"
#define DATA_LOG_QUEUE_LENGTH 16
#define DATA_LOG_PERIOD_US 100000

typedef struct {
    int64_t timestamp_us;
    honda_dash_data_t data;
} data_log_sample_t;

static const char *TAG = "data_logger";
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_file_mutex;
static FILE *s_file;
static volatile bool s_recording;
static int64_t s_started_us;
static int64_t s_last_submit_us;
static unsigned s_rows_since_flush;
static char s_filename[20];

static void copy_filename(char *destination, size_t destination_size)
{
    if (destination && destination_size > 0) {
        snprintf(destination, destination_size, "%s", s_filename);
    }
}

static void write_sample(const data_log_sample_t *sample)
{
    const honda_dash_data_t *data = &sample->data;
    long long elapsed_ms = (sample->timestamp_us - s_started_us) / 1000;
    fprintf(s_file,
            "%lld,%u,%.2f,%d,%.2f,%.2f,%.3f,%.2f,%.2f,%.3f,%.2f,%.2f,%d,%.2f,%d,%.2f,%d,%d,%.3f,%.2f\n",
            elapsed_ms, (unsigned)data->rpm, (double)data->speed_mph, (int)data->gear,
            (double)data->ect_f, (double)data->iat_f, (double)data->afr,
            (double)data->timing_deg, (double)data->map_psi, (double)data->batt_v,
            (double)data->tps_pct, (double)data->oil_psi, data->oil_valid ? 1 : 0,
            (double)data->duty_pct, data->duty_valid ? 1 : 0,
            (double)data->knock_deg, data->knock_valid ? 1 : 0, data->cel ? 1 : 0,
            data->odo_miles, (double)data->fuel_pct);
    if (++s_rows_since_flush >= 10) {
        fflush(s_file);
        s_rows_since_flush = 0;
    }
}

static void data_logger_task(void *argument)
{
    (void)argument;
    data_log_sample_t sample;
    while (true) {
        if (xQueueReceive(s_queue, &sample, portMAX_DELAY) == pdTRUE &&
            xSemaphoreTake(s_file_mutex, portMAX_DELAY) == pdTRUE) {
            if (s_file && s_recording) {
                write_sample(&sample);
            }
            xSemaphoreGive(s_file_mutex);
        }
    }
}

void data_logger_init(void)
{
    if (s_queue) return;
    s_queue = xQueueCreate(DATA_LOG_QUEUE_LENGTH, sizeof(data_log_sample_t));
    s_file_mutex = xSemaphoreCreateMutex();
    if (!s_queue || !s_file_mutex) {
        ESP_LOGE(TAG, "Failed to allocate logger resources");
        return;
    }
    if (xTaskCreatePinnedToCore(data_logger_task, "data_logger", 4096, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start logger task");
        vQueueDelete(s_queue);
        vSemaphoreDelete(s_file_mutex);
        s_queue = NULL;
        s_file_mutex = NULL;
    }
}

esp_err_t data_logger_start(char *filename, size_t filename_size)
{
    if (!s_queue || !s_file_mutex) return ESP_ERR_INVALID_STATE;
    if (!theme_storage_is_available()) return ESP_ERR_NOT_FOUND;
    if (s_recording) {
        copy_filename(filename, filename_size);
        return ESP_OK;
    }
    if (mkdir(DATA_LOG_ROOT, 0775) != 0 && errno != EEXIST) return ESP_FAIL;

    char path[96];
    struct stat file_info;
    unsigned index;
    for (index = 1; index <= 9999; ++index) {
        snprintf(s_filename, sizeof(s_filename), "LOG%04u.CSV", index);
        snprintf(path, sizeof(path), "%s/%s", DATA_LOG_ROOT, s_filename);
        if (stat(path, &file_info) != 0) break;
    }
    if (index > 9999) return ESP_ERR_NO_MEM;

    FILE *file = fopen(path, "w");
    if (!file) {
        ESP_LOGE(TAG, "Cannot open %s: errno=%d", path, errno);
        return ESP_FAIL;
    }
    fputs("elapsed_ms,rpm,speed_mph,gear,coolant_f,intake_f,afr,timing_deg,map_psi,battery_v,tps_pct,oil_psi,oil_valid,duty_pct,duty_valid,knock_deg,knock_valid,cel,odometer_miles,fuel_pct\n",
          file);
    fflush(file);

    xSemaphoreTake(s_file_mutex, portMAX_DELAY);
    xQueueReset(s_queue);
    s_file = file;
    s_started_us = esp_timer_get_time();
    s_last_submit_us = 0;
    s_rows_since_flush = 0;
    s_recording = true;
    xSemaphoreGive(s_file_mutex);
    copy_filename(filename, filename_size);
    ESP_LOGI(TAG, "Recording started: %s", path);
    return ESP_OK;
}

esp_err_t data_logger_stop(char *filename, size_t filename_size)
{
    if (!s_file_mutex || !s_recording) return ESP_ERR_INVALID_STATE;
    copy_filename(filename, filename_size);
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);
    s_recording = false;
    fflush(s_file);
    fclose(s_file);
    s_file = NULL;
    xQueueReset(s_queue);
    xSemaphoreGive(s_file_mutex);
    ESP_LOGI(TAG, "Recording stopped: %s", s_filename);
    return ESP_OK;
}

void data_logger_submit(const honda_dash_data_t *data)
{
    if (!data || !s_recording || !s_queue) return;
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_submit_us < DATA_LOG_PERIOD_US) return;
    s_last_submit_us = now_us;
    data_log_sample_t sample = {.timestamp_us = now_us, .data = *data};
    if (xQueueSend(s_queue, &sample, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Sample queue full; row dropped");
    }
}

bool data_logger_is_recording(void)
{
    return s_recording;
}