/* Stand-in for ota_update.c, compiled only when
   CONFIG_HONDA_DASH_ENABLE_WIFI_OTA is off (see main/CMakeLists.txt).
   The settings menu's Update tile is hidden in this build (see
   honda_dash_ui.c), so none of this is reachable at runtime -- it
   exists purely so the rest of the UI code still links cleanly
   without needing its own #ifdefs scattered through it. */

#include "ota_update.h"
#include <stddef.h>

esp_err_t ota_update_register_bridge(const ota_update_bridge_ops_t *ops)
{
    (void)ops;
    return ESP_ERR_NOT_SUPPORTED;
}

void ota_update_unregister_bridge(void)
{
}

esp_err_t ota_update_start_ap_server(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_update_stop_ap_server(void)
{
    return ESP_OK;
}

bool ota_update_is_running(void)
{
    return false;
}

bool ota_update_is_starting(void)
{
    return false;
}

esp_err_t ota_update_get_last_error(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

const char *ota_update_get_status_detail(void)
{
    return "wifi/OTA feature disabled in this build";
}

const char *ota_update_get_ssid(void)
{
    return "";
}

const char *ota_update_get_password(void)
{
    return "";
}

const char *ota_update_get_url(void)
{
    return "";
}
