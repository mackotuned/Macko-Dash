#include "ota_c6_hosted_bridge.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wifi_remote.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "ota_update.h"

#ifndef OTA_C6_URL_LEN
#define OTA_C6_URL_LEN 128
#endif

#define OTA_AP_SSID              "ESP-Hosted-OTA"
#define OTA_AP_PASS              ""
#define OTA_AP_CHANNEL           1
#define OTA_AP_MAX_CONNECTIONS   4
#define OTA_AP_ROOT_URL          "http://192.168.4.1/"
#define OTA_AP_FIRMWARE_URL      "http://192.168.4.1/firmware.bin"
#define OTA_UPLOAD_PATH          "/spiffs/network_adapter.bin"
#define OTA_BRIDGE_START_TIMEOUT_MS 15000

static const char *TAG = "ota_c6_bridge";
static char s_image_url[OTA_C6_URL_LEN] = OTA_AP_ROOT_URL;
static bool s_server_running = false;
static bool s_install_pending = false;
static esp_netif_t *s_ap_netif = NULL;
static bool s_ap_netif_started = false;
static httpd_handle_t s_httpd = NULL;
static TaskHandle_t s_bridge_task = NULL;
static volatile bool s_starting = false;
static volatile esp_err_t s_last_start_err = ESP_OK;
static volatile TickType_t s_start_tick = 0;
static const char *s_status_detail = "idle";

/* Hosted symbols may be stripped depending on esp_hosted build mode. */
extern esp_err_t esp_hosted_reinit(void) __attribute__((weak));
extern esp_err_t esp_hosted_slave_reset(void) __attribute__((weak));
extern esp_err_t esp_hosted_slave_ota(const char *image_url) __attribute__((weak));
/* esp_hosted_slave_reset/reinit were renamed in esp_hosted 2.x -- this is
   the actual function that triggers the SDIO handshake with the slave now.
   Declared weak too so this still degrades gracefully if a future/older
   esp_hosted version doesn't have it. */
extern int esp_hosted_connect_to_slave(void) __attribute__((weak));

esp_err_t __attribute__((weak)) esp_wifi_remote_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    uint8_t base_mac[6] = { 0 };
    esp_err_t err = esp_efuse_mac_get_default(base_mac);
    if (err != ESP_OK) {
        return err;
    }

    if (ifx == WIFI_IF_AP) {
        return esp_derive_local_mac(mac, base_mac);
    }

    memcpy(mac, base_mac, sizeof(base_mac));
    return ESP_OK;
}

static void set_bridge_url(const char *url)
{
    if (url && url[0] != '\0') {
        strncpy(s_image_url, url, sizeof(s_image_url) - 1);
        s_image_url[sizeof(s_image_url) - 1] = '\0';
    }
}

static const char *UPLOAD_PAGE =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:20px;}"
    "button,input{font-size:18px;margin-top:12px;} .card{max-width:520px;margin:auto;background:#1a1a1a;padding:20px;border-radius:16px;}"
    "a{color:#7df;} </style></head><body><div class='card'>"
    "<h2>ESP-Hosted OTA Upload</h2>"
    "<p>Choose the firmware file, then tap Upload.</p>"
    "<input id='file' type='file' accept='.bin,.BIN,application/octet-stream' />"
    "<br><button onclick='upload()'>Upload</button>"
    "<pre id='status'>Waiting for file...</pre>"
    "</div><script>async function upload(){const f=document.getElementById('file').files[0];const s=document.getElementById('status');"
    "if(!f){s.textContent='Select a .bin file first.';return;}s.textContent='Uploading ' + f.name + '...';"
    "const r=await fetch('/upload',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-Filename':f.name},body:f});"
    "s.textContent=await r.text();}</script></body></html>";
static esp_err_t start_http_server(void);
static esp_err_t start_bridge_services(void);

static inline void set_status_detail(const char *detail)
{
    s_status_detail = detail ? detail : "unknown";
}

static bool bridge_start_timed_out(void)
{
    if (!s_starting) {
        return false;
    }

    TickType_t start_tick = s_start_tick;
    if (start_tick == 0) {
        return false;
    }

    TickType_t elapsed = xTaskGetTickCount() - start_tick;
    return elapsed > pdMS_TO_TICKS(OTA_BRIDGE_START_TIMEOUT_MS);
}

static void bridge_abort_stuck_start(void)
{
    if (!s_starting || !bridge_start_timed_out()) {
        return;
    }

    ESP_LOGE(TAG, "OTA startup timed out after %d ms", OTA_BRIDGE_START_TIMEOUT_MS);

    if (s_bridge_task) {
        vTaskDelete(s_bridge_task);
        s_bridge_task = NULL;
    }

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    s_server_running = false;
    s_starting = false;
    s_start_tick = 0;
    s_last_start_err = ESP_ERR_TIMEOUT;
    set_status_detail("timeout");
}

static esp_err_t mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    return err;
}

/* This hosted/SDIO setup has been observed delivering WIFI_EVENT_AP_START
   twice for a single esp_wifi_start() call. esp_netif's own default AP
   handler (registered by esp_netif_create_default_wifi_ap()) isn't
   idempotent -- it calls esp_netif_action_start() on every delivery, and
   the second call crashes lwIP with "netif_add ... netif already added".
   We register our own handler instead of the default one, guarded by a
   flag, so only the first delivery actually adds the netif. */
static void wifi_ap_start_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    if (s_ap_netif_started) {
        ESP_LOGW(TAG, "Ignoring duplicate Wi-Fi AP Start event (known esp_hosted/SDIO quirk)");
        return;
    }
    s_ap_netif_started = true;
    esp_netif_action_start(s_ap_netif, base, id, event_data);
}

static esp_err_t start_wifi_ap(void)
{
    set_status_detail("wifi:nvs_flash_init");
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES && err != ESP_ERR_NVS_NEW_VERSION_FOUND && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    set_status_detail("wifi:netif_init");
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    set_status_detail("wifi:event_loop_create");
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (esp_hosted_connect_to_slave) {
        set_status_detail("hosted:connect_to_slave");
        err = (esp_err_t)esp_hosted_connect_to_slave();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Hosted connect_to_slave failed (%s)", esp_err_to_name(err));
        }
    } else if (esp_hosted_slave_reset) {
        set_status_detail("hosted:slave_reset");
        err = esp_hosted_slave_reset();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Hosted slave reset failed (%s)", esp_err_to_name(err));
            if (esp_hosted_reinit) {
                set_status_detail("hosted:reinit");
                err = esp_hosted_reinit();
                if (err != ESP_OK) {
                    return err;
                }
            }
        }
    } else {
        set_status_detail("hosted:reset_unavailable");
    }

    set_status_detail("wifi:netif_create_ap");
    if (!s_ap_netif) {
        esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_WIFI_AP();
        s_ap_netif = esp_netif_create_wifi(WIFI_IF_AP, &base_cfg);
        if (!s_ap_netif) {
            return ESP_FAIL;
        }
        esp_netif_attach_wifi_ap(s_ap_netif);
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_AP_START,
                                             &wifi_ap_start_handler, NULL, NULL);
    }

    set_status_detail("wifi:init");
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "heap before wifi init=%u", (unsigned)esp_get_free_heap_size());
    cfg.static_rx_buf_num = 2;
    cfg.dynamic_rx_buf_num = 4;
    cfg.static_tx_buf_num = 0;
    cfg.dynamic_tx_buf_num = 4;
    cfg.rx_mgmt_buf_num = 6;
    cfg.cache_tx_buf_num = 0;
    cfg.csi_enable = 0;
    cfg.ampdu_rx_enable = 0;
    cfg.ampdu_tx_enable = 0;
    cfg.amsdu_tx_enable = 0;
    cfg.nvs_enable = 0;
    cfg.nano_enable = 0;
    cfg.rx_ba_win = 0;
    cfg.beacon_max_len = 256;
    cfg.mgmt_sbuf_num = 6;
    cfg.feature_caps = 0;
    cfg.sta_disconnected_pm = false;
    cfg.espnow_max_encrypt_num = 0;
    cfg.tx_hetb_queue_num = 0;
    cfg.dump_hesigb_enable = false;
    err = esp_wifi_init(&cfg);
    ESP_LOGI(TAG, "wifi_init returned %s, heap after=%u", esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    uint8_t base_mac[6] = { 0 };
    uint8_t ap_mac[6] = { 0 };
    err = esp_efuse_mac_get_default(base_mac);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_derive_local_mac(ap_mac, base_mac);
    if (err != ESP_OK) {
        return err;
    }

    set_status_detail("wifi:set_mac_ap");
    err = esp_wifi_set_mac(WIFI_IF_AP, ap_mac);
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wifi_config_t ap_config = { 0 };
    strlcpy((char *)ap_config.ap.ssid, OTA_AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(OTA_AP_SSID);
    ap_config.ap.channel = OTA_AP_CHANNEL;
    ap_config.ap.max_connection = OTA_AP_MAX_CONNECTIONS;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.pmf_cfg.required = false;

    set_status_detail("wifi:set_mode_ap");
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    set_status_detail("wifi:set_config_ap");
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    set_status_detail("wifi:start");
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    set_status_detail("wifi:ap_ready");
    return ESP_OK;
}

static esp_err_t serve_file(httpd_req_t *req, const char *path, const char *content_type)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_ERR_NOT_FOUND;
    }

    httpd_resp_set_type(req, content_type);
    char buffer[1024];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, UPLOAD_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t firmware_get_handler(httpd_req_t *req)
{
    return serve_file(req, OTA_UPLOAD_PATH, "application/octet-stream");
}

static void install_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(1200));
    ESP_LOGI(TAG, "Starting C6 flash from %s", OTA_AP_FIRMWARE_URL);
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
    if (esp_hosted_slave_ota) {
        err = esp_hosted_slave_ota(OTA_AP_FIRMWARE_URL);
    } else {
        ESP_LOGE(TAG, "C6 OTA API unavailable in current esp_hosted build");
    }
    ESP_LOGI(TAG, "C6 flash result: %s", esp_err_to_name(err));

    s_install_pending = false;
    vTaskDelete(NULL);
}

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    FILE *file = fopen(OTA_UPLOAD_PATH, "wb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open SPIFFS file");
        return ESP_FAIL;
    }

    char buffer[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buffer, remaining > (int)sizeof(buffer) ? (int)sizeof(buffer) : remaining);
        if (received <= 0) {
            fclose(file);
            remove(OTA_UPLOAD_PATH);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }

        if (fwrite(buffer, 1, received, file) != (size_t)received) {
            fclose(file);
            remove(OTA_UPLOAD_PATH);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SPIFFS write failed");
            return ESP_FAIL;
        }

        remaining -= received;
    }

    fclose(file);

    if (!s_install_pending) {
        s_install_pending = true;
        xTaskCreate(install_task, "ota_install", 8192, NULL, 5, NULL);
    }

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "Upload complete. Flashing the C6 now. Keep the board powered.");
}

static esp_err_t start_http_server(void)
{
    set_status_detail("http:start");
    if (s_httpd) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = false;
    config.stack_size = 6144;
    config.max_open_sockets = 3;
    config.max_uri_handlers = 4;
    config.max_resp_headers = 4;
    config.backlog_conn = 2;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    config.keep_alive_enable = false;
    config.task_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t firmware_uri = {
        .uri = "/firmware.bin",
        .method = HTTP_GET,
        .handler = firmware_get_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t upload_uri = {
        .uri = "/upload",
        .method = HTTP_POST,
        .handler = upload_post_handler,
        .user_ctx = NULL,
    };

    httpd_register_uri_handler(s_httpd, &root_uri);
    httpd_register_uri_handler(s_httpd, &firmware_uri);
    httpd_register_uri_handler(s_httpd, &upload_uri);
    set_status_detail("http:ready");
    return ESP_OK;
}

static void bridge_task(void *arg)
{
    (void)arg;
    set_status_detail("bridge:starting");

    esp_err_t err = start_bridge_services();
    if (err != ESP_OK) {
        s_last_start_err = err;
        ESP_LOGE(TAG, "Failed to start OTA server: %s", esp_err_to_name(err));
        s_server_running = false;
    } else {
        s_last_start_err = ESP_OK;
        s_server_running = true;
        ESP_LOGI(TAG, "OTA AP/server ready at %s", OTA_AP_ROOT_URL);
        set_status_detail("bridge:ready");
    }

    s_starting = false;
    s_start_tick = 0;
    s_bridge_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t start_bridge_services(void)
{
    set_status_detail("spiffs:mount");
    esp_err_t err = mount_spiffs();
    if (err == ESP_OK) {
        err = start_wifi_ap();
    }
    if (err == ESP_OK) {
        err = start_http_server();
    }

    if (err != ESP_OK && s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    return err;
}

static esp_err_t bridge_start(char *ssid, size_t ssid_len, char *pass, size_t pass_len, char *url, size_t url_len)
{
    bridge_abort_stuck_start();

    if (s_server_running || s_bridge_task) {
        if (ssid && ssid_len > 0) {
            snprintf(ssid, ssid_len, "%s", OTA_AP_SSID);
        }
        if (pass && pass_len > 0) {
            snprintf(pass, pass_len, "%s", OTA_AP_PASS);
        }
        if (url && url_len > 0) {
            snprintf(url, url_len, "%s", OTA_AP_ROOT_URL);
        }
        set_bridge_url(OTA_AP_ROOT_URL);
        return ESP_OK;
    }

    if (ssid && ssid_len > 0) {
        snprintf(ssid, ssid_len, "%s", OTA_AP_SSID);
    }
    if (pass && pass_len > 0) {
        snprintf(pass, pass_len, "%s", OTA_AP_PASS);
    }
    if (url && url_len > 0) {
        snprintf(url, url_len, "%s", OTA_AP_ROOT_URL);
    }

    set_bridge_url(OTA_AP_ROOT_URL);

    s_starting = true;
    s_start_tick = xTaskGetTickCount();
    s_last_start_err = ESP_OK;
    set_status_detail("bridge:task_create");

    BaseType_t ok = xTaskCreate(bridge_task, "ota_bridge", 12288, NULL, 5, &s_bridge_task);
    if (ok != pdPASS) {
        s_starting = false;
        s_start_tick = 0;
        s_bridge_task = NULL;
        ESP_LOGE(TAG, "Failed to create OTA bridge task");
        s_last_start_err = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t bridge_stop(void)
{
    bridge_abort_stuck_start();
    set_status_detail("bridge:stopping");

    s_install_pending = false;

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    // Keep Wi-Fi/netif initialized to avoid unstable teardown paths while
    // ESP-Hosted transport is active; Start can reuse this initialized state.

    if (s_bridge_task) {
        vTaskDelete(s_bridge_task);
        s_bridge_task = NULL;
    }

    s_server_running = false;
    s_starting = false;
    s_start_tick = 0;
    s_last_start_err = ESP_OK;
    set_status_detail("idle");
    return ESP_OK;
}

static bool bridge_is_running(void)
{
    bridge_abort_stuck_start();
    return s_server_running;
}

static bool bridge_is_starting(void)
{
    bridge_abort_stuck_start();
    return s_starting;
}

static esp_err_t bridge_get_last_error(void)
{
    bridge_abort_stuck_start();
    return s_last_start_err;
}

static const char *bridge_get_status_detail(void)
{
    bridge_abort_stuck_start();
    return s_status_detail;
}

esp_err_t ota_c6_hosted_bridge_register(void)
{
    static const ota_update_bridge_ops_t ops = {
        .start = bridge_start,
        .stop = bridge_stop,
        .is_running = bridge_is_running,
        .is_starting = bridge_is_starting,
        .get_last_error = bridge_get_last_error,
        .get_status_detail = bridge_get_status_detail,
    };

    return ota_update_register_bridge(&ops);
}