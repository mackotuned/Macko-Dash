#pragma once

/*
 * Wi-Fi + firmware update over the ESP32-C6 co-processor (esp_hosted).
 *
 * All radio and network work runs on one background task; the UI only
 * posts requests and polls wifi_update_get_status(). The radio is off
 * unless the UI turns it on (Wi-Fi / Firmware Update pages).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WU_MAX_APS 16

typedef struct {
    char ssid[33];      /* exact network name, may contain trailing spaces */
    int8_t rssi;
    bool secure;
} wu_ap_t;

typedef enum {
    WU_WIFI_OFF = 0,
    WU_WIFI_STARTING,
    WU_WIFI_IDLE,           /* radio on, not connected */
    WU_WIFI_CONNECTING,
    WU_WIFI_CONNECTED,
    WU_WIFI_FAILED,         /* last connect failed, see wifi_detail */
    WU_WIFI_ERROR,          /* Wi-Fi chip did not start */
} wu_wifi_state_t;

typedef enum {
    WU_UPD_IDLE = 0,
    WU_UPD_CHECKING,
    WU_UPD_CURRENT,         /* installed version is the latest */
    WU_UPD_AVAILABLE,       /* newer version ready to install */
    WU_UPD_USB_ONLY,        /* newer version must be installed with the USB updater */
    WU_UPD_DOWNLOADING,
    WU_UPD_REBOOTING,
    WU_UPD_ERROR,
} wu_upd_state_t;

typedef struct {
    wu_wifi_state_t wifi;
    char ssid[33];          /* network connected / connecting / failed */
    char wifi_detail[64];
    bool scanning;
    uint32_t scan_gen;      /* bumps every time new scan results arrive */
    wu_upd_state_t upd;
    char latest[24];        /* latest version on GitHub, e.g. "3.3.0" */
    char upd_detail[96];
    int progress;           /* 0-100 while downloading */
} wu_status_t;

/* Call once after the dash is up: confirms this firmware booted fine
   (cancels rollback) and loads the saved network. */
void wifi_update_init(void);

void wifi_update_radio_on(void);    /* also connects to the saved network */
void wifi_update_radio_off(void);   /* ignored while an update downloads */
void wifi_update_scan(void);
int wifi_update_get_aps(wu_ap_t *out, int max);
void wifi_update_connect(const char *ssid, const char *password);
void wifi_update_forget(void);
bool wifi_update_saved_ssid(char *out, size_t len);

void wifi_update_check(void);
void wifi_update_install(void);
void wifi_update_get_status(wu_status_t *out);

#ifdef __cplusplus
}
#endif
