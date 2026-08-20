#include "theme_storage.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_log.h"
#include "zlib.h"

#define THEME_STORAGE_ROOT BSP_SD_MOUNT_POINT "/MACKODASH"
#define THEME_STORAGE_DIR  THEME_STORAGE_ROOT "/THEMES"
#define THEME_PACKAGE_SUFFIX ".mdtheme.zip"
#define THEME_MANIFEST_NAME "manifest.json"
#define THEME_MANIFEST_MAX_SIZE 8192
#define THEME_MANIFEST_MAX_COMPRESSED_SIZE 32768
#define ZIP_EOCD_MAX_SEARCH (65535 + 22)

static const char *TAG = "theme_storage";
static theme_storage_package_t s_packages[THEME_STORAGE_MAX_PACKAGES];
static size_t s_package_count;
static bool s_storage_available;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool read_exact(FILE *file, void *data, size_t size)
{
    return fread(data, 1, size, file) == size;
}

static void set_package_status(theme_storage_package_t *package, const char *status)
{
    snprintf(package->status, sizeof(package->status), "%s", status);
}

static void set_package_defaults(theme_storage_package_t *package)
{
    size_t filename_len = strnlen(package->filename, sizeof(package->filename));
    size_t copy_len = filename_len < sizeof(package->display_name) - 1 ?
                      filename_len : sizeof(package->display_name) - 1;
    memcpy(package->display_name, package->filename, copy_len);
    package->display_name[copy_len] = '\0';
    size_t name_len = strlen(package->display_name);
    size_t suffix_len = strlen(THEME_PACKAGE_SUFFIX);
    if (name_len >= suffix_len) {
        package->display_name[name_len - suffix_len] = '\0';
    }
    for (char *cursor = package->display_name; *cursor; ++cursor) {
        if (*cursor == '_' || *cursor == '-') {
            *cursor = ' ';
        }
    }
    set_package_status(package, "Invalid manifest");
}

static bool valid_theme_id(const char *id)
{
    if (!id || !id[0] || strlen(id) >= THEME_STORAGE_ID_MAX) {
        return false;
    }
    for (const char *cursor = id; *cursor; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (!(islower(ch) || isdigit(ch) || ch == '.' || ch == '_' || ch == '-')) {
            return false;
        }
    }
    return true;
}

static bool valid_package_path(const char *path)
{
    if (!path || !path[0] || path[0] == '/' || path[0] == '\\' || strchr(path, '\\')) {
        return false;
    }
    const char *segment = path;
    while (*segment) {
        const char *end = strchr(segment, '/');
        size_t length = end ? (size_t)(end - segment) : strlen(segment);
        if (length == 0 || (length == 1 && segment[0] == '.') ||
            (length == 2 && segment[0] == '.' && segment[1] == '.')) {
            return false;
        }
        if (!end) break;
        segment = end + 1;
    }
    return true;
}

static bool parse_manifest(theme_storage_package_t *package, const char *json, size_t json_size)
{
    cJSON *root = cJSON_ParseWithLength(json, json_size);
    if (!root) {
        set_package_status(package, "Malformed manifest");
        return false;
    }

    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *resolution = cJSON_GetObjectItemCaseSensitive(root, "resolution");
    cJSON *lvgl = cJSON_GetObjectItemCaseSensitive(root, "lvgl");
    cJSON *layout = cJSON_GetObjectItemCaseSensitive(root, "layout");

    bool valid = cJSON_IsNumber(schema) && schema->valueint == 1 &&
                 cJSON_IsString(id) && valid_theme_id(id->valuestring) &&
                 cJSON_IsString(name) && name->valuestring[0] &&
                 strlen(name->valuestring) < THEME_STORAGE_NAME_MAX &&
                 cJSON_IsArray(resolution) && cJSON_GetArraySize(resolution) == 2 &&
                 cJSON_IsString(lvgl) && strcmp(lvgl->valuestring, "8.4") == 0 &&
                 cJSON_IsString(layout) && valid_package_path(layout->valuestring);

        cJSON *width = valid ? cJSON_GetArrayItem(resolution, 0) : NULL;
        cJSON *height = valid ? cJSON_GetArrayItem(resolution, 1) : NULL;
        valid = valid && cJSON_IsNumber(width) && width->valueint >= 160 && width->valueint <= 2048 &&
            cJSON_IsNumber(height) && height->valueint >= 120 && height->valueint <= 2048;

    if (valid) {
        package->schema = (uint16_t)schema->valueint;
        snprintf(package->id, sizeof(package->id), "%s", id->valuestring);
        snprintf(package->display_name, sizeof(package->display_name), "%s", name->valuestring);
        snprintf(package->layout_path, sizeof(package->layout_path), "%s", layout->valuestring);
        package->design_width = (uint16_t)width->valueint;
        package->design_height = (uint16_t)height->valueint;
        package->manifest_valid = true;
        set_package_status(package, "Ready");
    } else {
        set_package_status(package, "Unsupported manifest");
    }

    cJSON_Delete(root);
    return valid;
}

static bool inflate_entry(const uint8_t *compressed, size_t compressed_size,
                          uint8_t *output, size_t output_size, size_t expected_size)
{
    z_stream stream = {0};
    stream.next_in = (Bytef *)compressed;
    stream.avail_in = (uInt)compressed_size;
    stream.next_out = (Bytef *)output;
    stream.avail_out = (uInt)output_size;

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }
    int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return result == Z_STREAM_END && stream.total_out == expected_size;
}

esp_err_t theme_storage_read_file(const theme_storage_package_t *package, const char *entry_path,
                                  size_t max_size, uint8_t **data, size_t *data_size)
{
    if (!package || !entry_path || !valid_package_path(entry_path) || !data || !data_size || max_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    *data = NULL;
    *data_size = 0;
    FILE *file = fopen(package->path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }

    bool success = false;
    uint8_t *tail = NULL;
    uint8_t *compressed = NULL;
    uint8_t *output = NULL;
    if (fseek(file, 0, SEEK_END) != 0) goto cleanup;
    long file_size_long = ftell(file);
    if (file_size_long < 22) goto cleanup;
    size_t file_size = (size_t)file_size_long;
    size_t tail_size = file_size < ZIP_EOCD_MAX_SEARCH ? file_size : ZIP_EOCD_MAX_SEARCH;
    tail = malloc(tail_size);
    if (!tail || fseek(file, (long)(file_size - tail_size), SEEK_SET) != 0 ||
        !read_exact(file, tail, tail_size)) goto cleanup;

    const uint8_t *eocd = NULL;
    for (size_t offset = tail_size - 22 + 1; offset-- > 0;) {
        if (read_le32(tail + offset) == 0x06054b50) {
            eocd = tail + offset;
            break;
        }
    }
    if (!eocd || read_le16(eocd + 4) != 0 || read_le16(eocd + 6) != 0) goto cleanup;

    uint16_t entry_count = read_le16(eocd + 10);
    uint32_t central_size = read_le32(eocd + 12);
    uint32_t central_offset = read_le32(eocd + 16);
    if (entry_count == UINT16_MAX || central_offset > file_size ||
        central_size > file_size - central_offset || fseek(file, central_offset, SEEK_SET) != 0) goto cleanup;

    uint16_t method = 0;
    uint32_t expected_crc = 0;
    uint32_t compressed_size = 0;
    uint32_t manifest_size = 0;
    uint32_t local_offset = 0;
    bool found = false;
    for (uint16_t index = 0; index < entry_count; ++index) {
        uint8_t header[46];
        if (!read_exact(file, header, sizeof(header)) || read_le32(header) != 0x02014b50) goto cleanup;
        uint16_t flags = read_le16(header + 8);
        uint16_t filename_size = read_le16(header + 28);
        uint16_t extra_size = read_le16(header + 30);
        uint16_t comment_size = read_le16(header + 32);
        char filename[THEME_STORAGE_PATH_MAX];
        bool possible_match = filename_size == strlen(entry_path) && filename_size < sizeof(filename);
        if (possible_match) {
            if (!read_exact(file, filename, filename_size)) goto cleanup;
            filename[filename_size] = '\0';
        } else if (fseek(file, filename_size, SEEK_CUR) != 0) {
            goto cleanup;
        }

        if (possible_match && strcmp(filename, entry_path) == 0) {
            method = read_le16(header + 10);
            expected_crc = read_le32(header + 16);
            compressed_size = read_le32(header + 20);
            manifest_size = read_le32(header + 24);
            local_offset = read_le32(header + 42);
            found = (flags & 1) == 0 && (method == 0 || method == 8) &&
                    manifest_size > 0 && manifest_size <= max_size &&
                    compressed_size <= max_size * 4;
        }
        if (fseek(file, (long)extra_size + comment_size, SEEK_CUR) != 0) goto cleanup;
        if (found) break;
    }
    if (!found || local_offset > file_size || fseek(file, local_offset, SEEK_SET) != 0) goto cleanup;

    uint8_t local_header[30];
    if (!read_exact(file, local_header, sizeof(local_header)) || read_le32(local_header) != 0x04034b50) goto cleanup;
    uint16_t local_name_size = read_le16(local_header + 26);
    uint16_t local_extra_size = read_le16(local_header + 28);
    size_t data_offset = (size_t)local_offset + sizeof(local_header) + local_name_size + local_extra_size;
    if (data_offset > file_size || compressed_size > file_size - data_offset ||
        fseek(file, (long)data_offset, SEEK_SET) != 0) goto cleanup;

    compressed = malloc(compressed_size ? compressed_size : 1);
    output = malloc((size_t)manifest_size + 1);
    if (!compressed || !output || !read_exact(file, compressed, compressed_size)) goto cleanup;

    if (method == 0) {
        if (compressed_size != manifest_size) goto cleanup;
        memcpy(output, compressed, manifest_size);
    } else if (!inflate_entry(compressed, compressed_size, output,
                              manifest_size, manifest_size)) {
        goto cleanup;
    }
    output[manifest_size] = '\0';
    if (crc32(0L, output, manifest_size) != expected_crc) goto cleanup;
    *data = output;
    *data_size = manifest_size;
    output = NULL;
    success = true;

cleanup:
    free(output);
    free(compressed);
    free(tail);
    fclose(file);
    return success ? ESP_OK : ESP_FAIL;
}

static bool read_manifest_from_zip(theme_storage_package_t *package)
{
    uint8_t *manifest = NULL;
    size_t manifest_size = 0;
    esp_err_t err = theme_storage_read_file(package, THEME_MANIFEST_NAME,
                                            THEME_MANIFEST_MAX_SIZE, &manifest, &manifest_size);
    if (err != ESP_OK) {
        set_package_status(package, "Missing manifest");
        return false;
    }
    bool success = parse_manifest(package, (const char *)manifest, manifest_size);
    free(manifest);
    return success;
}

static bool has_theme_suffix(const char *filename)
{
    size_t filename_len = strlen(filename);
    size_t suffix_len = strlen(THEME_PACKAGE_SUFFIX);
    if (filename_len < suffix_len) {
        return false;
    }

    const char *tail = filename + filename_len - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower((unsigned char)tail[i]) != tolower((unsigned char)THEME_PACKAGE_SUFFIX[i])) {
            return false;
        }
    }
    return true;
}

static int package_compare(const void *left, const void *right)
{
    const theme_storage_package_t *a = left;
    const theme_storage_package_t *b = right;
    return strcmp(a->filename, b->filename);
}

static esp_err_t ensure_theme_directories(void)
{
    if (mkdir(THEME_STORAGE_ROOT, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Cannot create %s: errno=%d", THEME_STORAGE_ROOT, errno);
        return ESP_FAIL;
    }
    if (mkdir(THEME_STORAGE_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Cannot create %s: errno=%d", THEME_STORAGE_DIR, errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t theme_storage_init(void)
{
    s_package_count = 0;
    s_storage_available = false;

    esp_err_t err = bsp_sdcard_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No usable SD card; built-in themes remain available: %s", esp_err_to_name(err));
        return err;
    }

    s_storage_available = true;
    ESP_LOGI(TAG, "SD card mounted at %s", BSP_SD_MOUNT_POINT);
    ensure_theme_directories();

    DIR *directory = opendir(THEME_STORAGE_DIR);
    if (!directory) {
        ESP_LOGW(TAG, "Cannot scan %s: errno=%d", THEME_STORAGE_DIR, errno);
        return ESP_FAIL;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.' || !has_theme_suffix(entry->d_name)) {
            continue;
        }
        if (s_package_count >= THEME_STORAGE_MAX_PACKAGES) {
            ESP_LOGW(TAG, "Theme limit reached; ignoring %s", entry->d_name);
            continue;
        }

        theme_storage_package_t *package = &s_packages[s_package_count++];
        memset(package, 0, sizeof(*package));
        snprintf(package->filename, sizeof(package->filename), "%s", entry->d_name);
        snprintf(package->path, sizeof(package->path), "%s/%s", THEME_STORAGE_DIR, entry->d_name);
        set_package_defaults(package);
        read_manifest_from_zip(package);
    }
    closedir(directory);

    qsort(s_packages, s_package_count, sizeof(s_packages[0]), package_compare);
    for (size_t i = 0; i < s_package_count; ++i) {
        if (!s_packages[i].manifest_valid) continue;
        for (size_t j = i + 1; j < s_package_count; ++j) {
            if (s_packages[j].manifest_valid && strcmp(s_packages[i].id, s_packages[j].id) == 0) {
                s_packages[i].manifest_valid = false;
                s_packages[j].manifest_valid = false;
                set_package_status(&s_packages[i], "Duplicate theme ID");
                set_package_status(&s_packages[j], "Duplicate theme ID");
            }
        }
    }
    ESP_LOGI(TAG, "Discovered %u theme package(s) in %s", (unsigned)s_package_count, THEME_STORAGE_DIR);
    for (size_t i = 0; i < s_package_count; ++i) {
        ESP_LOGI(TAG, "Theme package: %s (%s)", s_packages[i].display_name, s_packages[i].status);
    }
    return ESP_OK;
}

bool theme_storage_is_available(void)
{
    return s_storage_available;
}

size_t theme_storage_get_count(void)
{
    return s_package_count;
}

const theme_storage_package_t *theme_storage_get_package(size_t index)
{
    return index < s_package_count ? &s_packages[index] : NULL;
}

esp_err_t theme_storage_delete(size_t index)
{
    if (index >= s_package_count) return ESP_ERR_INVALID_ARG;

    char path[THEME_STORAGE_PATH_MAX];
    snprintf(path, sizeof(path), "%s", s_packages[index].path);
    if (remove(path) != 0) {
        ESP_LOGE(TAG, "Cannot delete %s: errno=%d", path, errno);
        return ESP_FAIL;
    }

    if (index + 1 < s_package_count) {
        memmove(&s_packages[index], &s_packages[index + 1],
                (s_package_count - index - 1) * sizeof(s_packages[0]));
    }
    --s_package_count;
    memset(&s_packages[s_package_count], 0, sizeof(s_packages[0]));
    ESP_LOGI(TAG, "Deleted theme package %s", path);
    return ESP_OK;
}