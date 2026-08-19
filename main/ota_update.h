#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	esp_err_t (*start)(char *ssid, size_t ssid_len, char *pass, size_t pass_len, char *url, size_t url_len);
	esp_err_t (*stop)(void);
	bool (*is_running)(void);
	bool (*is_starting)(void);
	esp_err_t (*get_last_error)(void);
	const char *(*get_status_detail)(void);
} ota_update_bridge_ops_t;

esp_err_t ota_update_register_bridge(const ota_update_bridge_ops_t *ops);
void ota_update_unregister_bridge(void);

esp_err_t ota_update_start_ap_server(void);
esp_err_t ota_update_stop_ap_server(void);
bool ota_update_is_running(void);
bool ota_update_is_starting(void);
esp_err_t ota_update_get_last_error(void);
const char *ota_update_get_status_detail(void);

const char *ota_update_get_ssid(void);
const char *ota_update_get_password(void);
const char *ota_update_get_url(void);

#ifdef __cplusplus
}
#endif
