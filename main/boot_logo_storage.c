#include "boot_logo_storage.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "theme_storage.h"
#include "zlib.h"

#define LOGO_DIRECTORY "/sdcard/MACKODASH/BOOT_LOGOS"
#define LOGO_SUFFIX ".mdlogo"
#define LOGO_HEADER_SIZE 80U
#define LOGO_WIDTH 1024U
#define LOGO_HEIGHT 600U
#define LOGO_DATA_SIZE (LOGO_WIDTH * LOGO_HEIGHT * 2U)
#define LOGO_NVS_NAMESPACE "honda_dash"
#define LOGO_NVS_KEY "boot_logo"

static const char *TAG = "boot_logo_storage";
static boot_logo_storage_entry_t s_entries[BOOT_LOGO_STORAGE_MAX_LOGOS];
static size_t s_entry_count;
static char s_selected_filename[96];

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool has_logo_suffix(const char *filename)
{
    size_t length = strlen(filename);
    size_t suffix_length = sizeof(LOGO_SUFFIX) - 1;
    if (length <= suffix_length) return false;
    const char *tail = filename + length - suffix_length;
    for (size_t index = 0; index < suffix_length; ++index) {
        if (tolower((unsigned char)tail[index]) != LOGO_SUFFIX[index]) return false;
    }
    return true;
}

static bool read_logo_header(const char *path, char display_name[BOOT_LOGO_STORAGE_NAME_MAX],
                             uint32_t *expected_crc)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint8_t header[LOGO_HEADER_SIZE];
    bool valid = fread(header, 1, sizeof(header), file) == sizeof(header);
    struct stat info;
    valid = valid && stat(path, &info) == 0 && info.st_size == LOGO_HEADER_SIZE + LOGO_DATA_SIZE;
    valid = valid && memcmp(header, "MDL1", 4) == 0 && read_le16(header + 4) == 1 &&
            read_le16(header + 6) == LOGO_WIDTH && read_le16(header + 8) == LOGO_HEIGHT &&
            read_le32(header + 12) == LOGO_DATA_SIZE;
    if (valid) {
        memcpy(display_name, header + 20, BOOT_LOGO_STORAGE_NAME_MAX - 1);
        display_name[BOOT_LOGO_STORAGE_NAME_MAX - 1] = '\0';
        valid = display_name[0] != '\0';
        if (expected_crc) *expected_crc = read_le32(header + 16);
    }
    fclose(file);
    return valid;
}

static int entry_compare(const void *left, const void *right)
{
    const boot_logo_storage_entry_t *a = left;
    const boot_logo_storage_entry_t *b = right;
    return strcmp(a->display_name, b->display_name);
}

static void load_selected_filename(void)
{
    s_selected_filename[0] = '\0';
    nvs_handle_t handle;
    if (nvs_open(LOGO_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    size_t length = sizeof(s_selected_filename);
    if (nvs_get_str(handle, LOGO_NVS_KEY, s_selected_filename, &length) != ESP_OK) {
        s_selected_filename[0] = '\0';
    }
    nvs_close(handle);
}

void boot_logo_storage_init(void)
{
    s_entry_count = 0;
    load_selected_filename();
    if (!theme_storage_is_available()) return;
    if (mkdir(LOGO_DIRECTORY, 0775) != 0 && errno != EEXIST) return;

    DIR *directory = opendir(LOGO_DIRECTORY);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && s_entry_count < BOOT_LOGO_STORAGE_MAX_LOGOS) {
        if (entry->d_name[0] == '.' || !has_logo_suffix(entry->d_name)) continue;
        char path[192];
        snprintf(path, sizeof(path), "%s/%s", LOGO_DIRECTORY, entry->d_name);
        boot_logo_storage_entry_t *logo = &s_entries[s_entry_count];
        memset(logo, 0, sizeof(*logo));
        if (!read_logo_header(path, logo->display_name, NULL)) continue;
        snprintf(logo->filename, sizeof(logo->filename), "%s", entry->d_name);
        ++s_entry_count;
    }
    closedir(directory);
    qsort(s_entries, s_entry_count, sizeof(s_entries[0]), entry_compare);
    ESP_LOGI(TAG, "Discovered %u custom boot logo(s)", (unsigned)s_entry_count);
}

size_t boot_logo_storage_get_count(void)
{
    return s_entry_count;
}

const boot_logo_storage_entry_t *boot_logo_storage_get_entry(size_t index)
{
    return index < s_entry_count ? &s_entries[index] : NULL;
}

int boot_logo_storage_get_selected_index(void)
{
    if (!s_selected_filename[0]) return -1;
    for (size_t index = 0; index < s_entry_count; ++index) {
        if (strcmp(s_entries[index].filename, s_selected_filename) == 0) return (int)index;
    }
    return -1;
}

esp_err_t boot_logo_storage_select(int index)
{
    if (index < -1 || index >= (int)s_entry_count) return ESP_ERR_INVALID_ARG;
    const char *filename = index < 0 ? "" : s_entries[index].filename;
    nvs_handle_t handle;
    esp_err_t result = nvs_open(LOGO_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_str(handle, LOGO_NVS_KEY, filename);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK) snprintf(s_selected_filename, sizeof(s_selected_filename), "%s", filename);
    return result;
}

esp_err_t boot_logo_storage_delete(size_t index)
{
    if (index >= s_entry_count) return ESP_ERR_INVALID_ARG;
    bool selected = strcmp(s_entries[index].filename, s_selected_filename) == 0;
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", LOGO_DIRECTORY, s_entries[index].filename);
    if (remove(path) != 0) return ESP_FAIL;
    if (selected) boot_logo_storage_select(-1);
    boot_logo_storage_init();
    return ESP_OK;
}

esp_err_t boot_logo_storage_load_selected(lv_img_dsc_t *image, uint8_t **pixels)
{
    if (!image || !pixels) return ESP_ERR_INVALID_ARG;
    *pixels = NULL;
    int selected = boot_logo_storage_get_selected_index();
    if (selected < 0) return ESP_ERR_NOT_FOUND;
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", LOGO_DIRECTORY, s_entries[selected].filename);
    uint32_t expected_crc = 0;
    char name[BOOT_LOGO_STORAGE_NAME_MAX];
    if (!read_logo_header(path, name, &expected_crc)) return ESP_ERR_INVALID_SIZE;

    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, LOGO_HEADER_SIZE, SEEK_SET) != 0) {
        if (file) fclose(file);
        return ESP_FAIL;
    }
    uint8_t *data = heap_caps_malloc(LOGO_DATA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    bool valid = fread(data, 1, LOGO_DATA_SIZE, file) == LOGO_DATA_SIZE;
    fclose(file);
    valid = valid && crc32(0, data, LOGO_DATA_SIZE) == expected_crc;
    if (!valid) {
        free(data);
        return ESP_ERR_INVALID_CRC;
    }

    memset(image, 0, sizeof(*image));
    image->header.cf = LV_IMG_CF_TRUE_COLOR;
    image->header.w = LOGO_WIDTH;
    image->header.h = LOGO_HEIGHT;
    image->data_size = LOGO_DATA_SIZE;
    image->data = data;
    *pixels = data;
    return ESP_OK;
}