/*
 * Wi-Fi + firmware update over the ESP32-C6 co-processor (esp_hosted).
 * See wifi_update.h. One worker task owns the radio and all network I/O;
 * Wi-Fi events are forwarded to it through the same queue, so no radio
 * call ever runs on the UI or event-loop task.
 */
#include "wifi_update.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "rotary_controller_uart.h"

#define RELEASE_API_URL "https://api.github.com/repos/mackotuned/MackoDash-Flash-Tools/releases/latest"
#define FIRMWARE_ASSET  "mackodash.bin"
#define NVS_NS          "wifi_upd"
#define API_BODY_MAX    (128 * 1024)
#define KNOB_RELEASE_API_URL "https://api.github.com/repos/mackotuned/MackoDash-S3-Controller/releases/latest"
#define KNOB_FIRMWARE_ASSET  "mackodash_rotary.bin"
#define KNOB_PROJECT         "mackodash_rotary"
#define KNOB_IMAGE_MAX       (3 * 1024 * 1024)   /* the knob's app slot */
#define KNOB_HTTP_PORT       8080

static const char *TAG = "wifi_update";

enum {
    CMD_RADIO_ON,
    CMD_RADIO_OFF,
    CMD_SCAN,
    CMD_SCAN_DONE,
    CMD_CONNECT,
    CMD_DISCONNECTED,
    CMD_GOT_IP,
    CMD_FORGET,
    CMD_CHECK,
    CMD_INSTALL,
    CMD_KNOB_ON,
    CMD_KNOB_OFF,
    CMD_KNOB_CHECK,
    CMD_KNOB_INSTALL,
};

typedef struct {
    uint8_t cmd;
    int32_t arg;
} wu_msg_t;

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_lock;
static wu_status_t s_status;
static wu_ap_t s_aps[WU_MAX_APS];
static int s_ap_count;

/* worker-task-only state */
static bool s_wifi_ready;
static bool s_radio_on;          /* home Wi-Fi (station) wanted */
static bool s_knob_ap;           /* knob network (access point) wanted */
static bool s_started;           /* esp_wifi_start() done */
static esp_netif_t *s_netif;
static esp_netif_t *s_ap_netif;
static int64_t s_knob_poll_us;
static int s_connect_tries;
static int s_ignore_disconnects;
static bool s_save_on_connect;
static int s_scan_retries;
static TickType_t s_scan_retry_at;
static char s_conn_ssid[33];
static char s_conn_pass[65];
static char *s_bin_url;
static bool s_hosted_allowed;
static int s_bin_size;

/* guarded by s_lock */
static char s_saved_ssid[33];
static char s_saved_pass[65];
static char s_req_ssid[33];
static char s_req_pass[65];

/* ------------------------------------------------------------------ */

static void lock(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();   /* first caller is the UI task */
    xSemaphoreTake(s_lock, portMAX_DELAY);
}
static void unlock(void) { xSemaphoreGive(s_lock); }

static void set_wifi(wu_wifi_state_t state, const char *ssid, const char *fmt, ...)
{
    lock();
    s_status.wifi = state;
    if (ssid) snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", ssid);
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s_status.wifi_detail, sizeof(s_status.wifi_detail), fmt, ap);
        va_end(ap);
    } else {
        s_status.wifi_detail[0] = '\0';
    }
    unlock();
}

static void set_upd(wu_upd_state_t state, const char *fmt, ...)
{
    lock();
    s_status.upd = state;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s_status.upd_detail, sizeof(s_status.upd_detail), fmt, ap);
        va_end(ap);
    } else {
        s_status.upd_detail[0] = '\0';
    }
    unlock();
}

static wu_wifi_state_t wifi_state(void)
{
    lock();
    wu_wifi_state_t state = s_status.wifi;
    unlock();
    return state;
}

static wu_upd_state_t upd_state(void)
{
    lock();
    wu_upd_state_t state = s_status.upd;
    unlock();
    return state;
}

static void post(uint8_t cmd, int32_t arg)
{
    if (!s_queue) return;
    wu_msg_t msg = {.cmd = cmd, .arg = arg};
    xQueueSend(s_queue, &msg, 0);
}

/* ------------------------- flash-write helper ------------------------ */

/* The worker's stack lives in PSRAM to spare internal RAM, but code that
   writes flash must not run on a PSRAM stack (the cache is off while the
   flash is busy). Flash work runs on a short-lived task with a small
   internal-RAM stack instead; the caller waits for it to finish. */
typedef struct {
    esp_err_t (*fn)(void *arg);
    void *arg;
    esp_err_t result;
    SemaphoreHandle_t done;
} flash_job_t;

static void flash_job_task(void *param)
{
    flash_job_t *job = (flash_job_t *)param;
    job->result = job->fn(job->arg);
    xSemaphoreGive(job->done);
    vTaskDeleteWithCaps(NULL);
}

static esp_err_t run_flash_job(esp_err_t (*fn)(void *arg), void *arg)
{
    flash_job_t job = {.fn = fn, .arg = arg, .result = ESP_FAIL, .done = xSemaphoreCreateBinary()};
    if (!job.done) return ESP_ERR_NO_MEM;
    if (xTaskCreateWithCaps(flash_job_task, "wu_flash", 4096, &job, 4, NULL,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(TAG, "no internal RAM for the flash helper (largest block %u)",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        vSemaphoreDelete(job.done);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return job.result;
}

/* ---------------------------- credentials --------------------------- */

typedef struct {
    const char *ssid;
    const char *pass;
} creds_t;

static esp_err_t creds_write_job(void *arg)
{
    const creds_t *c = (const creds_t *)arg;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    if (c) {
        nvs_set_str(nvs, "ssid", c->ssid);
        nvs_set_str(nvs, "pass", c->pass);
    } else {
        nvs_erase_all(nvs);
    }
    err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static void creds_load(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) return;
    size_t len = sizeof(s_saved_ssid);
    if (nvs_get_str(nvs, "ssid", s_saved_ssid, &len) != ESP_OK) s_saved_ssid[0] = '\0';
    len = sizeof(s_saved_pass);
    if (nvs_get_str(nvs, "pass", s_saved_pass, &len) != ESP_OK) s_saved_pass[0] = '\0';
    nvs_close(nvs);
}

static void creds_save(const char *ssid, const char *pass)
{
    creds_t c = {.ssid = ssid, .pass = pass};
    if (run_flash_job(creds_write_job, &c) != ESP_OK) ESP_LOGW(TAG, "could not save the Wi-Fi network");
    lock();
    snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", ssid);
    snprintf(s_saved_pass, sizeof(s_saved_pass), "%s", pass);
    unlock();
}

static void creds_erase(void)
{
    run_flash_job(creds_write_job, NULL);
    lock();
    s_saved_ssid[0] = s_saved_pass[0] = '\0';
    unlock();
}

/* ------------------------------ radio ------------------------------- */

/* esp_hosted calls esp_hosted_init() from a startup constructor, before the
   display is up. The linker routes that call here (see main/CMakeLists.txt)
   and it is skipped; the link to the C6 starts the first time Wi-Fi is used. */
int __real_esp_hosted_init(void);
int __wrap_esp_hosted_init(void)
{
    if (!s_hosted_allowed) return ESP_OK;
    return __real_esp_hosted_init();
}


static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        post(CMD_SCAN_DONE, 0);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        post(CMD_DISCONNECTED, ((wifi_event_sta_disconnected_t *)data)->reason);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ip = (ip_event_got_ip_t *)data;
        post(CMD_GOT_IP, (int32_t)ip->ip_info.ip.addr);
    }
}

static bool wifi_ready(void)
{
    if (s_wifi_ready) return true;
    s_hosted_allowed = true;
    esp_err_t err = __wrap_esp_hosted_init();
    if (err != ESP_OK) goto fail;
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    if (!s_netif) s_netif = esp_netif_create_default_wifi_sta();
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto fail;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    s_wifi_ready = true;
    ESP_LOGI(TAG, "Wi-Fi ready");
    return true;
fail:
    ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
    set_wifi(WU_WIFI_ERROR, NULL, "Wi-Fi chip did not start (%s)", esp_err_to_name(err));
    return false;
}

static void start_connect(const char *ssid, const char *pass, bool save)
{
    wu_wifi_state_t state = wifi_state();
    if (state == WU_WIFI_CONNECTED || state == WU_WIFI_CONNECTING) {
        s_ignore_disconnects++;
        esp_wifi_disconnect();
    }
    snprintf(s_conn_ssid, sizeof(s_conn_ssid), "%s", ssid);
    snprintf(s_conn_pass, sizeof(s_conn_pass), "%s", pass ? pass : "");
    s_save_on_connect = save;
    s_connect_tries = 0;

    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, s_conn_ssid, strlen(s_conn_ssid));
    memcpy(wc.sta.password, s_conn_pass, strlen(s_conn_pass));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    set_wifi(WU_WIFI_CONNECTING, s_conn_ssid, "Connecting...");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) set_wifi(WU_WIFI_FAILED, s_conn_ssid, "Could not connect (%s)", esp_err_to_name(err));
}

/* Station (home Wi-Fi) and access point (knob network) can run together. */
static esp_err_t apply_mode(void)
{
    wifi_mode_t mode = s_radio_on ? (s_knob_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA)
                                  : (s_knob_ap ? WIFI_MODE_AP : WIFI_MODE_NULL);
    if (mode == WIFI_MODE_NULL) {
        if (s_started) esp_wifi_stop();
        s_started = false;
        return ESP_OK;
    }
    esp_err_t err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) return err;
    if (s_knob_ap) {
        wifi_config_t ap = {0};
        lock();
        size_t ssid_len = strnlen(s_status.knob_ssid, sizeof(ap.ap.ssid));
        memcpy(ap.ap.ssid, s_status.knob_ssid, ssid_len);
        unlock();
        ap.ap.ssid_len = (uint8_t)ssid_len;
        snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "%s", WU_KNOB_PASSWORD);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
        ap.ap.channel = 1;            /* follows the home network's channel when connected */
        ap.ap.max_connection = 2;
        ap.ap.pmf_cfg.capable = true;
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
        if (err != ESP_OK) return err;
    }
    if (!s_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) return err;
        s_started = true;
    }
    return ESP_OK;
}

static void knob_on(void)
{
    if (s_knob_ap) return;
    if (!wifi_ready()) return;
    s_knob_ap = true;
    esp_err_t err = apply_mode();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "knob network did not start: %s", esp_err_to_name(err));
        s_knob_ap = false;
        return;
    }
    err = rotary_controller_net_start();
    lock();
    s_status.knob_ap = true;
    unlock();
    ESP_LOGI(TAG, "knob network up (%s)", esp_err_to_name(err));
}

static void knob_off(void)
{
    if (!s_knob_ap) return;
    s_knob_ap = false;
    apply_mode();
    lock();
    s_status.knob_ap = false;
    s_status.knob_rssi = 0;
    unlock();
}

static void knob_poll(void)
{
    if (!s_knob_ap) return;
    int64_t now = esp_timer_get_time();
    if (now - s_knob_poll_us < 2000000) return;
    s_knob_poll_us = now;
    wifi_sta_list_t list = {0};
    int rssi = 0;
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num > 0) rssi = list.sta[0].rssi;
    lock();
    s_status.knob_rssi = rssi;
    unlock();
}

static void radio_on(void)
{
    if (s_radio_on) return;
    ESP_LOGI(TAG, "radio on");
    set_wifi(WU_WIFI_STARTING, "", "Starting Wi-Fi...");
    if (!wifi_ready()) return;
    s_radio_on = true;
    esp_err_t err = apply_mode();
    if (err != ESP_OK) {
        s_radio_on = false;
        set_wifi(WU_WIFI_ERROR, "", "Wi-Fi did not start (%s)", esp_err_to_name(err));
        return;
    }
    char ssid[33], pass[65];
    lock();
    snprintf(ssid, sizeof(ssid), "%s", s_saved_ssid);
    snprintf(pass, sizeof(pass), "%s", s_saved_pass);
    unlock();
    if (ssid[0]) start_connect(ssid, pass, false);
    else set_wifi(WU_WIFI_IDLE, "", "Not connected");
}

static void radio_off(void)
{
    if (!s_radio_on) return;
    if (upd_state() == WU_UPD_DOWNLOADING || s_status.knob_upd == WU_UPD_DOWNLOADING) return;
    wu_wifi_state_t state = wifi_state();
    if (state == WU_WIFI_CONNECTED || state == WU_WIFI_CONNECTING) s_ignore_disconnects++;
    esp_wifi_disconnect();
    s_radio_on = false;
    apply_mode();               /* keeps the knob network running if it is on */
    s_scan_retry_at = 0;
    lock();
    s_status.scanning = false;
    unlock();
    set_wifi(WU_WIFI_OFF, "", NULL);
}

static void scan_start(void)
{
    if (!s_radio_on) return;
    wifi_scan_config_t sc = {.show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err == ESP_OK) {
        s_scan_retry_at = 0;
        lock();
        s_status.scanning = true;
        unlock();
    } else if (s_scan_retries++ < 6) {
        /* the radio refuses to scan while it is busy joining; try again shortly */
        s_scan_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(1500);
        lock();
        s_status.scanning = true;
        unlock();
    } else {
        s_scan_retry_at = 0;
        lock();
        s_status.scanning = false;
        unlock();
    }
}

static int ap_cmp(const void *a, const void *b)
{
    return ((const wu_ap_t *)b)->rssi - ((const wu_ap_t *)a)->rssi;
}

static void scan_done(void)
{
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > 40) count = 40;
    wifi_ap_record_t *recs = heap_caps_calloc(count ? count : 1, sizeof(*recs), MALLOC_CAP_SPIRAM);
    wu_ap_t found[WU_MAX_APS];
    int n = 0;
    if (recs && esp_wifi_scan_get_ap_records(&count, recs) == ESP_OK) {
        for (int i = 0; i < count; ++i) {
            const char *ssid = (const char *)recs[i].ssid;
            if (!ssid[0]) continue;
            int dup = -1;
            for (int k = 0; k < n; ++k) {
                if (strcmp(found[k].ssid, ssid) == 0) dup = k;
            }
            if (dup >= 0) {
                if (recs[i].rssi > found[dup].rssi) found[dup].rssi = recs[i].rssi;
                continue;
            }
            if (n >= WU_MAX_APS) continue;
            snprintf(found[n].ssid, sizeof(found[n].ssid), "%s", ssid);
            found[n].rssi = recs[i].rssi;
            found[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
            n++;
        }
    } else {
        esp_wifi_clear_ap_list();
    }
    free(recs);
    qsort(found, n, sizeof(found[0]), ap_cmp);
    lock();
    memcpy(s_aps, found, sizeof(found[0]) * n);
    s_ap_count = n;
    s_status.scanning = false;
    s_status.scan_gen++;
    unlock();
}

static void handle_disconnect(int reason)
{
    if (s_ignore_disconnects > 0) {
        s_ignore_disconnects--;
        return;
    }
    if (!s_radio_on) return;
    wu_wifi_state_t state = wifi_state();
    if (state != WU_WIFI_CONNECTING && state != WU_WIFI_CONNECTED) return;

    bool auth = reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_MIC_FAILURE ||
                reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_CONNECTION_FAIL;
    int limit = state == WU_WIFI_CONNECTED ? 6 : (auth ? 2 : 3);
    if (++s_connect_tries > limit) {
        if (auth) set_wifi(WU_WIFI_FAILED, s_conn_ssid, "Wrong password");
        else if (reason == WIFI_REASON_NO_AP_FOUND) set_wifi(WU_WIFI_FAILED, s_conn_ssid, "Network not found");
        else set_wifi(WU_WIFI_FAILED, s_conn_ssid, "Could not connect (code %d)", reason);
        return;
    }
    set_wifi(WU_WIFI_CONNECTING, s_conn_ssid, "Connecting...");
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_wifi_connect();
}

static void handle_got_ip(uint32_t addr)
{
    esp_ip4_addr_t ip = {.addr = addr};
    s_connect_tries = 0;
    set_wifi(WU_WIFI_CONNECTED, s_conn_ssid, IPSTR, IP2STR(&ip));
    if (s_save_on_connect) {
        creds_save(s_conn_ssid, s_conn_pass);
        s_save_on_connect = false;
    }
}

/* ---------------------------- releases ------------------------------ */

typedef struct {
    char *data;
    int len;
} body_t;

static esp_err_t collect_body(esp_http_client_event_t *e)
{
    body_t *body = (body_t *)e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && body && body->data) {
        int room = API_BODY_MAX - 1 - body->len;
        int n = e->data_len < room ? e->data_len : room;
        if (n > 0) {
            memcpy(body->data + body->len, e->data, n);
            body->len += n;
        }
    }
    return ESP_OK;
}

static void parse_version(const char *text, int out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (!text) return;
    while (*text && (*text < '0' || *text > '9')) text++;
    sscanf(text, "%d.%d.%d", &out[0], &out[1], &out[2]);
}

static int compare_versions(const char *a, const char *b)
{
    int va[3], vb[3];
    parse_version(a, va);
    parse_version(b, vb);
    for (int i = 0; i < 3; ++i) {
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    }
    return 0;
}

typedef struct {
    char latest[24];
    char *url;          /* download URL of the wanted asset, NULL if missing */
    int size;
} release_t;

/* Reads a GitHub repository's latest release. NULL on success, else a message. */
static const char *fetch_release(const char *api_url, const char *asset_name, release_t *out)
{
    static char message[96];
    memset(out, 0, sizeof(*out));
    body_t body = {.data = heap_caps_malloc(API_BODY_MAX, MALLOC_CAP_SPIRAM), .len = 0};
    if (!body.data) return "Out of memory";
    esp_http_client_config_t hc = {
        .url = api_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = collect_body,
        .user_data = &body,
        .user_agent = "MackoDash",
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    body.data[body.len] = '\0';

    if (err != ESP_OK || status != 200) {
        free(body.data);
        ESP_LOGW(TAG, "release check failed: %s, HTTP %d", esp_err_to_name(err), status);
        if (err != ESP_OK) snprintf(message, sizeof(message), "Could not reach GitHub (%s)", esp_err_to_name(err));
        else if (status == 403 || status == 429) snprintf(message, sizeof(message), "GitHub is busy, try again in a few minutes");
        else snprintf(message, sizeof(message), "GitHub answered %d", status);
        return message;
    }

    cJSON *root = cJSON_Parse(body.data);
    free(body.data);
    const cJSON *tag = root ? cJSON_GetObjectItem(root, "tag_name") : NULL;
    if (!cJSON_IsString(tag)) {
        cJSON_Delete(root);
        return "Could not read the release information";
    }
    snprintf(out->latest, sizeof(out->latest), "%s", tag->valuestring[0] == 'v' ? tag->valuestring + 1 : tag->valuestring);
    const cJSON *asset;
    cJSON_ArrayForEach(asset, cJSON_GetObjectItem(root, "assets")) {
        const cJSON *name = cJSON_GetObjectItem(asset, "name");
        const cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
        const cJSON *size = cJSON_GetObjectItem(asset, "size");
        if (cJSON_IsString(name) && strcmp(name->valuestring, asset_name) == 0 && cJSON_IsString(url)) {
            out->url = strdup(url->valuestring);
            out->size = cJSON_IsNumber(size) ? size->valueint : 0;
        }
    }
    cJSON_Delete(root);
    return NULL;
}

static void check_for_update(void)
{
    if (wifi_state() != WU_WIFI_CONNECTED) {
        set_upd(WU_UPD_ERROR, "Connect to Wi-Fi first");
        return;
    }
    set_upd(WU_UPD_CHECKING, "Checking for updates...");
    release_t rel;
    const char *problem = fetch_release(RELEASE_API_URL, FIRMWARE_ASSET, &rel);
    if (problem) {
        set_upd(WU_UPD_ERROR, "%s", problem);
        return;
    }
    free(s_bin_url);
    s_bin_url = rel.url;
    s_bin_size = rel.size;
    lock();
    snprintf(s_status.latest, sizeof(s_status.latest), "%s", rel.latest);
    unlock();

    const esp_app_desc_t *app = esp_app_get_description();
    if (compare_versions(rel.latest, app->version) <= 0) {
        set_upd(WU_UPD_CURRENT, "You have the latest version");
    } else if (!s_bin_url) {
        set_upd(WU_UPD_USB_ONLY, "Version %s must be installed with the MackoDash USB updater", rel.latest);
    } else {
        set_upd(WU_UPD_AVAILABLE, "Version %s is ready to install", rel.latest);
    }
}

/* Image layout: 24-byte image header, 8-byte first segment header, then the
   app description (esp_app_desc_t) at the start of the first segment. */
#define IMAGE_MAGIC        0xE9
#define APP_DESC_OFFSET    32

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    size_t expected;
    int *progress;      /* status field to update, scaled to 0..span */
    int span;
} image_buf_t;

static esp_err_t collect_image(esp_http_client_event_t *e)
{
    image_buf_t *img = (image_buf_t *)e->user_data;
    if (e->event_id != HTTP_EVENT_ON_DATA || !img || !img->data) return ESP_OK;
    /* the redirect response's own body arrives too; keep only the 200 body */
    if (esp_http_client_get_status_code(e->client) != 200) return ESP_OK;
    if (img->len + e->data_len > img->cap) return ESP_FAIL;
    memcpy(img->data + img->len, e->data, e->data_len);
    img->len += e->data_len;
    if (img->expected && img->progress) {
        lock();
        *img->progress = (int)((int64_t)img->len * img->span / img->expected);
        unlock();
    }
    return ESP_OK;
}

/* Downloads a firmware image into PSRAM and checks it is an app image of the
   given project. NULL on success (img->data owned by the caller). */
static const char *download_image(const char *url, size_t cap, size_t expected, const char *project,
                                  int *progress, int span, image_buf_t *img)
{
    static char message[96];
    *img = (image_buf_t){.cap = cap, .expected = expected, .progress = progress, .span = span};
    img->data = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!img->data) return "Out of memory";
    esp_http_client_config_t hc = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = collect_image,
        .user_data = img,
        .user_agent = "MackoDash",
        .timeout_ms = 20000,
        .buffer_size = 8192,      /* GitHub's download redirect has a very long Location header */
        .buffer_size_tx = 4096,
        .max_redirection_count = 5,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hc);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "download: %s, HTTP %d, %u bytes", esp_err_to_name(err), status, (unsigned)img->len);

    const char *problem = NULL;
    if (err != ESP_OK || status != 200 || img->len == 0 || (img->expected && img->len != img->expected)) {
        snprintf(message, sizeof(message), "Download failed (%s). Nothing was changed.",
                 err != ESP_OK ? esp_err_to_name(err) : "incomplete");
        problem = message;
    } else {
        const esp_app_desc_t *desc = (const esp_app_desc_t *)(img->data + APP_DESC_OFFSET);
        if (img->len < APP_DESC_OFFSET + sizeof(esp_app_desc_t) || (uint8_t)img->data[0] != IMAGE_MAGIC ||
                desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
                strncmp(desc->project_name, project, sizeof(desc->project_name)) != 0) {
            problem = "The download is not the right firmware. Nothing was changed.";
        }
    }
    if (problem) {
        free(img->data);
        img->data = NULL;
    }
    return problem;
}

typedef struct {
    const esp_partition_t *part;
    const char *data;
    size_t len;
} write_job_t;

/* runs on the internal-RAM helper task */
static esp_err_t write_image_job(void *arg)
{
    write_job_t *job = (write_job_t *)arg;
    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(job->part, job->len, &ota);
    if (err != ESP_OK) return err;
    const size_t chunk = 64 * 1024;
    for (size_t off = 0; off < job->len; off += chunk) {
        size_t n = job->len - off < chunk ? job->len - off : chunk;
        err = esp_ota_write(ota, job->data + off, n);
        if (err != ESP_OK) {
            esp_ota_abort(ota);
            return err;
        }
        lock();
        s_status.progress = 85 + (int)((int64_t)(off + n) * 14 / job->len);
        unlock();
    }
    err = esp_ota_end(ota);   /* checks the image hash */
    if (err != ESP_OK) return err;
    return esp_ota_set_boot_partition(job->part);
}

static void install_update(void)
{
    if (upd_state() != WU_UPD_AVAILABLE || !s_bin_url) return;
    if (wifi_state() != WU_WIFI_CONNECTED) {
        set_upd(WU_UPD_ERROR, "Connect to Wi-Fi first");
        return;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        set_upd(WU_UPD_ERROR, "No free firmware slot");
        return;
    }
    lock();
    s_status.progress = 0;
    unlock();
    set_upd(WU_UPD_DOWNLOADING, "Downloading...");

    /* The whole image is downloaded into PSRAM and checked before anything is
       written, so a dropped connection can never leave a half-written slot. */
    image_buf_t img;
    const char *problem = download_image(s_bin_url, part->size, s_bin_size > 0 ? (size_t)s_bin_size : 0,
                                         esp_app_get_description()->project_name, &s_status.progress, 85, &img);
    if (problem) {
        set_upd(WU_UPD_ERROR, "%s", problem);
        return;
    }

    set_upd(WU_UPD_DOWNLOADING, "Installing...");
    write_job_t job = {.part = part, .data = img.data, .len = img.len};
    esp_err_t err = run_flash_job(write_image_job, &job);
    free(img.data);
    if (err != ESP_OK) {
        set_upd(WU_UPD_ERROR, err == ESP_ERR_OTA_VALIDATE_FAILED ? "The download was damaged. Nothing was changed."
                                                                 : "Install failed (%s)", esp_err_to_name(err));
        return;
    }
    lock();
    s_status.progress = 100;
    unlock();
    set_upd(WU_UPD_REBOOTING, "Installed. Restarting...");
    ESP_LOGI(TAG, "update installed to %s, restarting", part->label);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

/* ---------------------------- knob updates --------------------------- */

/* The dash downloads the knob firmware into PSRAM, serves it over plain HTTP
   on the knob network and tells the knob (MD frame) where to fetch it. */

static char *s_knob_bin_url;
static int s_knob_bin_size;
static image_buf_t s_knob_img;
static int s_knob_phase;                /* 0 idle, 1 sending, 2 waiting for the new version */
static int64_t s_knob_deadline_us;
static int64_t s_knob_announce_us;
static volatile size_t s_srv_sent;
static volatile bool s_srv_client;
static volatile bool s_srv_done;
static volatile bool s_srv_ok;

static void set_knob_upd(wu_upd_state_t state, const char *fmt, ...)
{
    lock();
    s_status.knob_upd = state;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(s_status.knob_upd_detail, sizeof(s_status.knob_upd_detail), fmt, ap);
        va_end(ap);
    } else {
        s_status.knob_upd_detail[0] = '\0';
    }
    unlock();
}

static void knob_server_task(void *arg)
{
    (void)arg;
    bool ok = false;
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener >= 0) {
        int yes = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(KNOB_HTTP_PORT),
                                   .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(listener, 1) == 0) {
            struct timeval wait = {.tv_sec = 60, .tv_usec = 0};
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(listener, &fds);
            if (select(listener + 1, &fds, NULL, NULL, &wait) > 0) {
                int client = accept(listener, NULL, NULL);
                if (client >= 0) {
                    s_srv_client = true;
                    struct timeval io = {.tv_sec = 15, .tv_usec = 0};
                    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &io, sizeof(io));
                    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
                    char request[512];
                    recv(client, request, sizeof(request), 0);    /* any GET gets the image */
                    char header[128];
                    int hl = snprintf(header, sizeof(header),
                                      "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                                      "Content-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)s_knob_img.len);
                    ok = send(client, header, hl, 0) == hl;
                    for (size_t off = 0; ok && off < s_knob_img.len;) {
                        size_t n = s_knob_img.len - off < 4096 ? s_knob_img.len - off : 4096;
                        int sent = send(client, s_knob_img.data + off, n, 0);
                        if (sent <= 0) ok = false;
                        else off += sent, s_srv_sent = off;
                    }
                    shutdown(client, SHUT_WR);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    close(client);
                }
            }
        }
        close(listener);
    }
    s_srv_ok = ok;
    s_srv_done = true;
    vTaskDeleteWithCaps(NULL);
}

static void knob_check(void)
{
    if (wifi_state() != WU_WIFI_CONNECTED) {
        set_knob_upd(WU_UPD_ERROR, "Connect the dash to Wi-Fi first");
        return;
    }
    char version[24] = "";
    if (!rotary_controller_wireless_status(version, sizeof(version))) {
        set_knob_upd(WU_UPD_ERROR, "The knob is not connected");
        return;
    }
    set_knob_upd(WU_UPD_CHECKING, "Checking for knob updates...");
    release_t rel;
    const char *problem = fetch_release(KNOB_RELEASE_API_URL, KNOB_FIRMWARE_ASSET, &rel);
    if (problem) {
        set_knob_upd(WU_UPD_ERROR, "%s", problem);
        return;
    }
    free(s_knob_bin_url);
    s_knob_bin_url = rel.url;
    s_knob_bin_size = rel.size;
    lock();
    snprintf(s_status.knob_latest, sizeof(s_status.knob_latest), "%s", rel.latest);
    unlock();
    if (!version[0]) {
        set_knob_upd(WU_UPD_USB_ONLY, "Install knob 1.2.0 with the USB updater once to update it from here");
    } else if (compare_versions(rel.latest, version) <= 0) {
        set_knob_upd(WU_UPD_CURRENT, "The knob has the latest version");
    } else if (!s_knob_bin_url) {
        set_knob_upd(WU_UPD_USB_ONLY, "Knob %s must be installed with the USB updater", rel.latest);
    } else {
        set_knob_upd(WU_UPD_AVAILABLE, "Knob %s is ready to install", rel.latest);
    }
}

static void knob_install(void)
{
    lock();
    bool ready = s_status.knob_upd == WU_UPD_AVAILABLE;
    s_status.knob_progress = 0;
    unlock();
    if (!ready || !s_knob_bin_url || s_knob_phase) return;
    set_knob_upd(WU_UPD_DOWNLOADING, "Downloading knob firmware...");
    const char *problem = download_image(s_knob_bin_url, KNOB_IMAGE_MAX,
                                         s_knob_bin_size > 0 ? (size_t)s_knob_bin_size : 0, KNOB_PROJECT,
                                         &s_status.knob_progress, 40, &s_knob_img);
    if (problem) {
        set_knob_upd(WU_UPD_ERROR, "%s", problem);
        return;
    }
    s_srv_sent = 0;
    s_srv_client = s_srv_done = s_srv_ok = false;
    if (xTaskCreateWithCaps(knob_server_task, "knob_srv", 4096, NULL, 4, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        free(s_knob_img.data);
        s_knob_img.data = NULL;
        set_knob_upd(WU_UPD_ERROR, "Out of memory");
        return;
    }
    s_knob_phase = 1;
    s_knob_announce_us = 0;
    s_knob_deadline_us = esp_timer_get_time() + 90LL * 1000 * 1000;
    set_knob_upd(WU_UPD_DOWNLOADING, "Sending to the knob...");
}

/* Drives a running knob update; called from the worker loop. */
static void knob_tick(void)
{
    if (!s_knob_phase) return;
    int64_t now = esp_timer_get_time();
    char latest[24];
    lock();
    snprintf(latest, sizeof(latest), "%s", s_status.knob_latest);
    unlock();

    if (s_knob_phase == 1) {
        if (!s_srv_client && now >= s_knob_announce_us) {
            s_knob_announce_us = now + 2000000;
            rotary_controller_send_knob_update(KNOB_HTTP_PORT, (uint32_t)s_knob_img.len, latest);
        }
        if (s_knob_img.len) {
            lock();
            s_status.knob_progress = 40 + (int)((int64_t)s_srv_sent * 55 / s_knob_img.len);
            unlock();
        }
        if (s_srv_done) {
            free(s_knob_img.data);
            s_knob_img.data = NULL;
            if (!s_srv_ok) {
                s_knob_phase = 0;
                set_knob_upd(WU_UPD_ERROR, s_srv_client ? "Sending to the knob failed. Try again."
                                                        : "The knob did not answer. Try again.");
                return;
            }
            s_knob_phase = 2;
            s_knob_deadline_us = now + 90LL * 1000 * 1000;
            set_knob_upd(WU_UPD_DOWNLOADING, "The knob is installing...");
        }
        return;
    }

    char version[24] = "";
    bool connected = rotary_controller_wireless_status(version, sizeof(version));
    if (connected && compare_versions(version, latest) == 0) {
        s_knob_phase = 0;
        lock();
        s_status.knob_progress = 100;
        unlock();
        set_knob_upd(WU_UPD_CURRENT, "Knob updated to %s", latest);
    } else if (now > s_knob_deadline_us) {
        s_knob_phase = 0;
        set_knob_upd(WU_UPD_ERROR, "The knob did not come back with the new version");
    }
}

/* ------------------------------ worker ------------------------------ */

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        wu_msg_t msg;
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(250)) != pdTRUE) {
            knob_poll();
            knob_tick();
            if (s_scan_retry_at && (int32_t)(xTaskGetTickCount() - s_scan_retry_at) >= 0) {
                s_scan_retry_at = 0;
                scan_start();
            }
            continue;
        }
        switch (msg.cmd) {
        case CMD_RADIO_ON: radio_on(); break;
        case CMD_RADIO_OFF: radio_off(); break;
        case CMD_SCAN:
            s_scan_retries = 0;
            scan_start();
            break;
        case CMD_SCAN_DONE: scan_done(); break;
        case CMD_CONNECT: {
            char ssid[33], pass[65];
            lock();
            snprintf(ssid, sizeof(ssid), "%s", s_req_ssid);
            snprintf(pass, sizeof(pass), "%s", s_req_pass);
            unlock();
            radio_on();
            if (s_radio_on) start_connect(ssid, pass, true);
            break;
        }
        case CMD_DISCONNECTED: handle_disconnect(msg.arg); break;
        case CMD_GOT_IP: handle_got_ip((uint32_t)msg.arg); break;
        case CMD_FORGET:
            creds_erase();
            if (s_radio_on) {
                wu_wifi_state_t state = wifi_state();
                if (state == WU_WIFI_CONNECTED || state == WU_WIFI_CONNECTING) s_ignore_disconnects++;
                esp_wifi_disconnect();
                set_wifi(WU_WIFI_IDLE, "", "Not connected");
            }
            break;
        case CMD_CHECK: check_for_update(); break;
        case CMD_INSTALL: install_update(); break;
        case CMD_KNOB_ON: knob_on(); break;
        case CMD_KNOB_OFF: knob_off(); break;
        case CMD_KNOB_CHECK: knob_check(); break;
        case CMD_KNOB_INSTALL: knob_install(); break;
        default: break;
        }
    }
}

static void ensure_task(void)
{
    if (s_queue) return;
    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t big_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_queue = xQueueCreate(16, sizeof(wu_msg_t));
    /* PSRAM stack: internal RAM is scarce. Flash writes go through run_flash_job(). */
    BaseType_t ok = xTaskCreateWithCaps(worker, "wifi_update", 16384, NULL, 4, NULL, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "worker task %s (internal RAM free %u, largest block %u)", ok == pdPASS ? "started" : "FAILED",
             (unsigned)free_int, (unsigned)big_int);
    if (ok != pdPASS) set_wifi(WU_WIFI_ERROR, "", "Not enough memory to start Wi-Fi");
}

/* ------------------------------ public ------------------------------ */

void wifi_update_init(void)
{
    lock();
    unlock();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "new firmware started fine, keeping it");
        esp_ota_mark_app_valid_cancel_rollback();
    }
    creds_load();
    /* knob network name: MackoDash-XXXX from this dash's MAC address */
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    lock();
    snprintf(s_status.knob_ssid, sizeof(s_status.knob_ssid), "%s%02X%02X", WU_KNOB_SSID_PREFIX, mac[4], mac[5]);
    unlock();
}

void wifi_update_radio_on(void) { ensure_task(); post(CMD_RADIO_ON, 0); }
void wifi_update_radio_off(void) { if (s_queue) post(CMD_RADIO_OFF, 0); }
void wifi_update_scan(void) { ensure_task(); post(CMD_SCAN, 0); }
void wifi_update_forget(void) { ensure_task(); post(CMD_FORGET, 0); }
void wifi_update_check(void) { ensure_task(); post(CMD_CHECK, 0); }
void wifi_update_install(void) { ensure_task(); post(CMD_INSTALL, 0); }
void wifi_update_knob_network(bool on) { ensure_task(); post(on ? CMD_KNOB_ON : CMD_KNOB_OFF, 0); }
void wifi_update_knob_check(void) { ensure_task(); post(CMD_KNOB_CHECK, 0); }
void wifi_update_knob_install(void) { ensure_task(); post(CMD_KNOB_INSTALL, 0); }

void wifi_update_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0]) return;
    ensure_task();
    lock();
    snprintf(s_req_ssid, sizeof(s_req_ssid), "%s", ssid);
    snprintf(s_req_pass, sizeof(s_req_pass), "%s", password ? password : "");
    unlock();
    post(CMD_CONNECT, 0);
}

int wifi_update_get_aps(wu_ap_t *out, int max)
{
    lock();
    int n = s_ap_count < max ? s_ap_count : max;
    memcpy(out, s_aps, sizeof(wu_ap_t) * n);
    unlock();
    return n;
}

bool wifi_update_saved_ssid(char *out, size_t len)
{
    lock();
    snprintf(out, len, "%s", s_saved_ssid);
    unlock();
    return out[0] != '\0';
}

void wifi_update_get_status(wu_status_t *out)
{
    lock();
    *out = s_status;
    unlock();
}
