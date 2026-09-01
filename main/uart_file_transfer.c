#include "uart_file_transfer.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "data_logger.h"
#include "device_log_viewer.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "theme_storage.h"

#define TRANSFER_UART UART_NUM_0
#define TRANSFER_BAUD CONFIG_ESP_CONSOLE_UART_BAUDRATE
#define LOG_DIRECTORY "/sdcard/MACKODASH/LOGS"
#define THEME_DIRECTORY "/sdcard/MACKODASH/THEMES"
#define THEME_UPLOAD_LIMIT (8U * 1024U * 1024U)
#define IO_BUFFER_SIZE 1024

static const char *TAG = "uart_files";

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
        }
    }
    return crc;
}

static void uart_send(const void *data, size_t size)
{
    uart_write_bytes(TRANSFER_UART, data, size);
}

static void uart_send_line(const char *line)
{
    uart_send(line, strlen(line));
    uart_send("\n", 1);
}

static bool valid_log_filename(const char *filename)
{
    size_t length = filename ? strlen(filename) : 0;
    if (length < 5 || length >= sizeof(((device_log_file_t *)0)->filename) ||
            strcmp(filename + length - 4, ".CSV") != 0) return false;
    for (size_t index = 0; index < length - 4; ++index) {
        unsigned char character = (unsigned char)filename[index];
        if (!isalnum(character) && character != '_' && character != '-') return false;
    }
    return true;
}

static bool has_theme_suffix(const char *filename)
{
    static const char suffix[] = ".mdtheme.zip";
    size_t filename_length = strlen(filename);
    size_t suffix_length = sizeof(suffix) - 1;
    if (filename_length <= suffix_length) return false;
    const char *tail = filename + filename_length - suffix_length;
    for (size_t index = 0; index < suffix_length; ++index) {
        if (tolower((unsigned char)tail[index]) != suffix[index]) return false;
    }
    return true;
}

static bool valid_theme_filename(const char *filename)
{
    size_t length = filename ? strlen(filename) : 0;
    if (length == 0 || length >= THEME_STORAGE_NAME_MAX || !has_theme_suffix(filename)) return false;
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)filename[index];
        if (!isalnum(character) && character != '-' && character != '_' && character != '.') return false;
    }
    return true;
}

static int read_exact(uint8_t *buffer, size_t size)
{
    size_t received = 0;
    while (received < size) {
        int count = uart_read_bytes(TRANSFER_UART, buffer + received, size - received,
                                    pdMS_TO_TICKS(10000));
        if (count <= 0) return -1;
        received += (size_t)count;
    }
    return (int)received;
}

static void handle_list(void)
{
    device_log_file_t files[DEVICE_LOG_MAX_FILES];
    size_t count = device_log_list(files, DEVICE_LOG_MAX_FILES);
    char response[96];
    snprintf(response, sizeof(response), "MDP1 LIST %u", (unsigned)count);
    uart_send_line(response);
    for (size_t index = 0; index < count; ++index) {
        snprintf(response, sizeof(response), "MDP1 FILE %s %lu", files[index].filename,
                 (unsigned long)files[index].size_bytes);
        uart_send_line(response);
    }
    uart_send_line("MDP1 END");
}

static void handle_get(const char *filename)
{
    if (!valid_log_filename(filename)) {
        uart_send_line("MDP1 ERROR INVALID_NAME");
        return;
    }
    if (data_logger_is_recording()) {
        uart_send_line("MDP1 ERROR RECORDING_ACTIVE");
        return;
    }
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", LOG_DIRECTORY, filename);
    FILE *file = fopen(path, "rb");
    if (!file) {
        uart_send_line("MDP1 ERROR NOT_FOUND");
        return;
    }
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size < 0) {
        fclose(file);
        uart_send_line("MDP1 ERROR READ_FAILED");
        return;
    }

    esp_log_level_set("*", ESP_LOG_NONE);
    char header[96];
    snprintf(header, sizeof(header), "MDP1 DATA %s %lu", filename, (unsigned long)info.st_size);
    uart_send_line(header);
    uint8_t buffer[IO_BUFFER_SIZE];
    uint32_t crc = UINT32_MAX;
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        crc = crc32_update(crc, buffer, count);
        uart_send(buffer, count);
    }
    fclose(file);
    crc ^= UINT32_MAX;
    snprintf(header, sizeof(header), "\nMDP1 DONE %08lX", (unsigned long)crc);
    uart_send_line(header);
    uart_wait_tx_done(TRANSFER_UART, pdMS_TO_TICKS(5000));
    esp_log_level_set("*", (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);
}

static void handle_put_theme(const char *arguments)
{
    char filename[THEME_STORAGE_NAME_MAX];
    unsigned long declared_size;
    unsigned long declared_crc;
    if (sscanf(arguments, "%95s %lu %lx", filename, &declared_size, &declared_crc) != 3 ||
        !valid_theme_filename(filename) || declared_size == 0 || declared_size > THEME_UPLOAD_LIMIT) {
        uart_send_line("MDP1 ERROR INVALID_UPLOAD");
        return;
    }
    if (!theme_storage_is_available()) {
        uart_send_line("MDP1 ERROR SD_NOT_FOUND");
        return;
    }
    if (data_logger_is_recording()) {
        uart_send_line("MDP1 ERROR RECORDING_ACTIVE");
        return;
    }

    char final_path[THEME_STORAGE_PATH_MAX];
    char temporary_path[THEME_STORAGE_PATH_MAX];
    snprintf(final_path, sizeof(final_path), "%s/%s", THEME_DIRECTORY, filename);
    snprintf(temporary_path, sizeof(temporary_path), "%s/.upload.tmp", THEME_DIRECTORY);
    FILE *file = fopen(temporary_path, "wb");
    if (!file) {
        uart_send_line("MDP1 ERROR OPEN_FAILED");
        return;
    }

    esp_log_level_set("*", ESP_LOG_NONE);
    uart_send_line("MDP1 READY");
    uint8_t buffer[IO_BUFFER_SIZE];
    size_t remaining = (size_t)declared_size;
    uint32_t crc = UINT32_MAX;
    bool success = true;
    while (remaining > 0) {
        size_t block = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (read_exact(buffer, block) < 0 || fwrite(buffer, 1, block, file) != block) {
            success = false;
            break;
        }
        crc = crc32_update(crc, buffer, block);
        remaining -= block;
    }
    if (fflush(file) != 0) success = false;
    if (fclose(file) != 0) success = false;
    crc ^= UINT32_MAX;
    if (!success || remaining != 0 || crc != (uint32_t)declared_crc) {
        remove(temporary_path);
        uart_send_line("MDP1 ERROR VERIFY_FAILED");
        uart_wait_tx_done(TRANSFER_UART, pdMS_TO_TICKS(2000));
        esp_log_level_set("*", (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);
        return;
    }
    remove(final_path);
    if (rename(temporary_path, final_path) != 0) {
        remove(temporary_path);
        uart_send_line("MDP1 ERROR INSTALL_FAILED");
        uart_wait_tx_done(TRANSFER_UART, pdMS_TO_TICKS(2000));
        esp_log_level_set("*", (esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL);
        return;
    }
    uart_send_line("MDP1 DONE REBOOTING");
    uart_wait_tx_done(TRANSFER_UART, pdMS_TO_TICKS(3000));
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void transfer_task(void *argument)
{
    (void)argument;
    char line[160];
    size_t used = 0;
    while (true) {
        uint8_t character;
        int count = uart_read_bytes(TRANSFER_UART, &character, 1, portMAX_DELAY);
        if (count != 1) continue;
        if (character == '\r') continue;
        if (character != '\n' && used + 1 < sizeof(line)) {
            line[used++] = (char)character;
            continue;
        }
        line[used] = '\0';
        used = 0;
        if (strcmp(line, "MDP1 HELLO") == 0) {
            uart_send_line("MDP1 HELLO 1");
        } else if (strcmp(line, "MDP1 LIST") == 0) {
            handle_list();
        } else if (strncmp(line, "MDP1 GET ", 9) == 0) {
            handle_get(line + 9);
        } else if (strncmp(line, "MDP1 PUTTHEME ", 14) == 0) {
            handle_put_theme(line + 14);
        }
    }
}

esp_err_t uart_file_transfer_start(void)
{
    if (!uart_is_driver_installed(TRANSFER_UART)) {
        uart_config_t configuration = {
            .baud_rate = TRANSFER_BAUD,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        ESP_RETURN_ON_ERROR(uart_param_config(TRANSFER_UART, &configuration), TAG, "UART config failed");
        ESP_RETURN_ON_ERROR(uart_driver_install(TRANSFER_UART, 8192, 8192, 0, NULL, 0),
                            TAG, "UART driver install failed");
    }
    if (xTaskCreate(transfer_task, "uart_files", 6144, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "UART log/theme transfer ready at %d baud", TRANSFER_BAUD);
    return ESP_OK;
}