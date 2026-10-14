#include "hvac_link.h"

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

#define XFER_TIMEOUT_MS 50

esp_err_t hvac_link_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = HVAC_I2C_ADDR,
        .scl_speed_hz = HVAC_I2C_HZ,
    };
    s_bus = bus;
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

esp_err_t hvac_link_probe(void)
{
    if (!s_bus) return ESP_ERR_INVALID_STATE;
    return i2c_master_probe(s_bus, HVAC_I2C_ADDR, XFER_TIMEOUT_MS);
}

esp_err_t hvac_link_set(uint8_t fan, hvac_mode_t mode, uint8_t temp)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (fan > HVAC_FAN_MAX) fan = HVAC_FAN_MAX;
    if (mode > HVAC_MODE_MAX) mode = HVAC_MODE_MAX;
    if (temp > HVAC_TEMP_MAX) temp = HVAC_TEMP_MAX;

    uint8_t pkt[HVAC_PKT_LEN] = {
        HVAC_PKT_HEADER, fan, (uint8_t)mode, temp, (uint8_t)(fan + mode + temp),
    };
    return i2c_master_transmit(s_dev, pkt, sizeof(pkt), XFER_TIMEOUT_MS);
}
