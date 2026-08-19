#include "ota_update.h"

#include "esp_log.h"
#include <string.h>

#define OTA_DEFAULT_SSID "ESP-Hosted-OTA"
#define OTA_DEFAULT_PASS "n/a"
#define OTA_DEFAULT_URL  "http://192.168.4.1/"

static const char *TAG = "ota_update";

static bool s_running = false;
static char s_ssid[64] = OTA_DEFAULT_SSID;
static char s_password[64] = OTA_DEFAULT_PASS;
static char s_url[96] = OTA_DEFAULT_URL;
static const char *s_status_detail = "idle";
static esp_err_t s_last_error = ESP_OK;
static ota_update_bridge_ops_t s_bridge_ops = {0};

static void ota_update_sync_state_from_bridge(void)
{
    bool backend_running = s_bridge_ops.is_running ? s_bridge_ops.is_running() : false;
    bool backend_starting = s_bridge_ops.is_starting ? s_bridge_ops.is_starting() : false;

    if (!backend_running && !backend_starting) {
        s_running = false;
    }

    if (s_bridge_ops.get_last_error) {
        s_last_error = s_bridge_ops.get_last_error();
    }

    if (s_bridge_ops.get_status_detail) {
        const char *detail = s_bridge_ops.get_status_detail();
        s_status_detail = detail ? detail : "unknown";
    }
}

esp_err_t ota_update_register_bridge(const ota_update_bridge_ops_t *ops)
{
    if (!ops || !ops->start || !ops->stop) {
        return ESP_ERR_INVALID_ARG;
    }

    s_bridge_ops = *ops;
    return ESP_OK;
}

void ota_update_unregister_bridge(void)
{
    memset(&s_bridge_ops, 0, sizeof(s_bridge_ops));
}

esp_err_t ota_update_start_ap_server(void)
{
    ota_update_sync_state_from_bridge();

    if (s_running) {
        return ESP_OK;
    }

    if (!s_bridge_ops.start) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    char new_ssid[sizeof(s_ssid)] = OTA_DEFAULT_SSID;
    char new_pass[sizeof(s_password)] = OTA_DEFAULT_PASS;
    char new_url[sizeof(s_url)] = OTA_DEFAULT_URL;

    esp_err_t err = s_bridge_ops.start(new_ssid, sizeof(new_ssid), new_pass, sizeof(new_pass), new_url, sizeof(new_url));
    if (err != ESP_OK) {
        s_last_error = err;
        ESP_LOGW(TAG, "OTA bridge start failed: %s", esp_err_to_name(err));
        return err;
    }

    strncpy(s_ssid, new_ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_password, new_pass, sizeof(s_password) - 1);
    s_password[sizeof(s_password) - 1] = '\0';
    strncpy(s_url, new_url, sizeof(s_url) - 1);
    s_url[sizeof(s_url) - 1] = '\0';

    ota_update_sync_state_from_bridge();
    if (s_bridge_ops.is_starting && s_bridge_ops.is_starting()) {
        s_running = true;
    } else if (s_bridge_ops.is_running && s_bridge_ops.is_running()) {
        s_running = true;
    } else if (s_bridge_ops.get_last_error) {
        s_last_error = s_bridge_ops.get_last_error();
        return (s_last_error == ESP_OK) ? ESP_FAIL : s_last_error;
    } else {
        s_running = true;
    }

    ESP_LOGI(TAG, "OTA bridge started: SSID=%s URL=%s", s_ssid, s_url);
    return ESP_OK;
}

esp_err_t ota_update_stop_ap_server(void)
{
    ota_update_sync_state_from_bridge();

    if (!s_running) {
        return ESP_OK;
    }

    if (!s_bridge_ops.stop) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = s_bridge_ops.stop();
    if (err != ESP_OK) {
        s_last_error = err;
        ESP_LOGW(TAG, "OTA bridge stop failed: %s", esp_err_to_name(err));
        return err;
    }

    s_running = false;
    s_last_error = ESP_OK;
    return ESP_OK;
}

bool ota_update_is_running(void)
{
    ota_update_sync_state_from_bridge();
    return s_running;
}

bool ota_update_is_starting(void)
{
    ota_update_sync_state_from_bridge();
    return s_bridge_ops.is_starting ? s_bridge_ops.is_starting() : false;
}

esp_err_t ota_update_get_last_error(void)
{
    ota_update_sync_state_from_bridge();
    return s_last_error;
}

const char *ota_update_get_status_detail(void)
{
    ota_update_sync_state_from_bridge();
    return s_status_detail;
}

const char *ota_update_get_ssid(void)
{
    return s_ssid;
}

const char *ota_update_get_password(void)
{
    return s_password;
}

const char *ota_update_get_url(void)
{
    return s_url;
}
